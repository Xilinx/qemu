/*
 * gdb server stub
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
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
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "cpu.h"
#ifdef CONFIG_USER_ONLY
#include "qemu.h"
#else
#include "monitor/monitor.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "sysemu/sysemu.h"
#include "exec/gdbstub.h"
#include "hw/remote-port.h"
#endif

#define MAX_PACKET_LENGTH 4096

#include "qemu/sockets.h"
#include "sysemu/hw_accel.h"
#include "sysemu/kvm.h"
#include "exec/semihost.h"
#include "exec/exec-all.h"

#ifdef CONFIG_USER_ONLY
#define GDB_ATTACHED "0"
#else
#define GDB_ATTACHED "1"
#endif

static inline int target_memory_rw_debug(CPUState *cpu, target_ulong addr,
                                         uint8_t *buf, int len, bool is_write)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (cc->memory_rw_debug) {
        return cc->memory_rw_debug(cpu, addr, buf, len, is_write);
    }
    return cpu_memory_rw_debug(cpu, addr, buf, len, is_write);
}

enum {
    GDB_SIGNAL_0 = 0,
    GDB_SIGNAL_INT = 2,
    GDB_SIGNAL_QUIT = 3,
    GDB_SIGNAL_TRAP = 5,
    GDB_SIGNAL_ABRT = 6,
    GDB_SIGNAL_ALRM = 14,
    GDB_SIGNAL_IO = 23,
    GDB_SIGNAL_XCPU = 24,
    GDB_SIGNAL_UNKNOWN = 143
};

#ifdef CONFIG_USER_ONLY

/* Map target signal numbers to GDB protocol signal numbers and vice
 * versa.  For user emulation's currently supported systems, we can
 * assume most signals are defined.
 */

static int gdb_signal_table[] = {
    0,
    TARGET_SIGHUP,
    TARGET_SIGINT,
    TARGET_SIGQUIT,
    TARGET_SIGILL,
    TARGET_SIGTRAP,
    TARGET_SIGABRT,
    -1, /* SIGEMT */
    TARGET_SIGFPE,
    TARGET_SIGKILL,
    TARGET_SIGBUS,
    TARGET_SIGSEGV,
    TARGET_SIGSYS,
    TARGET_SIGPIPE,
    TARGET_SIGALRM,
    TARGET_SIGTERM,
    TARGET_SIGURG,
    TARGET_SIGSTOP,
    TARGET_SIGTSTP,
    TARGET_SIGCONT,
    TARGET_SIGCHLD,
    TARGET_SIGTTIN,
    TARGET_SIGTTOU,
    TARGET_SIGIO,
    TARGET_SIGXCPU,
    TARGET_SIGXFSZ,
    TARGET_SIGVTALRM,
    TARGET_SIGPROF,
    TARGET_SIGWINCH,
    -1, /* SIGLOST */
    TARGET_SIGUSR1,
    TARGET_SIGUSR2,
#ifdef TARGET_SIGPWR
    TARGET_SIGPWR,
#else
    -1,
#endif
    -1, /* SIGPOLL */
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
#ifdef __SIGRTMIN
    __SIGRTMIN + 1,
    __SIGRTMIN + 2,
    __SIGRTMIN + 3,
    __SIGRTMIN + 4,
    __SIGRTMIN + 5,
    __SIGRTMIN + 6,
    __SIGRTMIN + 7,
    __SIGRTMIN + 8,
    __SIGRTMIN + 9,
    __SIGRTMIN + 10,
    __SIGRTMIN + 11,
    __SIGRTMIN + 12,
    __SIGRTMIN + 13,
    __SIGRTMIN + 14,
    __SIGRTMIN + 15,
    __SIGRTMIN + 16,
    __SIGRTMIN + 17,
    __SIGRTMIN + 18,
    __SIGRTMIN + 19,
    __SIGRTMIN + 20,
    __SIGRTMIN + 21,
    __SIGRTMIN + 22,
    __SIGRTMIN + 23,
    __SIGRTMIN + 24,
    __SIGRTMIN + 25,
    __SIGRTMIN + 26,
    __SIGRTMIN + 27,
    __SIGRTMIN + 28,
    __SIGRTMIN + 29,
    __SIGRTMIN + 30,
    __SIGRTMIN + 31,
    -1, /* SIGCANCEL */
    __SIGRTMIN,
    __SIGRTMIN + 32,
    __SIGRTMIN + 33,
    __SIGRTMIN + 34,
    __SIGRTMIN + 35,
    __SIGRTMIN + 36,
    __SIGRTMIN + 37,
    __SIGRTMIN + 38,
    __SIGRTMIN + 39,
    __SIGRTMIN + 40,
    __SIGRTMIN + 41,
    __SIGRTMIN + 42,
    __SIGRTMIN + 43,
    __SIGRTMIN + 44,
    __SIGRTMIN + 45,
    __SIGRTMIN + 46,
    __SIGRTMIN + 47,
    __SIGRTMIN + 48,
    __SIGRTMIN + 49,
    __SIGRTMIN + 50,
    __SIGRTMIN + 51,
    __SIGRTMIN + 52,
    __SIGRTMIN + 53,
    __SIGRTMIN + 54,
    __SIGRTMIN + 55,
    __SIGRTMIN + 56,
    __SIGRTMIN + 57,
    __SIGRTMIN + 58,
    __SIGRTMIN + 59,
    __SIGRTMIN + 60,
    __SIGRTMIN + 61,
    __SIGRTMIN + 62,
    __SIGRTMIN + 63,
    __SIGRTMIN + 64,
    __SIGRTMIN + 65,
    __SIGRTMIN + 66,
    __SIGRTMIN + 67,
    __SIGRTMIN + 68,
    __SIGRTMIN + 69,
    __SIGRTMIN + 70,
    __SIGRTMIN + 71,
    __SIGRTMIN + 72,
    __SIGRTMIN + 73,
    __SIGRTMIN + 74,
    __SIGRTMIN + 75,
    __SIGRTMIN + 76,
    __SIGRTMIN + 77,
    __SIGRTMIN + 78,
    __SIGRTMIN + 79,
    __SIGRTMIN + 80,
    __SIGRTMIN + 81,
    __SIGRTMIN + 82,
    __SIGRTMIN + 83,
    __SIGRTMIN + 84,
    __SIGRTMIN + 85,
    __SIGRTMIN + 86,
    __SIGRTMIN + 87,
    __SIGRTMIN + 88,
    __SIGRTMIN + 89,
    __SIGRTMIN + 90,
    __SIGRTMIN + 91,
    __SIGRTMIN + 92,
    __SIGRTMIN + 93,
    __SIGRTMIN + 94,
    __SIGRTMIN + 95,
    -1, /* SIGINFO */
    -1, /* UNKNOWN */
    -1, /* DEFAULT */
    -1,
    -1,
    -1,
    -1,
    -1,
    -1
#endif
};
#else
/* In system mode we only need SIGINT and SIGTRAP; other signals
   are not yet supported.  */

enum {
    TARGET_SIGINT = 2,
    TARGET_SIGTRAP = 5
};

static int gdb_signal_table[] = {
    -1,
    -1,
    TARGET_SIGINT,
    -1,
    -1,
    TARGET_SIGTRAP
};
#endif

#ifdef CONFIG_USER_ONLY
static int target_signal_to_gdb (int sig)
{
    int i;
    for (i = 0; i < ARRAY_SIZE (gdb_signal_table); i++)
        if (gdb_signal_table[i] == sig)
            return i;
    return GDB_SIGNAL_UNKNOWN;
}
#endif

static int gdb_signal_to_target (int sig)
{
    if (sig < ARRAY_SIZE (gdb_signal_table))
        return gdb_signal_table[sig];
    else
        return -1;
}

/* #define DEBUG_GDB */

#ifdef DEBUG_GDB
# define DEBUG_GDB_GATE 1
#else
# define DEBUG_GDB_GATE 0
#endif

#define gdb_debug(fmt, ...) do { \
    if (DEBUG_GDB_GATE) { \
        fprintf(stderr, "%s: " fmt, __func__, ## __VA_ARGS__); \
    } \
} while (0)


typedef struct GDBRegisterState {
    int base_reg;
    int num_regs;
    gdb_reg_cb get_reg;
    gdb_reg_cb set_reg;
    const char *xml;
    struct GDBRegisterState *next;
} GDBRegisterState;

enum RSState {
    RS_INACTIVE,
    RS_IDLE,
    RS_GETLINE,
    RS_GETLINE_ESC,
    RS_GETLINE_RLE,
    RS_CHKSUM1,
    RS_CHKSUM2,
};

