/*
 * Execution trace packager for QEMU.
 * Copyright (c) 2013 Xilinx Inc.
 * Written by Edgar E. Iglesias
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include <unistd.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include "qemu/sockets.h"
#endif

#include "qemu-common.h"
#include "qemu/etrace.h"
#include "qemu/timer.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "cpu.h"
#include "exec/exec-all.h"

/* Still under development.  */
#define ETRACE_VERSION_MAJOR 0
#define ETRACE_VERSION_MINOR 0

enum {
    TYPE_EXEC = 1,
    TYPE_TB = 2,
    TYPE_NOTE = 3,
    TYPE_MEM = 4,
    TYPE_ARCH = 5,
    TYPE_BARRIER = 6,
    TYPE_OLD_EVENT_U64 = 7,
    TYPE_EVENT_U64 = 8,
    TYPE_INFO = 0x4554,
};

struct etrace_hdr {
    uint16_t type;
    uint16_t unit_id;
    uint32_t len;
} QEMU_PACKED;

enum etrace_info_flags {
    ETRACE_INFO_F_TB_CHAINING   = (1 << 0),
};

struct etrace_info_data {
    uint64_t attr;
    struct {
        uint16_t major;
        uint16_t minor;
    } version;
} QEMU_PACKED;

struct etrace_arch {
    struct {
        uint32_t arch_id;
        uint8_t arch_bits;
        uint8_t big_endian;
    } guest, host;
} QEMU_PACKED;

struct etrace_exec {
    uint64_t start_time;
} QEMU_PACKED;

struct etrace_note {
    uint64_t time;
} QEMU_PACKED;

struct etrace_mem {
    uint64_t time;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t value;
    uint32_t attr;
    uint8_t size;
    uint8_t padd[3];
} QEMU_PACKED;

struct etrace_tb {
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t host_addr;
    uint32_t guest_code_len;
    uint32_t host_code_len;
} QEMU_PACKED;

struct etrace_event_u64 {
    uint32_t flags;
    uint16_t unit_id;
    uint16_t __reserved;
    uint64_t time;
    uint64_t val;
    uint64_t prev_val;
    uint16_t dev_name_len;
    uint16_t event_name_len;
} QEMU_PACKED;

const char *qemu_arg_etrace;
const char *qemu_arg_etrace_flags;
struct etracer qemu_etracer = {0};
bool qemu_etrace_enabled;

void qemu_etrace_cleanup(void)
{
    etrace_close(&qemu_etracer);
}

struct {
    const char *name;
    enum qemu_etrace_flag flags;
} qemu_etrace_flagmap[] = {
    { "none", ETRACE_F_NONE },
    { "exec", ETRACE_F_EXEC },
    { "disas", ETRACE_F_TRANSLATION },
    { "mem", ETRACE_F_MEM },
    { "cpu", ETRACE_F_CPU },
    { "gpio", ETRACE_F_GPIO },
    { "all", ~0 },
    { NULL, 0 },
};

static uint64_t qemu_etrace_str2flags(const char *str, size_t len)
{
    uint64_t flags = 0;
    unsigned int pos = 0;

    while (qemu_etrace_flagmap[pos].name) {
        if (len != strlen(qemu_etrace_flagmap[pos].name)) {
            pos++;
            continue;
        }

        if (!memcmp(qemu_etrace_flagmap[pos].name, str, len)) {
            flags |= qemu_etrace_flagmap[pos].flags;
            break;
        }
        pos++;
    }
    if (!flags) {
        fprintf(stderr, "Invalid etrace flag %s\n", str);
        exit(EXIT_FAILURE);
    }
    return flags;
}

static uint64_t qemu_etrace_opts2flags(const char *opts)
{
    uint64_t flags = 0;
    const char *prev = opts, *end = opts;

    while (prev && *prev) {
        while (*end != ',' && *end != 0) {
            end++;
        }
        flags |= qemu_etrace_str2flags(prev, end - prev);
        while (*end == ',') {
            end++;
        }
        prev = end;
    }
    return flags;
}

static void etrace_write(struct etracer *t, const void *buf, size_t len)
{
    size_t r;

    r = fwrite(buf, 1, len, t->fp);
    if (feof(t->fp) || ferror(t->fp)) {
        fprintf(stderr, "Etrace peer EOF/disconnected!\n");
        /* FIXME: Allow qemu to continue?  */
        fclose(t->fp);
        t->fp = NULL;
        exit(1);
    }
    /* FIXME: Make this more robust.  */
    assert(r == len);
}