/* GDBClusters represent clusters with 1 or more CPUs.  */
typedef struct GDBCluster {
    struct {
        CPUState *first;
        CPUState *last;
    } cpus;
    bool attached;
} GDBCluster;

typedef struct GDBState {
    GDBCluster *clusters;
    int num_clusters;
    int cur_cluster;

    CPUState *c_cpu; /* current CPU for step/continue ops */
    CPUState *g_cpu; /* current CPU for other ops */
    CPUState *query_cpu; /* for q{f|s}ThreadInfo */
    int query_cluster;
    enum RSState state; /* parsing state */
    char line_buf[MAX_PACKET_LENGTH];
    int line_buf_index;
    int line_sum; /* running checksum */
    int line_csum; /* checksum at the end of the packet */
    uint8_t last_packet[MAX_PACKET_LENGTH + 4];
    int last_packet_len;
    int signal;
    bool client_connected;
    bool multiprocess;
    char threadid_str[64];
    bool break_on_guest_error;
    bool breakpoints_per_core;
#ifdef CONFIG_USER_ONLY
    int fd;
    int running_state;
#else
    CharBackend chr;
    Chardev *mon_chr;
#endif
    char syscall_buf[256];
    gdb_syscall_complete_cb current_syscall_cb;
} GDBState;

/* By default use no IRQs and no timers while single stepping so as to
 * make single stepping like an ICE HW step.
 */
static int sstep_flags = SSTEP_ENABLE|SSTEP_NOIRQ|SSTEP_NOTIMER;

static GDBState *gdbserver_state;

bool gdb_has_xml;

#ifndef CONFIG_USER_ONLY
static void gdb_output(GDBState *s, const char *msg, int len);
#endif

int semihosting_target = SEMIHOSTING_TARGET_AUTO;

#ifdef CONFIG_USER_ONLY
/* XXX: This is not thread safe.  Do we care?  */
static int gdbserver_fd = -1;

static int get_char(GDBState *s)
{
    uint8_t ch;
    int ret;

    for(;;) {
        ret = qemu_recv(s->fd, &ch, 1, 0);
        if (ret < 0) {
            if (errno == ECONNRESET)
                s->fd = -1;
            if (errno != EINTR)
                return -1;
        } else if (ret == 0) {
            close(s->fd);
            s->fd = -1;
            return -1;
        } else {
            break;
        }
    }
    return ch;
}
#endif

static enum {
    GDB_SYS_UNKNOWN,
    GDB_SYS_ENABLED,
    GDB_SYS_DISABLED,
} gdb_syscall_mode;

/* Decide if either remote gdb syscalls or native file IO should be used. */
int use_gdb_syscalls(void)
{
    SemihostingTarget target = semihosting_get_target();
    if (target == SEMIHOSTING_TARGET_NATIVE) {
        /* -semihosting-config target=native */
        return false;
    } else if (target == SEMIHOSTING_TARGET_GDB) {
        /* -semihosting-config target=gdb */
        return true;
    }

    /* -semihosting-config target=auto */
    /* On the first call check if gdb is connected and remember. */
    if (gdb_syscall_mode == GDB_SYS_UNKNOWN) {
        gdb_syscall_mode = (gdbserver_state ? GDB_SYS_ENABLED
                                            : GDB_SYS_DISABLED);
    }
    return gdb_syscall_mode == GDB_SYS_ENABLED;
}

/* Resume execution.  */
static inline void gdb_continue(GDBState *s)
{
#ifdef CONFIG_USER_ONLY
    s->running_state = 1;
#else
    if (!runstate_needs_reset()) {
        vm_start();
    }
#endif
}

static void put_buffer(GDBState *s, const uint8_t *buf, int len)
{
#ifdef CONFIG_USER_ONLY
    int ret;

    while (len > 0) {
        ret = send(s->fd, buf, len, 0);
        if (ret < 0) {
            if (errno != EINTR)
                return;
        } else {
            buf += ret;
            len -= ret;
        }
    }
#else
    /* XXX this blocks entire thread. Rewrite to use
     * qemu_chr_fe_write and background I/O callbacks */
    qemu_chr_fe_write_all(&s->chr, buf, len);
#endif
}

static inline int fromhex(int v)
{
    if (v >= '0' && v <= '9')
        return v - '0';
    else if (v >= 'A' && v <= 'F')
        return v - 'A' + 10;
    else if (v >= 'a' && v <= 'f')
        return v - 'a' + 10;
    else
        return 0;
}

static inline int tohex(int v)
{
    if (v < 10)
        return v + '0';
    else
        return v - 10 + 'a';
}

static void memtohex(char *buf, const uint8_t *mem, int len)
{
    int i, c;
    char *q;
    q = buf;
    for(i = 0; i < len; i++) {
        c = mem[i];
        *q++ = tohex(c >> 4);
        *q++ = tohex(c & 0xf);
    }
    *q = '\0';
}

static void hextomem(uint8_t *mem, const char *buf, int len)
{
    int i;

    for(i = 0; i < len; i++) {
        mem[i] = (fromhex(buf[0]) << 4) | fromhex(buf[1]);
        buf += 2;
    }
}

/* return -1 if error, 0 if OK */
static int put_packet_binary(GDBState *s, const char *buf, int len)
{
    int csum, i;
    uint8_t *p;

    for(;;) {
        p = s->last_packet;
        *(p++) = '$';
        memcpy(p, buf, len);
        p += len;
        csum = 0;
        for(i = 0; i < len; i++) {
            csum += buf[i];
        }
        *(p++) = '#';
        *(p++) = tohex((csum >> 4) & 0xf);
        *(p++) = tohex((csum) & 0xf);

        s->last_packet_len = p - s->last_packet;
        put_buffer(s, (uint8_t *)s->last_packet, s->last_packet_len);

#ifdef CONFIG_USER_ONLY
        i = get_char(s);
        if (i < 0)
            return -1;
        if (i == '+')
            break;
#else
        break;
#endif
    }
    return 0;
}

/* return -1 if error, 0 if OK */
static int put_packet(GDBState *s, const char *buf)
{
    gdb_debug("reply='%s'\n", buf);

    return put_packet_binary(s, buf, strlen(buf));
}

/* Encode data using the encoding for 'x' packets.  */
static int memtox(char *buf, const char *mem, int len)
{
    char *p = buf;
    char c;

    while (len--) {
        c = *(mem++);
        switch (c) {
        case '#': case '$': case '*': case '}':
            *(p++) = '}';
            *(p++) = c ^ 0x20;
            break;
        default:
            *(p++) = c;
            break;
        }
    }
    return p - buf;
}

static const char *get_feature_xml(const char *p, const char **newp,
                                   CPUClass *cc, CPUState *cpu)
{
    size_t len;
    int i;
    const char *name;
    static char target_xml[1024];

    len = 0;
    while (p[len] && p[len] != ':')
        len++;
    *newp = p + len;

    name = NULL;
    if (strncmp(p, "target.xml", len) == 0) {
        /* Generate the XML description for this CPU.  */
        if (1 || !target_xml[0]) {
            GDBRegisterState *r;

            snprintf(target_xml, sizeof(target_xml),
                     "<?xml version=\"1.0\"?>"
                     "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
                     "<target>"
                     "<xi:include href=\"%s\"/>",
                     cc->gdb_core_xml_file);

            for (r = cpu->gdb_regs; r; r = r->next) {
                pstrcat(target_xml, sizeof(target_xml), "<xi:include href=\"");
                pstrcat(target_xml, sizeof(target_xml), r->xml);
                pstrcat(target_xml, sizeof(target_xml), "\"/>");
            }
            if (cc->gdb_arch_name) {
                gchar *arch = cc->gdb_arch_name(cpu);
                pstrcat(target_xml, sizeof(target_xml), "<architecture>");
                pstrcat(target_xml, sizeof(target_xml), arch);
                pstrcat(target_xml, sizeof(target_xml), "</architecture>");
                g_free(arch);
            }
            pstrcat(target_xml, sizeof(target_xml), "</target>");
        }
        return target_xml;
    }
    for (i = 0; ; i++) {
        name = xml_builtin[i][0];
        if (!name || (strncmp(name, p, len) == 0 && strlen(name) == len))
            break;
    }
    return name ? xml_builtin[i][1] : NULL;
}

static int gdb_read_register(CPUState *cpu, uint8_t *mem_buf, int reg)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);
    CPUArchState *env = cpu->env_ptr;
    GDBRegisterState *r;

    if (reg < cc->gdb_num_core_regs) {
        return cc->gdb_read_register(cpu, mem_buf, reg);
    }

    for (r = cpu->gdb_regs; r; r = r->next) {
        if (r->base_reg <= reg && reg < r->base_reg + r->num_regs) {
            return r->get_reg(env, mem_buf, reg - r->base_reg);
        }
    }
    return 0;
}

static int gdb_write_register(CPUState *cpu, uint8_t *mem_buf, int reg)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);
    CPUArchState *env = cpu->env_ptr;
    GDBRegisterState *r;

    if (reg < cc->gdb_num_core_regs) {
        return cc->gdb_write_register(cpu, mem_buf, reg);
    }

    for (r = cpu->gdb_regs; r; r = r->next) {
        if (r->base_reg <= reg && reg < r->base_reg + r->num_regs) {
            return r->set_reg(env, mem_buf, reg - r->base_reg);
        }
    }
    return 0;
}

/* Register a supplemental set of CPU registers.  If g_pos is nonzero it
   specifies the first register number and these registers are included in
   a standard "g" packet.  Direction is relative to gdb, i.e. get_reg is
   gdb reading a CPU register, and set_reg is gdb modifying a CPU register.
 */

void gdb_register_coprocessor(CPUState *cpu,
                              gdb_reg_cb get_reg, gdb_reg_cb set_reg,
                              int num_regs, const char *xml, int g_pos)
{
    GDBRegisterState *s;
    GDBRegisterState **p;

    p = &cpu->gdb_regs;
    while (*p) {
        /* Check for duplicates.  */
        if (strcmp((*p)->xml, xml) == 0)
            return;
        p = &(*p)->next;
    }

    s = g_new0(GDBRegisterState, 1);
    s->base_reg = cpu->gdb_num_regs;
    s->num_regs = num_regs;
    s->get_reg = get_reg;
    s->set_reg = set_reg;
    s->xml = xml;

    /* Add to end of list.  */
    cpu->gdb_num_regs += num_regs;
    *p = s;
    if (g_pos) {
        if (g_pos != s->base_reg) {
            error_report("Error: Bad gdb register numbering for '%s', "
                         "expected %d got %d", xml, g_pos, s->base_reg);
        } else {
            cpu->gdb_num_g_regs = cpu->gdb_num_regs;
        }
    }
}

#ifndef CONFIG_USER_ONLY
/* Translate GDB watchpoint type to a flags value for cpu_watchpoint_* */
static inline int xlat_gdb_type(CPUState *cpu, int gdbtype)
{
    static const int xlat[] = {
        [GDB_WATCHPOINT_WRITE]  = BP_GDB | BP_MEM_WRITE,
        [GDB_WATCHPOINT_READ]   = BP_GDB | BP_MEM_READ,
        [GDB_WATCHPOINT_ACCESS] = BP_GDB | BP_MEM_ACCESS,
    };

    CPUClass *cc = CPU_GET_CLASS(cpu);
    int cputype = xlat[gdbtype];

    if (cc->gdb_stop_before_watchpoint) {
        cputype |= BP_STOP_BEFORE_ACCESS;
    }
    return cputype;
}

static void gdb_monitor(GDBState *s, const char *line)
{
    unsigned long val;
    const char *p = line;

    fprintf(stderr, "%s: %s\n", __func__, line);
    if (strncmp(p,"break_on_guest_error",20) == 0) {
        p += 20;
        fprintf(stderr, "p %s\n", p);
        if (*p == '=') {
            p++;
            val = strtoul(p, (char **)&p, 16);
            s->break_on_guest_error = !!val;
        }
        gdb_output(s, s->break_on_guest_error ? "1\n" : "0\n", 2);
        put_packet(s, "OK");
    }
}
#endif

static int gdb_breakpoint_insert(GDBState *s,
                                 target_ulong addr, target_ulong len, int type)
{
    CPUState *cpu;
    int err = 0;

    if (kvm_enabled()) {
        return kvm_insert_breakpoint(gdbserver_state->c_cpu, addr, len, type);
    }

    switch (type) {
    case GDB_BREAKPOINT_SW:
    case GDB_BREAKPOINT_HW:
        if (s->breakpoints_per_core) {
            cpu_breakpoint_insert(s->c_cpu, addr, BP_GDB, NULL);
            return 0;
        }
        CPU_FOREACH(cpu) {
            err = cpu_breakpoint_insert(cpu, addr, BP_GDB, NULL);
            if (err) {
                break;
            }
        }
        return err;
#ifndef CONFIG_USER_ONLY
    case GDB_WATCHPOINT_WRITE:
    case GDB_WATCHPOINT_READ:
    case GDB_WATCHPOINT_ACCESS:
        CPU_FOREACH(cpu) {
            err = cpu_watchpoint_insert(cpu, addr, len,
                                        xlat_gdb_type(cpu, type), NULL);
            if (err) {
                break;
            }
        }
        return err;
#endif
    default:
        return -ENOSYS;
    }
}

static int gdb_breakpoint_remove(GDBState *s,
                                 target_ulong addr, target_ulong len, int type)
{
    CPUState *cpu;
    int err = 0;

    if (kvm_enabled()) {
        return kvm_remove_breakpoint(gdbserver_state->c_cpu, addr, len, type);
    }

    switch (type) {
    case GDB_BREAKPOINT_SW:
    case GDB_BREAKPOINT_HW:
        if (s->breakpoints_per_core) {
            err = cpu_breakpoint_remove(s->c_cpu, addr, BP_GDB);
            return err;
        }
        CPU_FOREACH(cpu) {
            err = cpu_breakpoint_remove(cpu, addr, BP_GDB);
            if (err) {
                break;
            }
        }
        return err;
#ifndef CONFIG_USER_ONLY
    case GDB_WATCHPOINT_WRITE:
    case GDB_WATCHPOINT_READ:
    case GDB_WATCHPOINT_ACCESS:
        CPU_FOREACH(cpu) {
            err = cpu_watchpoint_remove(cpu, addr, len,
                                        xlat_gdb_type(cpu, type));
            if (err)
                break;
        }
        return err;
#endif
    default:
        return -ENOSYS;
    }
}

static void gdb_breakpoint_remove_all(void)
{
    CPUState *cpu;

    if (kvm_enabled()) {
        kvm_remove_all_breakpoints(gdbserver_state->c_cpu);
        return;
    }

    CPU_FOREACH(cpu) {
        cpu_breakpoint_remove_all(cpu, BP_GDB);
#ifndef CONFIG_USER_ONLY
        cpu_watchpoint_remove_all(cpu, BP_GDB);
#endif
    }
}

static void gdb_set_cpu_pc(GDBState *s, target_ulong pc)
{
    CPUState *cpu = s->c_cpu;

    cpu_synchronize_state(cpu);
    cpu_set_pc(cpu, pc);
}

static CPUState *find_cpu(GDBState *s, int32_t pid, int32_t thread_id)
{
    GDBCluster *cl;
    CPUState *cpu;

    if (pid <= 0) {
        pid = 1;
    }

    cl = &s->clusters[pid - 1];
    cpu = cl->cpus.first;

    while (cpu) {
        if ((cpu->cpu_index + 1) == thread_id || thread_id <= 0) {
            return cpu;
        }

        if (cpu == cl->cpus.last) {
            break;
        }
        cpu = CPU_NEXT(cpu);
    }

    return NULL;
}

#define MAX_PLIST 8 * 1024
static char *gdb_get_process_list(GDBState *s)
{
    CPUState *cpu;
    char *buf;
    unsigned int i;
    int len;

    buf = g_malloc0(MAX_PLIST);

#undef HEADER
#define HEADER "<?xml version=\"1.0\"?>\n" \
    "<!DOCTYPE target SYSTEM \"osdata.dtd\">\n<osdata type=\"processes\">\n"

    pstrcat(buf, MAX_PLIST, HEADER);
    for (i = 0; i < s->num_clusters; i++) {
        char lbuf[64];
        unsigned int num_cores = 0;

        len = snprintf(lbuf, sizeof(lbuf),
                       "<item>\n <column name=\"pid\">%u</column>\n <column name=\"cores\">",
                       i + 1);
        pstrcat(buf, MAX_PLIST, lbuf);

        cpu = s->clusters[i].cpus.first;
        while (cpu) {
            len = snprintf(lbuf, sizeof(lbuf), "%s%u",
                           num_cores ? "," : "", (cpu->cpu_index + 1));
            pstrcat(buf, MAX_PLIST, lbuf);
            if (cpu == s->clusters[i].cpus.last) {
                break;
            }
            cpu = CPU_NEXT(cpu);
            num_cores++;
        }
        pstrcat(buf, MAX_PLIST, "</column>\n</item>\n");
    }
    len = len;
    pstrcat(buf, MAX_PLIST, "</osdata>");
    return buf;
}