static void etrace_write_header(struct etracer *t, uint16_t type,
                                uint16_t unit_id, uint32_t len)
{
    struct etrace_hdr hdr = {
        .type = type,
        .unit_id = unit_id,
        .len = len
    };
    etrace_write(t, &hdr, sizeof hdr);
}

#define UNIX_PREFIX "unix:"

static int sk_unix_client(const char *descr)
{
#ifndef _WIN32
    struct sockaddr_un addr;
    int fd, nfd;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    printf("connect to %s\n", descr + strlen(UNIX_PREFIX));

    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, descr + strlen(UNIX_PREFIX),
            sizeof addr.sun_path - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) >= 0) {
        return fd;
    }

    printf("Failed to connect to %s, attempt to listen\n", addr.sun_path);
    unlink(addr.sun_path);
    /* Failed to connect. Bind, listen and accept.  */
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        goto fail;
    }

    listen(fd, 5);
    nfd = accept(fd, NULL, NULL);
    close(fd);
    return nfd;
fail:
    close(fd);
#endif
    return -1;
}

static FILE *etrace_open(const char *descr)
{
    FILE *fp = NULL;
    int fd = -1;

    if (descr == NULL) {
        return NULL;
    }

    if (memcmp(UNIX_PREFIX, descr, strlen(UNIX_PREFIX)) == 0) {
        /* UNIX.  */
        fd = sk_unix_client(descr);
        fp = fdopen(fd, "w");
    } else {
        fp = fopen(descr, "w");
    }
    return fp;
}

/*
 * Initialize a tracing context.
 *
 * arch_id should identify the architecture. Maybe the ELF machine code?
 */
bool etrace_init(struct etracer *t, const char *filename,
                 const char *opts,
                 unsigned int arch_id, unsigned int arch_bits)
{
    struct etrace_info_data id;
    struct etrace_arch arch;

    memset(t, 0, sizeof *t);
    t->fp = etrace_open(filename);
    if (!t->fp) {
        return false;
    }

    memset(&id, 0, sizeof id);
    id.version.major = ETRACE_VERSION_MAJOR;
    id.version.minor = ETRACE_VERSION_MINOR;
    id.attr = 0;
    if (qemu_loglevel_mask(CPU_LOG_TB_NOCHAIN)) {
        id.attr |= ETRACE_INFO_F_TB_CHAINING;
    }
    etrace_write_header(t, TYPE_INFO, 0, sizeof id);
    etrace_write(t, &id, sizeof id);


    /* FIXME: Pass info about host.  */
    arch.guest.arch_id = arch_id;
    t->arch_bits = arch.guest.arch_bits = arch_bits;
#ifdef TARGET_WORDS_BIGENDIAN
    arch.guest.big_endian = 1;
#endif
    etrace_write_header(t, TYPE_ARCH, 0, sizeof arch);
    etrace_write(t, &arch, sizeof arch);

    t->flags = qemu_etrace_opts2flags(opts);
    return true;
}

static void etrace_flush_exec_cache(struct etracer *t)
{
    size_t size64 = t->exec_cache.pos * sizeof t->exec_cache.t64[0];
    size_t size32 = t->exec_cache.pos * sizeof t->exec_cache.t32[0];
    size_t size = t->arch_bits == 32 ? size32 : size64;
    struct etrace_exec ex;

    if (!size) {
        return;
    }

    ex.start_time = t->exec_cache.start_time;

    etrace_write_header(t, TYPE_EXEC, t->exec_cache.unit_id, size + sizeof ex);
    etrace_write(t, &ex, sizeof ex);
    etrace_write(t, &t->exec_cache.t64[0], size);
    t->exec_cache.pos = 0;
    memset(&t->exec_cache.t64[0], 0, sizeof t->exec_cache.t64);

    /* A barrier indicates that the other side can assume order across the
       the barrier.  */
    etrace_write_header(t, TYPE_BARRIER, t->exec_cache.unit_id, 0);
}

#define PROXIMITY_MASK (~0xfff)

/* Check that the addresses are reasonably near. I.e we didnt change
   address space or similar.  */
static bool address_near(uint64_t a, uint64_t b)
{
    a &= PROXIMITY_MASK;
    b &= PROXIMITY_MASK;
    return a == b;
}