static void gdb_thread_id(const char *p, const char **next_p,
                          int32_t *pid_p, uint32_t *tid_p)
{
    /* We are flexible here and accept extended thread ids even if
     * multiprocess support was not signaled by the peer.
     */
    uint32_t pid = 1, tid;
    bool extended = *p == 'p';

    if (extended) {
        p++;
        pid = strtoull(p, (char **)&p, 16);
        p++;
    }
    tid = strtoull(p, (char **)&p, 16);

    if (pid <= 0) {
        pid = 1;
    }

    if (pid_p) {
        *pid_p = pid;
    }
    if (tid_p) {
        *tid_p = tid;
    }
    if (next_p) {
        *next_p = p;
    }
}

static const char *gdb_gen_thread_id(GDBState *s, uint32_t pid, uint32_t tid)
{
    static char id[64];
    unsigned int pos = 0;

    if (s->multiprocess) {
        pos += snprintf(id, sizeof(id), "p%x.", pid);
    }
    snprintf(id + pos, sizeof(id) - pos, "%x", tid);
    return id;
}

static void gdb_match_supported(GDBState *s, const char *p)
{
    p = strchr(p, ':');
    while (p) {
        p++;
        if (strncmp(p, "multiprocess", 12) == 0) {
            s->multiprocess = true;
        }
        p = strchr(p, ':');
    }
}

static int is_query_packet(const char *p, const char *query, char separator)
{
    unsigned int query_len = strlen(query);

    return strncmp(p, query, query_len) == 0 &&
        (p[query_len] == '\0' || p[query_len] == separator);
}