static bool qualify_merge(uint64_t start, uint64_t end,
                          uint64_t new_start, uint64_t new_end)
{
    if (end != new_start) {
        return false;
    }

    if (start == end || new_start == new_end) {
        return false;
    }

    if (!address_near(start, end)) {
        return false;
    }
    if (!address_near(new_start, new_end)) {
        return false;
    }

    return true;
}

/* Exec cache accessors. To avoid duplicating src code we use the cpp.  */
#define XC_ACCESSOR(field)                                                \
static inline void execache_set_ ## field(struct etracer *t,              \
                                          unsigned int pos, uint64_t v)   \
{                                                                         \
    if (t->arch_bits == 32) {                                             \
        t->exec_cache.t32[pos].field = v;                                 \
    } else {                                                              \
        t->exec_cache.t64[pos].field = v;                                 \
    }                                                                     \
}                                                                         \
static inline uint64_t execache_get_ ## field(struct etracer *t,          \
                                              unsigned int pos)           \
{                                                                         \
    if (t->arch_bits == 32) {                                             \
        return t->exec_cache.t32[pos].field;                              \
    } else {                                                              \
        return t->exec_cache.t64[pos].field;                              \
    }                                                                     \
}

XC_ACCESSOR(start)
XC_ACCESSOR(end)
XC_ACCESSOR(duration)

/*
 * dump an execution record.
 *
 * unit_id idenfies the master, e.g CPU #0 or #1 etc.
 *
 */
void etrace_dump_exec(struct etracer *t, unsigned int unit_id,
                      uint64_t start, uint64_t end,
                      uint64_t start_time, uint32_t duration)
{
    unsigned int pos;

    if (unit_id != t->exec_cache.unit_id) {
        etrace_flush_exec_cache(t);
        t->exec_cache.unit_id = unit_id;
    }

    pos = t->exec_cache.pos;
    if (pos == 0) {
        t->exec_cache.start_time = start_time;
    }

    assert(t->arch_bits == 32 || t->arch_bits == 64);
    if (pos &&
        qualify_merge(execache_get_start(t, pos), execache_get_end(t, pos),
                      start, end)) {
        /* Reuse the old entry.  */
        pos -= 1;
        execache_set_duration(t, pos, execache_get_duration(t, pos) + duration);
    } else {
        /* Advance.  */
        t->exec_cache.pos += 1;
        execache_set_start(t, pos, start);
        execache_set_duration(t, pos, duration);
    }

    execache_set_end(t, pos, end);
    if (!qemu_loglevel_mask(CPU_LOG_TB_NOCHAIN)) {
        assert(execache_get_start(t, pos) <= execache_get_end(t, pos));
    }

    if (t->exec_cache.pos == EXEC_CACHE_SIZE) {
        etrace_flush_exec_cache(t);
    }
}

static void etrace_dump_guestmem(struct etracer *t, AddressSpace *as,
                                 uint64_t guest_vaddr, uint64_t guest_paddr,
                                 size_t guest_len)
{
#if defined(CONFIG_USER_ONLY)
    /* Currently, user mode address are directly addressable.  */
    etrace_write(t, (void *) (uintptr_t) guest_vaddr, guest_len);
#else
    unsigned char buf[8 * 1024];

    /* Once we have per-master address-space support, we can assert()
       as not beeing NULL. But for now, provide this fallback.  */
    if (as == NULL) {
        as = &address_space_memory;
    }

    /* TODO: We know that tb guest mem is mapped in at this time, so we could
       dig out the host ram pointer and directly write from it.  */
    while (guest_len) {
        unsigned int copylen = guest_len > sizeof buf ? sizeof buf : guest_len;

        address_space_rw(as, guest_paddr, MEMTXATTRS_UNSPECIFIED, buf, copylen, 0);
        etrace_write(t, buf, copylen);
        guest_len -= copylen;
    }
#endif
}

/*
 * Dump a pkg of TB info.
 *
 * unit_id idenfies the master
 * guest vaddr and paddr are the virtual and physical addresses
 * containing guest code.
 *
 * host_buf points the translated host machine code.
 */
void etrace_dump_tb(struct etracer *t, AddressSpace *as, uint16_t unit_id,
                    uint64_t guest_vaddr, uint64_t guest_paddr,
                    size_t guest_len,
                    void *host_buf, size_t host_len)
{
    struct etrace_tb tb;
    size_t size;

    tb.vaddr = guest_vaddr;
    tb.paddr = guest_paddr;
    tb.host_addr = (intptr_t) host_buf;
    tb.guest_code_len = guest_len;
    tb.host_code_len = host_len;

    size = sizeof tb + guest_len + host_len;
    /* Write headers.  */
    etrace_write_header(t, TYPE_TB, unit_id, size);
    etrace_write(t, &tb, sizeof tb);
    /* Guest code.  */
    etrace_dump_guestmem(t, as, guest_vaddr, guest_paddr, guest_len);
    /* Host/native code.  */
    etrace_write(t, host_buf, host_len);
}

static uint64_t etrace_time(void)
{
#if defined(CONFIG_USER_ONLY)
    return 0;
#else
    return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);;
#endif
}

void etrace_mem_access(struct etracer *t, uint16_t unit_id,
                       uint64_t guest_vaddr, uint64_t guest_paddr,
                       size_t size, uint64_t attr, uint64_t val)
{
    struct etrace_mem mem;

    etrace_flush_exec_cache(t);
    mem.time = etrace_time();
    mem.vaddr = guest_vaddr;
    mem.paddr = guest_paddr;
    mem.attr = attr;
    mem.size = size;
    mem.value = val;

    /* Write headers.  */
    etrace_write_header(t, TYPE_MEM, unit_id, sizeof mem);
    etrace_write(t, &mem, sizeof mem);
}

void etrace_dump_exec_start(struct etracer *t,
                            unsigned int unit_id,
                            uint64_t start)
{
    assert(!t->exec_start_valid);
    t->exec_start = start;
    t->exec_start_time = etrace_time();
    t->exec_start_valid = true;
}

void etrace_dump_exec_end(struct etracer *t,
                          unsigned int unit_id,
                          uint64_t end)
{
    int64_t tdiff;
    if (!t->exec_start_valid) {
        printf("exec_start not valid! %" PRIx64 " %" PRIx64 "\n", t->exec_start, end);
    }
    tdiff = etrace_time() - t->exec_start_time;
    if (tdiff < 0) {
        printf("tdiff=%" PRId64 "\n", tdiff);
        fflush(NULL);
    }
    assert(tdiff >= 0);
    assert(t->exec_start_valid);
    t->exec_start_valid = false;
    etrace_dump_exec(t, unit_id, t->exec_start, end, t->exec_start_time, tdiff);
}

void etrace_note_write(struct etracer *t, unsigned int unit_id,
                       void *buf, size_t len)
{
    struct etrace_note nt;

    etrace_flush_exec_cache(t);

    nt.time = etrace_time();
    etrace_write_header(t, TYPE_NOTE, unit_id, sizeof nt + len);
    etrace_write(t, &nt, sizeof nt);
    etrace_write(t, buf, len);
}

int etrace_note_fprintf(FILE *fp,
                        const char *fmt, ...)
{
    struct etracer *t = (void *) fp;
    va_list ap;
    char *s;
    int r;

    va_start(ap, fmt);
    r = vasprintf(&s, fmt, ap);
    if (r > 0) {
        etrace_note_write(t, t->current_unit_id, s, r);
    }
    va_end(ap);
    return r;
}

void etrace_event_u64(struct etracer *t, uint16_t unit_id,
                      uint32_t flags,
                      const char *dev_name,
                      const char *event_name,
                      uint64_t val, uint64_t prev_val)
{
    struct etrace_event_u64 event;
    size_t dev_len, event_len;

    etrace_flush_exec_cache(t);

    dev_len = strlen(dev_name) + 1;
    event_len = strlen(event_name) + 1;

    event.time = etrace_time();
    event.flags = flags;
    event.unit_id = unit_id;
    event.dev_name_len = dev_len;
    event.event_name_len = event_len;
    event.val = val;
    event.prev_val = prev_val;
    etrace_write_header(t, TYPE_EVENT_U64, unit_id,
                        sizeof event + dev_len + event_len);
    etrace_write(t, &event, sizeof event);
    etrace_write(t, dev_name, dev_len);
    etrace_write(t, event_name, event_len);
}

void etrace_close(struct etracer *t)
{
    if (t->fp) {
        etrace_flush_exec_cache(t);
        fclose(t->fp);
    }
}