static int gdb_handle_packet(GDBState *s, const char *line_buf)
{
    GDBCluster *cl = &s->clusters[s->cur_cluster];
    CPUState *cpu;
    CPUClass *cc;
    const char *p;
    int32_t cluster;
    uint32_t thread;
    int ch, reg_size, type, res;
    char buf[MAX_PACKET_LENGTH];
    uint8_t mem_buf[MAX_PACKET_LENGTH];
    uint8_t *registers;
    target_ulong addr, len;


    gdb_debug("command='%s'\n", line_buf);

    p = line_buf;
    ch = *p++;
    switch(ch) {
    case '!':
        put_packet(s, "OK");
        break;
    case '?':
        s->cur_cluster = 0;
        cl = &s->clusters[s->cur_cluster];
        s->c_cpu = cl->cpus.first;
        s->g_cpu = cl->cpus.first;
        /* TODO: Make this return the correct value for user-mode.  */
        snprintf(buf, sizeof(buf), "T%02xthread:%s;", GDB_SIGNAL_TRAP,
                 gdb_gen_thread_id(s, s->cur_cluster + 1,
                                   (s->c_cpu->cpu_index + 1)));
        put_packet(s, buf);
        /* Remove all the breakpoints when this query is issued,
         * because gdb is doing and initial connect and the state
         * should be cleaned up.
         */
        gdb_breakpoint_remove_all();
        break;
    case 'c':
        if (*p != '\0') {
            addr = strtoull(p, (char **)&p, 16);
            gdb_set_cpu_pc(s, addr);
        }
        s->signal = 0;
        gdb_continue(s);
    return RS_IDLE;
    case 'C':
        s->signal = gdb_signal_to_target (strtoul(p, (char **)&p, 16));
        if (s->signal == -1)
            s->signal = 0;
        gdb_continue(s);
        return RS_IDLE;
    case 'v':
        if (strncmp(p, "Cont", 4) == 0) {
            int res_signal, res_thread;

            p += 4;
            if (*p == '?') {
                put_packet(s, "vCont;c;C;s;S");
                break;
            }
            res = 0;
            res_signal = 0;
            res_thread = 0;
            while (*p) {
                int action, signal;

                if (*p++ != ';') {
                    res = 0;
                    break;
                }
                action = *p++;
                signal = 0;
                if (action == 'C' || action == 'S') {
                    signal = gdb_signal_to_target(strtoul(p, (char **)&p, 16));
                    if (signal == -1) {
                        signal = 0;
                    }
                } else if (action != 'c' && action != 's') {
                    res = 0;
                    break;
                }
                thread = 0;
                if (*p == ':') {
                    gdb_thread_id(p + 1, &p, &cluster, &thread);
                }
                action = tolower(action);
                if (res == 0 || (res == 'c' && action == 's')) {
                    res = action;
                    res_signal = signal;
                    res_thread = thread;
                }
            }
            if (res) {
                if (res_thread != -1 && res_thread != 0) {
                    cpu = find_cpu(s, cluster, res_thread);
                    if (cpu == NULL) {
                        put_packet(s, "E22");
                        break;
                    }
                    s->cur_cluster = cluster - 1;
                    cl = &s->clusters[s->cur_cluster];
                    s->c_cpu = cpu;
                }
                if (res == 's') {
                    cpu_single_step(s->c_cpu, sstep_flags);
                }
                s->signal = res_signal;
                gdb_continue(s);
                return RS_IDLE;
            }
            break;
        } else if (strncmp(p, "Attach", 6) == 0) {
            cluster = s->num_clusters + 1;
            p += 6;
            if (*p == ';') {
                p++;
                cluster = strtoull(p, (char **)&p, 16);
            }
            if (cluster <= s->num_clusters) {
                s->cur_cluster = cluster - 1;
                cl = &s->clusters[s->cur_cluster];
                cl->attached = true;
                s->c_cpu = cl->cpus.first;
                s->g_cpu = cl->cpus.first;
                snprintf(buf, sizeof(buf), "T%02xthread:%s;",
                         GDB_SIGNAL_TRAP,
                         gdb_gen_thread_id(s, cluster,
                                           (s->c_cpu->cpu_index + 1)));

                put_packet(s, buf);
            } else {
                put_packet(s, "E22");
            }
            break;
        } else {
            goto unknown_command;
        }
    case 'k':
        /* Kill the target */
        error_report("QEMU: Terminated via GDBstub");
        exit(0);
    case 'D':
        /* Detach packet */
        if (*p == ';') {
            cluster = strtoull(p + 1, (char **)&p, 16);
            s->clusters[cluster - 1].attached = false;
            /* FIXME: Remove all breakpoints for the cluster.  */
            put_packet(s, "OK");
            break;
        }
        gdb_breakpoint_remove_all();
        gdb_syscall_mode = GDB_SYS_DISABLED;
        gdb_continue(s);
        put_packet(s, "OK");
        break;
    case 's':
        if (*p != '\0') {
            addr = strtoull(p, (char **)&p, 16);
            gdb_set_cpu_pc(s, addr);
        }
        cpu_single_step(s->c_cpu, sstep_flags);
        gdb_continue(s);
    return RS_IDLE;
    case 'F':
        {
            target_ulong ret;
            target_ulong err;

            ret = strtoull(p, (char **)&p, 16);
            if (*p == ',') {
                p++;
                err = strtoull(p, (char **)&p, 16);
            } else {
                err = 0;
            }
            if (*p == ',')
                p++;
            type = *p;
            if (s->current_syscall_cb) {
                s->current_syscall_cb(s->c_cpu, ret, err);
                s->current_syscall_cb = NULL;
            }
            if (type == 'C') {
                put_packet(s, "T02");
            } else {
                gdb_continue(s);
            }
        }
        break;
    case 'g':
        cpu_synchronize_state(s->g_cpu);
        len = 0;
        for (addr = 0; addr < s->g_cpu->gdb_num_g_regs; addr++) {
            reg_size = gdb_read_register(s->g_cpu, mem_buf + len, addr);
            len += reg_size;
        }
        memtohex(buf, mem_buf, len);
        put_packet(s, buf);
        break;
    case 'G':
        cpu_synchronize_state(s->g_cpu);
        registers = mem_buf;
        len = strlen(p) / 2;
        hextomem((uint8_t *)registers, p, len);
        for (addr = 0; addr < s->g_cpu->gdb_num_g_regs && len > 0; addr++) {
            reg_size = gdb_write_register(s->g_cpu, registers, addr);
            len -= reg_size;
            registers += reg_size;
        }
        put_packet(s, "OK");
        break;
    case 'm':
        addr = strtoull(p, (char **)&p, 16);
        if (*p == ',')
            p++;
        len = strtoull(p, NULL, 16);

        /* memtohex() doubles the required space */
        if (len > MAX_PACKET_LENGTH / 2) {
            put_packet (s, "E22");
            break;
        }

        if (target_memory_rw_debug(s->g_cpu, addr, mem_buf, len, false) != 0) {
            put_packet (s, "E14");
        } else {
            memtohex(buf, mem_buf, len);
            put_packet(s, buf);
        }
        break;
    case 'M':
        addr = strtoull(p, (char **)&p, 16);
        if (*p == ',')
            p++;
        len = strtoull(p, (char **)&p, 16);
        if (*p == ':')
            p++;

        /* hextomem() reads 2*len bytes */
        if (len > strlen(p) / 2) {
            put_packet (s, "E22");
            break;
        }
        hextomem(mem_buf, p, len);
        if (target_memory_rw_debug(s->g_cpu, addr, mem_buf, len,
                                   true) != 0) {
            put_packet(s, "E14");
        } else {
            put_packet(s, "OK");
        }
        break;
    case 'p':
        /* Older gdb are really dumb, and don't use 'g' if 'p' is avaialable.
           This works, but can be very slow.  Anything new enough to
           understand XML also knows how to use this properly.  */
        if (!gdb_has_xml)
            goto unknown_command;
        addr = strtoull(p, (char **)&p, 16);
        reg_size = gdb_read_register(s->g_cpu, mem_buf, addr);
        if (reg_size) {
            memtohex(buf, mem_buf, reg_size);
            put_packet(s, buf);
        } else {
            put_packet(s, "E14");
        }
        break;
    case 'P':
        if (!gdb_has_xml)
            goto unknown_command;
        addr = strtoull(p, (char **)&p, 16);
        if (*p == '=')
            p++;
        reg_size = strlen(p) / 2;
        hextomem(mem_buf, p, reg_size);
        gdb_write_register(s->g_cpu, mem_buf, addr);
        put_packet(s, "OK");
        break;
    case 'Z':
    case 'z':
        type = strtoul(p, (char **)&p, 16);
        if (*p == ',')
            p++;
        addr = strtoull(p, (char **)&p, 16);
        if (*p == ',')
            p++;
        len = strtoull(p, (char **)&p, 16);
        if (ch == 'Z')
            res = gdb_breakpoint_insert(s, addr, len, type);
        else
            res = gdb_breakpoint_remove(s, addr, len, type);
        if (res >= 0)
             put_packet(s, "OK");
        else if (res == -ENOSYS)
            put_packet(s, "");
        else
            put_packet(s, "E22");
        break;
    case 'H':
        type = *p++;
        gdb_thread_id(p, &p, &cluster, &thread);
        cpu = find_cpu(s, cluster, thread);
        if (cpu == NULL) {
            put_packet(s, "E22");
            break;
        }
        switch (type) {
        case 'c':
            s->cur_cluster = cluster - 1;
            cl = &s->clusters[s->cur_cluster];
            s->c_cpu = cpu;
            put_packet(s, "OK");
            break;
        case 'g':
            s->g_cpu = cpu;
            put_packet(s, "OK");
            break;
        default:
             put_packet(s, "E22");
             break;
        }
        break;
    case 'T':
        gdb_thread_id(p, &p, &cluster, &thread);
        cpu = find_cpu(s, cluster, thread);

        if (cpu != NULL) {
            put_packet(s, "OK");
        } else {
            put_packet(s, "E22");
        }
        break;
    case 'q':
    case 'Q':
        /* parse any 'q' packets here */
        if (!strcmp(p,"qemu.sstepbits")) {
            /* Query Breakpoint bit definitions */
            snprintf(buf, sizeof(buf), "ENABLE=%x,NOIRQ=%x,NOTIMER=%x",
                     SSTEP_ENABLE,
                     SSTEP_NOIRQ,
                     SSTEP_NOTIMER);
            put_packet(s, buf);
            break;
        } else if (is_query_packet(p, "qemu.sstep", '=')) {
            /* Display or change the sstep_flags */
            p += 10;
            if (*p != '=') {
                /* Display current setting */
                snprintf(buf, sizeof(buf), "0x%x", sstep_flags);
                put_packet(s, buf);
                break;
            }
            p++;
            type = strtoul(p, (char **)&p, 16);
            sstep_flags = type;
            put_packet(s, "OK");
            break;
        } else if (strncmp(p,"qemu.bps-per-core",17) == 0) {
            p += 17;
            if (*p != '=') {
                /* Display current setting */
                snprintf(buf, sizeof(buf), "%d", s->breakpoints_per_core);
                put_packet(s, buf);
                break;
            }
            p++;
            s->breakpoints_per_core = strtoul(p, (char **)&p, 0);
            put_packet(s, "OK");
            break;
        } else if (strncmp(p,"qemu.debug-context",18) == 0) {
            unsigned int i = 0, len = 0, found = 0;
            cc = CPU_GET_CLASS(s->g_cpu);

            /* Display or change the context */
            p += 18;
            if (*p != '=') {
                *buf = '\0';
                while (cc->debug_contexts && cc->debug_contexts[i]) {
                    len += snprintf(buf + len, sizeof(buf) - len,
                                    "%s%s", i == 0 ? "" : ",",
                                    cc->debug_contexts[i]);
                    i++;
                }
                put_packet(s, buf);
                break;
            }
            p++;
            if (cc->set_debug_context) {
                while (cc->debug_contexts && cc->debug_contexts[i]) {
                    if (strcmp(p, cc->debug_contexts[i]) == 0) {
                        cc->set_debug_context(s->g_cpu, i);
                        put_packet(s, "OK");
                        found = 1;
                        break;
                    }
                    i++;
                }
            }
            if (!found) {
                put_packet(s, "E22");
            }
            break;
        } else if (strcmp(p,"C") == 0) {
            /* "Current thread" remains vague in the spec, so always return
             *  the first CPU (gdb returns the first thread). */
            snprintf(buf, sizeof(buf), "C%s",
                     gdb_gen_thread_id(s, s->cur_cluster + 1,
                                       (cl->cpus.first->cpu_index + 1)));
            put_packet(s, buf);
            break;
        } else if (strcmp(p,"fThreadInfo") == 0) {
            s->query_cluster = 0;
            s->query_cpu = s->clusters[s->query_cluster].cpus.first;
            goto report_cpuinfo;
        } else if (strcmp(p,"sThreadInfo") == 0) {
        report_cpuinfo:
            if (s->query_cpu) {
                snprintf(buf, sizeof(buf), "m%s",
                         gdb_gen_thread_id(s, s->query_cluster + 1,
                                           (s->query_cpu->cpu_index + 1)));
                put_packet(s, buf);
                if (s->query_cpu == s->clusters[s->query_cluster].cpus.last) {
                    s->query_cluster++;
                    if (s->query_cluster == s->num_clusters) {
                        s->query_cluster = 0;
                    }
                    s->query_cpu = NULL;
                    if (s->clusters[s->query_cluster].attached) {
                        s->query_cpu = s->clusters[s->query_cluster].cpus.first;
                    }
                } else {
                    s->query_cpu = CPU_NEXT(s->query_cpu);
                }
            } else
                put_packet(s, "l");
            break;
        } else if (strncmp(p,"ThreadExtraInfo,", 16) == 0) {
            gdb_thread_id(p + 16, &p, &cluster, &thread);
            cpu = find_cpu(s, cluster, thread);
            if (cpu != NULL) {
                cpu_synchronize_state(cpu);
                if (!cpu->gdb_id) {
                    const char *name = object_get_canonical_path(OBJECT(cpu));

                    len = snprintf((char *)mem_buf, sizeof(mem_buf),
                                   "CPU#%d %s", cpu->cpu_index, name);
                } else {
                    len = snprintf((char *)mem_buf, sizeof(mem_buf),
                                   "%s", cpu->gdb_id);
                }
                len += snprintf((char *)mem_buf + len, sizeof(mem_buf) - len,
                               " [%s]", cpu->halted ? "halted " : "running");
                memtohex(buf, mem_buf, len);
                put_packet(s, buf);
            } else {
                put_packet(s, "E22");
            }
            break;
        }
#ifdef CONFIG_USER_ONLY
        else if (strcmp(p, "Offsets") == 0) {
            TaskState *ts = s->c_cpu->opaque;

            snprintf(buf, sizeof(buf),
                     "Text=" TARGET_ABI_FMT_lx ";Data=" TARGET_ABI_FMT_lx
                     ";Bss=" TARGET_ABI_FMT_lx,
                     ts->info->code_offset,
                     ts->info->data_offset,
                     ts->info->data_offset);
            put_packet(s, buf);
            break;
        }
#else /* !CONFIG_USER_ONLY */
        else if (strncmp(p, "Rcmd,", 5) == 0) {
            int len = strlen(p + 5);

            if ((len % 2) != 0) {
                put_packet(s, "E01");
                break;
            }
            len = len / 2;
            hextomem(mem_buf, p + 5, len);
            mem_buf[len++] = 0;
            if (strncmp((char *) mem_buf, "gdbmon.", 7) == 0) {
                /* Display or change the sstep_flags */
                p = (char *) &mem_buf[7];
                gdb_monitor(s, p);
                break;
            }
#ifndef CONFIG_USER_ONLY
            qemu_chr_be_write(s->mon_chr, mem_buf, len);
            put_packet(s, "OK");
#endif
            break;
        }
        if (strncmp(p, "Attached", 8) == 0) {
            put_packet(s, "1");
            break;
        }
#endif /* !CONFIG_USER_ONLY */
        if (is_query_packet(p, "Supported", ':')) {
            gdb_match_supported(s, p + 9);

            snprintf(buf, sizeof(buf), "PacketSize=%x", MAX_PACKET_LENGTH);
            cc = CPU_GET_CLASS(cl->cpus.first);
            if (cc->gdb_core_xml_file != NULL) {
                pstrcat(buf, sizeof(buf), ";qXfer:features:read+");
            }
            pstrcat(buf, sizeof(buf), ";qXfer:osdata:read+");
            pstrcat(buf, sizeof(buf), ";multiprocess+");
            put_packet(s, buf);
            break;
        }
        if (strncmp(p, "Xfer:features:read:", 19) == 0) {
            const char *xml;
            target_ulong total_len;

            cc = CPU_GET_CLASS(s->g_cpu);
            if (cc->gdb_core_xml_file == NULL) {
                goto unknown_command;
            }

            gdb_has_xml = true;
            p += 19;
            xml = get_feature_xml(p, &p, cc, s->g_cpu);
            if (!xml) {
                snprintf(buf, sizeof(buf), "E00");
                put_packet(s, buf);
                break;
            }

            if (*p == ':')
                p++;
            addr = strtoul(p, (char **)&p, 16);
            if (*p == ',')
                p++;
            len = strtoul(p, (char **)&p, 16);

            total_len = strlen(xml);
            if (addr > total_len) {
                snprintf(buf, sizeof(buf), "E00");
                put_packet(s, buf);
                break;
            }
            if (len > (MAX_PACKET_LENGTH - 5) / 2)
                len = (MAX_PACKET_LENGTH - 5) / 2;
            if (len < total_len - addr) {
                buf[0] = 'm';
                len = memtox(buf + 1, xml + addr, len);
            } else {
                buf[0] = 'l';
                len = memtox(buf + 1, xml + addr, total_len - addr);
            }
            put_packet_binary(s, buf, len + 1);
            break;
        }
        /* FIXME: Merge with the XML handling to avoid code duplication.  */
        if (strncmp(p, "Xfer:osdata:read:processes:", 27) == 0) {
            const char *plist;
            target_ulong total_len;

            plist = gdb_get_process_list(s);

            p += 27;
            if (*p == ':')
                p++;
            addr = strtoul(p, (char **)&p, 16);
            if (*p == ',')
                p++;
            len = strtoul(p, (char **)&p, 16);

            total_len = strlen(plist);
            if (addr > total_len) {
                snprintf(buf, sizeof(buf), "E00");
                put_packet(s, buf);
                break;
            }

            if (len > (MAX_PACKET_LENGTH - 5) / 2)
                len = (MAX_PACKET_LENGTH - 5) / 2;
            if (len < total_len - addr) {
                buf[0] = 'm';
                len = memtox(buf + 1, plist + addr, len);
            } else {
                buf[0] = 'l';
                len = memtox(buf + 1, plist + addr, total_len - addr);
            }
            put_packet_binary(s, buf, len + 1);
            g_free((void *) plist);
            break;
        }
        /* Unrecognised 'q' command.  */
        goto unknown_command;

    default:
    unknown_command:
        /* put empty packet */
        buf[0] = '\0';
        put_packet(s, buf);
        break;
    }
    return RS_IDLE;
}

void gdb_set_stop_cpu(CPUState *cpu)
{
    gdbserver_state->c_cpu = cpu;
    gdbserver_state->g_cpu = cpu;
}

#ifndef CONFIG_USER_ONLY
static void gdb_output(GDBState *s, const char *msg, int len)
{
    char buf[MAX_PACKET_LENGTH];

    buf[0] = 'O';
    if (len > (MAX_PACKET_LENGTH/2) - 1) {
        len = (MAX_PACKET_LENGTH/2) - 1;
    }
    memtohex(buf + 1, (uint8_t *)msg, len);
    put_packet(s, buf);
}

static void gdb_vm_state_change(void *opaque, int running, RunState state)
{
    GDBState *s = gdbserver_state;
    CPUState *cpu = s->c_cpu;
    char buf[256];
    const char *type;
    int ret;

    if (running || s->state == RS_INACTIVE) {
        return;
    }
    /* Is there a GDB syscall waiting to be sent?  */
    if (s->current_syscall_cb) {
        put_packet(s, s->syscall_buf);
        return;
    }
    switch (state) {
    case RUN_STATE_DEBUG:
        if (cpu->watchpoint_hit) {
            switch (cpu->watchpoint_hit->flags & BP_MEM_ACCESS) {
            case BP_MEM_READ:
                type = "r";
                break;
            case BP_MEM_ACCESS:
                type = "a";
                break;
            default:
                type = "";
                break;
            }
            snprintf(buf, sizeof(buf),
                     "T%02xthread:%s;%swatch:" TARGET_FMT_lx ";",
                     GDB_SIGNAL_TRAP,
                     gdb_gen_thread_id(s, s->cur_cluster + 1,
                                       (cpu->cpu_index + 1)),
                     type,
                     (target_ulong)cpu->watchpoint_hit->vaddr);
            cpu->watchpoint_hit = NULL;
            goto send_packet;
        }
        tb_flush(cpu);
        ret = GDB_SIGNAL_TRAP;
        break;
    case RUN_STATE_PAUSED:
        ret = GDB_SIGNAL_INT;
        break;
    case RUN_STATE_SHUTDOWN:
        ret = GDB_SIGNAL_QUIT;
        break;
    case RUN_STATE_IO_ERROR:
        ret = GDB_SIGNAL_IO;
        break;
    case RUN_STATE_WATCHDOG:
        ret = GDB_SIGNAL_ALRM;
        break;
    case RUN_STATE_INTERNAL_ERROR:
        ret = GDB_SIGNAL_ABRT;
        break;
    case RUN_STATE_SAVE_VM:
    case RUN_STATE_RESTORE_VM:
        return;
    case RUN_STATE_FINISH_MIGRATE:
        ret = GDB_SIGNAL_XCPU;
        break;
    default:
        ret = GDB_SIGNAL_UNKNOWN;
        break;
    }
    gdb_set_stop_cpu(cpu);
    snprintf(buf, sizeof(buf), "T%02xthread:%s;", ret,
             gdb_gen_thread_id(s, s->cur_cluster + 1, (cpu->cpu_index + 1)));

send_packet:
    put_packet(s, buf);

    /* disable single step if it was enabled */
    cpu_single_step(cpu, 0);
}
#endif

/* Send a gdb syscall request.
   This accepts limited printf-style format specifiers, specifically:
    %x  - target_ulong argument printed in hex.
    %lx - 64-bit argument printed in hex.
    %s  - string pointer (target_ulong) and length (int) pair.  */
void gdb_do_syscallv(gdb_syscall_complete_cb cb, const char *fmt, va_list va)
{
    char *p;
    char *p_end;
    target_ulong addr;
    uint64_t i64;
    GDBState *s;

    s = gdbserver_state;
    if (!s)
        return;
    s->current_syscall_cb = cb;
#ifndef CONFIG_USER_ONLY
    vm_stop(RUN_STATE_DEBUG);
#endif
    p = s->syscall_buf;
    p_end = &s->syscall_buf[sizeof(s->syscall_buf)];
    *(p++) = 'F';
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt++) {
            case 'x':
                addr = va_arg(va, target_ulong);
                p += snprintf(p, p_end - p, TARGET_FMT_lx, addr);
                break;
            case 'l':
                if (*(fmt++) != 'x')
                    goto bad_format;
                i64 = va_arg(va, uint64_t);
                p += snprintf(p, p_end - p, "%" PRIx64, i64);
                break;
            case 's':
                addr = va_arg(va, target_ulong);
                p += snprintf(p, p_end - p, TARGET_FMT_lx "/%x",
                              addr, va_arg(va, int));
                break;
            default:
            bad_format:
                error_report("gdbstub: Bad syscall format string '%s'",
                             fmt - 1);
                break;
            }
        } else {
            *(p++) = *(fmt++);
        }
    }
    *p = 0;
#ifdef CONFIG_USER_ONLY
    put_packet(s, s->syscall_buf);
    gdb_handlesig(s->c_cpu, 0);
#else
    /* In this case wait to send the syscall packet until notification that
       the CPU has stopped.  This must be done because if the packet is sent
       now the reply from the syscall request could be received while the CPU
       is still in the running state, which can cause packets to be dropped
       and state transition 'T' packets to be sent while the syscall is still
       being processed.  */
    qemu_cpu_kick(s->c_cpu);
#endif
}

void gdb_do_syscall(gdb_syscall_complete_cb cb, const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    gdb_do_syscallv(cb, fmt, va);
    va_end(va);
}

static void gdb_read_byte(GDBState *s, int ch)
{
    int i, csum;
    uint8_t reply;

#ifndef CONFIG_USER_ONLY
    if (s->last_packet_len) {
        /* Waiting for a response to the last packet.  If we see the start
           of a new command then abandon the previous response.  */
        if (ch == '-') {
            gdb_debug("Got NACK, retransmitting\n");
            put_buffer(s, (uint8_t *)s->last_packet, s->last_packet_len);
        } else if (ch == '+') {
            gdb_debug("Got ACK\n");
        } else {
            gdb_debug("Got '%c' when expecting ACK/NACK\n", ch);
        }

        if (ch == '+' || ch == '$')
            s->last_packet_len = 0;
        if (ch != '$')
            return;
    }
    if (runstate_is_running()) {
        /* when the CPU is running, we cannot do anything except stop
           it when receiving a char */
        vm_stop(RUN_STATE_PAUSED);
    } else
#endif
    {
        switch(s->state) {
        case RS_IDLE:
            if (ch == '$') {
                /* start of command packet */
                s->line_buf_index = 0;
                s->line_sum = 0;
                s->state = RS_GETLINE;
            } else {
                gdb_debug("received garbage between packets: 0x%x\n", ch);
            }
            break;
        case RS_GETLINE:
            if (ch == '}') {
                /* start escape sequence */
                s->state = RS_GETLINE_ESC;
                s->line_sum += ch;
            } else if (ch == '*') {
                /* start run length encoding sequence */
                s->state = RS_GETLINE_RLE;
                s->line_sum += ch;
            } else if (ch == '#') {
                /* end of command, start of checksum*/
                s->state = RS_CHKSUM1;
            } else if (s->line_buf_index >= sizeof(s->line_buf) - 1) {
                gdb_debug("command buffer overrun, dropping command\n");
                s->state = RS_IDLE;
            } else {
                /* unescaped command character */
                s->line_buf[s->line_buf_index++] = ch;
                s->line_sum += ch;
            }
            break;
        case RS_GETLINE_ESC:
            if (ch == '#') {
                /* unexpected end of command in escape sequence */
                s->state = RS_CHKSUM1;
            } else if (s->line_buf_index >= sizeof(s->line_buf) - 1) {
                /* command buffer overrun */
                gdb_debug("command buffer overrun, dropping command\n");
                s->state = RS_IDLE;
            } else {
                /* parse escaped character and leave escape state */
                s->line_buf[s->line_buf_index++] = ch ^ 0x20;
                s->line_sum += ch;
                s->state = RS_GETLINE;
            }
            break;
        case RS_GETLINE_RLE:
            if (ch < ' ') {
                /* invalid RLE count encoding */
                gdb_debug("got invalid RLE count: 0x%x\n", ch);
                s->state = RS_GETLINE;
            } else {
                /* decode repeat length */
                int repeat = (unsigned char)ch - ' ' + 3;
                if (s->line_buf_index + repeat >= sizeof(s->line_buf) - 1) {
                    /* that many repeats would overrun the command buffer */
                    gdb_debug("command buffer overrun, dropping command\n");
                    s->state = RS_IDLE;
                } else if (s->line_buf_index < 1) {
                    /* got a repeat but we have nothing to repeat */
                    gdb_debug("got invalid RLE sequence\n");
                    s->state = RS_GETLINE;
                } else {
                    /* repeat the last character */
                    memset(s->line_buf + s->line_buf_index,
                           s->line_buf[s->line_buf_index - 1], repeat);
                    s->line_buf_index += repeat;
                    s->line_sum += ch;
                    s->state = RS_GETLINE;
                }
            }
            break;
        case RS_CHKSUM1:
            /* get high hex digit of checksum */
            if (!isxdigit(ch)) {
                gdb_debug("got invalid command checksum digit\n");
                s->state = RS_GETLINE;
                break;
            }
            s->line_buf[s->line_buf_index] = '\0';
            s->line_csum = fromhex(ch) << 4;
            s->state = RS_CHKSUM2;
            break;
        case RS_CHKSUM2:
            /* get low hex digit of checksum */
            if (!isxdigit(ch)) {
                gdb_debug("got invalid command checksum digit\n");
                s->state = RS_GETLINE;
                break;
            }
            s->line_csum |= fromhex(ch);
            csum = 0;
            for (i = 0; i < s->line_buf_index; i++) {
                csum += s->line_buf[i];
            }
            if (s->line_csum != (csum & 0xff)) {
                gdb_debug("got command packet with incorrect checksum\n");
                /* send NAK reply */
                reply = '-';
                put_buffer(s, &reply, 1);
                s->state = RS_IDLE;
            } else {
                /* send ACK reply */
                reply = '+';
                put_buffer(s, &reply, 1);
                s->state = gdb_handle_packet(s, s->line_buf);
#ifdef CONFIG_REMOTE_PORT
                bool tw_en = rp_time_warp_enable(false);
                rp_time_warp_enable(tw_en);
#endif
            }
            break;
        default:
            abort();
        }
    }
}

/* Tell the remote gdb that the process has exited.  */
void gdb_exit(CPUArchState *env, int code)
{
  GDBState *s;
  char buf[4];

  s = gdbserver_state;
  if (!s) {
      return;
  }
#ifdef CONFIG_USER_ONLY
  if (gdbserver_fd < 0 || s->fd < 0) {
      return;
  }
#endif

  snprintf(buf, sizeof(buf), "W%02x", (uint8_t)code);
  put_packet(s, buf);

#ifndef CONFIG_USER_ONLY
  qemu_chr_fe_deinit(&s->chr, true);
#endif
}

#ifdef CONFIG_USER_ONLY
int
gdb_handlesig(CPUState *cpu, int sig)
{
    GDBState *s;
    char buf[256];
    int n;

    s = gdbserver_state;
    if (gdbserver_fd < 0 || s->fd < 0) {
        return sig;
    }

    /* disable single step if it was enabled */
    cpu_single_step(cpu, 0);
    tb_flush(cpu);

    if (sig != 0) {
        snprintf(buf, sizeof(buf), "S%02x", target_signal_to_gdb(sig));
        put_packet(s, buf);
    }
    /* put_packet() might have detected that the peer terminated the
       connection.  */
    if (s->fd < 0) {
        return sig;
    }

    sig = 0;
    s->state = RS_IDLE;
    s->running_state = 0;
    while (s->running_state == 0) {
        n = read(s->fd, buf, 256);
        if (n > 0) {
            int i;

            for (i = 0; i < n; i++) {
                gdb_read_byte(s, buf[i]);
            }
        } else {
            /* XXX: Connection closed.  Should probably wait for another
               connection before continuing.  */
            if (n == 0) {
                close(s->fd);
            }
            s->fd = -1;
            return sig;
        }
    }
    sig = s->signal;
    s->signal = 0;
    return sig;
}

/* Tell the remote gdb that the process has exited due to SIG.  */
void gdb_signalled(CPUArchState *env, int sig)
{
    GDBState *s;
    char buf[4];

    s = gdbserver_state;
    if (gdbserver_fd < 0 || s->fd < 0) {
        return;
    }

    snprintf(buf, sizeof(buf), "X%02x", target_signal_to_gdb(sig));
    put_packet(s, buf);
}

static void gdb_accept(void)
{
    GDBState *s;
    struct sockaddr_in sockaddr;
    socklen_t len;
    int fd;

    for(;;) {
        len = sizeof(sockaddr);
        fd = accept(gdbserver_fd, (struct sockaddr *)&sockaddr, &len);
        if (fd < 0 && errno != EINTR) {
            perror("accept");
            return;
        } else if (fd >= 0) {
#ifndef _WIN32
            fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
            break;
        }
    }

    /* set short latency */
    socket_set_nodelay(fd);

    s = g_malloc0(sizeof(GDBState));
    s->c_cpu = first_cpu;
    s->g_cpu = first_cpu;
    s->fd = fd;
    gdb_has_xml = false;

    gdbserver_state = s;
}

static int gdbserver_open(int port)
{
    struct sockaddr_in sockaddr;
    int fd, ret;

    fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
#ifndef _WIN32
    fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif

    socket_set_fast_reuse(fd);

    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(port);
    sockaddr.sin_addr.s_addr = 0;
    ret = bind(fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
    if (ret < 0) {
        perror("bind");
        close(fd);
        return -1;
    }
    ret = listen(fd, 1);
    if (ret < 0) {
        perror("listen");
        close(fd);
        return -1;
    }
    return fd;
}

int gdbserver_start(int port)
{
    gdbserver_fd = gdbserver_open(port);
    if (gdbserver_fd < 0)
        return -1;
    /* accept connections */
    gdb_accept();
    return 0;
}

/* Disable gdb stub for child processes.  */
void gdbserver_fork(CPUState *cpu)
{
    GDBState *s = gdbserver_state;

    if (gdbserver_fd < 0 || s->fd < 0) {
        return;
    }
    close(s->fd);
    s->fd = -1;
    cpu_breakpoint_remove_all(cpu, BP_GDB);
    cpu_watchpoint_remove_all(cpu, BP_GDB);
}
#else
static void gdb_autosplit_cpus(GDBState *s)
{
    /* FIXME: this should be done explicitely from a QOM CLUSTER
     * container. Maybe autocreated from dts files.
     * In the meantime, follow a simple logic. Consecutive cores
     * of the same kind, form a cluster.
     */
    CPUState *cpu = first_cpu;
    CPUState *cpu_prev = NULL;

    assert(s->clusters == NULL);
    assert(s->num_clusters == 0);

    while (cpu) {
        if (!cpu_prev || (CPU_GET_CLASS(cpu) != CPU_GET_CLASS(cpu_prev))) {
            /* New cluster.  */
            s->clusters = g_renew(typeof(s->clusters[0]), s->clusters,
                                  s->num_clusters + 1);
            if (s->num_clusters) {
                s->clusters[s->num_clusters - 1].cpus.last = cpu_prev;
            }
            s->clusters[s->num_clusters].attached = false;
            s->clusters[s->num_clusters].cpus.first = cpu;
            s->num_clusters++;
        }
        cpu_prev = cpu;
        cpu = CPU_NEXT(cpu);
    }
    s->clusters[s->num_clusters - 1].cpus.last = cpu_prev;

#ifdef DEBUG_GDB
    {
        unsigned int i;

        for (i = 0; i < s->num_clusters; i++) {
            cpu = s->clusters[i].cpus.first;
            while (cpu) {
                CPUClass *cc = CPU_GET_CLASS(cpu);
                const char *name = object_get_canonical_path(OBJECT(cpu));
                qemu_log("Cluster%d: CPU%d %s xml=%s\n", i, cpu->cpu_index,
                         name, cc->gdb_core_xml_file);
                if (cpu == s->clusters[i].cpus.last) {
                    break;
                }
                cpu = CPU_NEXT(cpu);
            }
        }
    }
#endif
}

static int gdb_chr_can_receive(void *opaque)
{
  /* We can handle an arbitrarily large amount of data.
   Pick the maximum packet size, which is as good as anything.  */
  return MAX_PACKET_LENGTH;
}

static void gdb_chr_receive(void *opaque, const uint8_t *buf, int size)
{
    int i;

    for (i = 0; i < size; i++) {
        gdb_read_byte(gdbserver_state, buf[i]);
    }
}

static void gdb_chr_event(void *opaque, int event)
{
    switch (event) {
    case CHR_EVENT_OPENED:
        vm_stop(RUN_STATE_PAUSED);
        gdb_has_xml = false;
        break;
    default:
        break;
    }
}

static void gdb_monitor_output(GDBState *s, const char *msg, int len)
{
    char buf[MAX_PACKET_LENGTH];

    buf[0] = 'O';
    if (len > (MAX_PACKET_LENGTH/2) - 1)
        len = (MAX_PACKET_LENGTH/2) - 1;
    memtohex(buf + 1, (uint8_t *)msg, len);
    put_packet(s, buf);
}

static int gdb_monitor_write(Chardev *chr, const uint8_t *buf, int len)
{
    const char *p = (const char *)buf;
    int max_sz;

    max_sz = (sizeof(gdbserver_state->last_packet) - 2) / 2;
    for (;;) {
        if (len <= max_sz) {
            gdb_monitor_output(gdbserver_state, p, len);
            break;
        }
        gdb_monitor_output(gdbserver_state, p, max_sz);
        p += max_sz;
        len -= max_sz;
    }
    return len;
}

#ifndef _WIN32
static void gdb_sigterm_handler(int signal)
{
    if (runstate_is_running()) {
        vm_stop(RUN_STATE_PAUSED);
    }
}
#endif

static void gdb_monitor_open(Chardev *chr, ChardevBackend *backend,
                             bool *be_opened, Error **errp)
{
    *be_opened = false;
}

static void char_gdb_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->internal = true;
    cc->open = gdb_monitor_open;
    cc->chr_write = gdb_monitor_write;
}

#define TYPE_CHARDEV_GDB "chardev-gdb"

static const TypeInfo char_gdb_type_info = {
    .name = TYPE_CHARDEV_GDB,
    .parent = TYPE_CHARDEV,
    .class_init = char_gdb_class_init,
};

int gdbserver_start(const char *device)
{
    GDBState *s;
    char gdbstub_device_name[128];
    Chardev *chr = NULL;
    Chardev *mon_chr;

    if (!first_cpu) {
        error_report("gdbstub: meaningless to attach gdb to a "
                     "machine without any CPU.");
        return -1;
    }

    if (!device)
        return -1;
    if (strcmp(device, "none") != 0) {
        if (strstart(device, "tcp:", NULL)) {
            /* enforce required TCP attributes */
            snprintf(gdbstub_device_name, sizeof(gdbstub_device_name),
                     "%s,nowait,nodelay,server", device);
            device = gdbstub_device_name;
        }
#ifndef _WIN32
        else if (strcmp(device, "stdio") == 0) {
            struct sigaction act;

            memset(&act, 0, sizeof(act));
            act.sa_handler = gdb_sigterm_handler;
            sigaction(SIGINT, &act, NULL);
        }
#endif
        chr = qemu_chr_new_noreplay("gdb", device);
        if (!chr)
            return -1;
    }

    s = gdbserver_state;
    if (!s) {
        s = g_malloc0(sizeof(GDBState));
        gdbserver_state = s;

        qemu_add_vm_change_state_handler(gdb_vm_state_change, NULL);

        /* Initialize a monitor terminal for gdb */
        mon_chr = qemu_chardev_new(NULL, TYPE_CHARDEV_GDB,
                                   NULL, &error_abort);
        monitor_init(mon_chr, 0);
    } else {
        qemu_chr_fe_deinit(&s->chr, true);
        mon_chr = s->mon_chr;
        memset(s, 0, sizeof(GDBState));
        s->mon_chr = mon_chr;
    }
    gdb_autosplit_cpus(s);
    s->c_cpu = first_cpu;
    s->g_cpu = first_cpu;
    if (chr) {
        qemu_chr_fe_init(&s->chr, chr, &error_abort);
        qemu_chr_fe_set_handlers(&s->chr, gdb_chr_can_receive, gdb_chr_receive,
                                 gdb_chr_event, NULL, NULL, NULL, true);
    }
    s->state = chr ? RS_IDLE : RS_INACTIVE;
    s->mon_chr = mon_chr;
    s->current_syscall_cb = NULL;

    return 0;
}

static void register_types(void)
{
    type_register_static(&char_gdb_type_info);
}

type_init(register_types);
#endif
