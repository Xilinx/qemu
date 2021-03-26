/*
 * QEMU remote attach
 *
 * Copyright (c) 2013 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 * This code is licensed under the GNU GPL.
 */

#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "sysemu/dma.h"
#include "chardev/char.h"
#include "sysemu/cpus.h"
#include "hw/sysbus.h"
#include "hw/hw.h"
#include "hw/ptimer.h"
#include "qemu/sockets.h"
#include "qemu/thread.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

#ifndef _WIN32
#include <sys/mman.h>
#endif

#include "hw/fdt_generic_util.h"
#include "hw/remote-port-proto.h"
#include "hw/remote-port-device.h"
#include "hw/remote-port.h"

#define D(x)
#define SYNCD(x)

#ifndef REMOTE_PORT_ERR_DEBUG
#define REMOTE_PORT_DEBUG_LEVEL 0
#else
#define REMOTE_PORT_DEBUG_LEVEL 1
#endif

#define DB_PRINT_L(level, ...) do { \
    if (REMOTE_PORT_DEBUG_LEVEL > level) { \
        fprintf(stderr,  ": %s: ", __func__); \
        fprintf(stderr, ## __VA_ARGS__); \
    } \
} while (0);

#define REMOTE_PORT_CLASS(klass)    \
     OBJECT_CLASS_CHECK(RemotePortClass, (klass), TYPE_REMOTE_PORT)

static bool time_warp_enable = true;

bool rp_time_warp_enable(bool en)
{
    bool ret = time_warp_enable;

    time_warp_enable = en;
    return ret;
}

static void rp_event_read(void *opaque);
static void sync_timer_hit(void *opaque);
static void syncresp_timer_hit(void *opaque);

static void rp_pkt_dump(const char *prefix, const char *buf, size_t len)
{
    qemu_hexdump(buf, stdout, prefix, len);
}

uint32_t rp_new_id(RemotePort *s)
{
    return atomic_fetch_inc(&s->current_id);
}

void rp_rsp_mutex_lock(RemotePort *s)
{
    qemu_mutex_lock(&s->rsp_mutex);
}

void rp_rsp_mutex_unlock(RemotePort *s)
{
    qemu_mutex_unlock(&s->rsp_mutex);
}

int64_t rp_normalized_vmclk(RemotePort *s)
{
    int64_t clk;

    clk = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    clk -= s->peer.clk_base;
    return clk;
}

static void rp_restart_sync_timer_bare(RemotePort *s)
{
    if (!s->do_sync) {
        return;
    }

    if (s->sync.quantum) {
        ptimer_stop(s->sync.ptimer);
        ptimer_set_limit(s->sync.ptimer, s->sync.quantum, 1);
        ptimer_run(s->sync.ptimer, 1);
    }
}

void rp_restart_sync_timer(RemotePort *s)
{
    if (s->doing_sync) {
        return;
    }
    ptimer_transaction_begin(s->sync.ptimer);
    rp_restart_sync_timer_bare(s);
    ptimer_transaction_commit(s->sync.ptimer);
}

static void rp_fatal_error(RemotePort *s, const char *reason)
{
    int64_t clk = rp_normalized_vmclk(s);
    error_report("%s: %s clk=%" PRIu64 " ns\n", s->prefix, reason, clk);
    exit(EXIT_FAILURE);
}

static ssize_t rp_recv(RemotePort *s, void *buf, size_t count)
{
    ssize_t r;

    r = qemu_chr_fe_read_all(&s->chr, buf, count);
    if (r <= 0) {
        return r;
    }
    if (r != count) {
        error_report("%s: Bad read, expected %zd but got %zd\n",
                     s->prefix, count, r);
        rp_fatal_error(s, "Bad read");
    }

    return r;
}

ssize_t rp_write(RemotePort *s, const void *buf, size_t count)
{
    ssize_t r;

    qemu_mutex_lock(&s->write_mutex);
    r = qemu_chr_fe_write_all(&s->chr, buf, count);
    qemu_mutex_unlock(&s->write_mutex);
    assert(r == count);
    if (r <= 0) {
        error_report("%s: Disconnected r=%zd buf=%p count=%zd\n",
                     s->prefix, r, buf, count);
        rp_fatal_error(s, "Bad write");
    }
    return r;
}

static unsigned int rp_has_work(RemotePort *s)
{
    unsigned int work = s->rx_queue.wpos - s->rx_queue.rpos;
    return work;
}

/* Response handling.  */
RemotePortRespSlot *rp_dev_wait_resp(RemotePort *s, uint32_t dev, uint32_t id)
{
    int i;

    assert(s->devs[dev]);

    /* Find a free slot.  */
    for (i = 0; i < ARRAY_SIZE(s->dev_state[dev].rsp_queue); i++) {
        if (s->dev_state[dev].rsp_queue[i].used == false) {
            break;
        }
    }

    if (s->dev_state[dev].rsp_queue[i].used == true) {
        error_report("Number of outstanding transactions exceeded! %d",
                      RP_MAX_OUTSTANDING_TRANSACTIONS);
        rp_fatal_error(s, "Internal error");
    }

    /* Got a slot, fill it in.  */
    s->dev_state[dev].rsp_queue[i].id = id;
    s->dev_state[dev].rsp_queue[i].valid = false;
    s->dev_state[dev].rsp_queue[i].used = true;

    while (!s->dev_state[dev].rsp_queue[i].valid) {
        rp_rsp_mutex_unlock(s);
        rp_event_read(s);
        rp_rsp_mutex_lock(s);
        if (s->dev_state[dev].rsp_queue[i].valid) {
            break;
        }
        if (!rp_has_work(s)) {
            qemu_cond_wait(&s->progress_cond, &s->rsp_mutex);
        }
    }
    return &s->dev_state[dev].rsp_queue[i];
}

RemotePortDynPkt rp_wait_resp(RemotePort *s)
{
    while (!rp_dpkt_is_valid(&s->rspqueue)) {
        rp_rsp_mutex_unlock(s);
        rp_event_read(s);
        rp_rsp_mutex_lock(s);
        /* Need to recheck the condition with the rsp lock taken.  */
        if (rp_dpkt_is_valid(&s->rspqueue)) {
            break;
        }
        D(qemu_log("%s: wait for progress\n", __func__));
        if (!rp_has_work(s)) {
            qemu_cond_wait(&s->progress_cond, &s->rsp_mutex);
        }
    }
    return s->rspqueue;
}

static void rp_cmd_hello(RemotePort *s, struct rp_pkt *pkt)
{
    s->peer.version = pkt->hello.version;
    if (pkt->hello.version.major != RP_VERSION_MAJOR) {
        error_report("remote-port version missmatch remote=%d.%d local=%d.%d\n",
                      pkt->hello.version.major, pkt->hello.version.minor,
                      RP_VERSION_MAJOR, RP_VERSION_MINOR);
        rp_fatal_error(s, "Bad version");
    }

    if (pkt->hello.caps.len) {
        void *caps = (char *) pkt + pkt->hello.caps.offset;

        rp_process_caps(&s->peer, caps, pkt->hello.caps.len);
    }
}

static void rp_cmd_sync(RemotePort *s, struct rp_pkt *pkt)
{
    size_t enclen;
    int64_t clk;
    int64_t diff;

    assert(!(pkt->hdr.flags & RP_PKT_FLAGS_response));

    clk = rp_normalized_vmclk(s);
    diff = pkt->sync.timestamp - clk;

    enclen = rp_encode_sync_resp(pkt->hdr.id, pkt->hdr.dev, &s->sync.rsp.sync,
                                 pkt->sync.timestamp);
    assert(enclen == sizeof s->sync.rsp.sync);

    /* We have temporarily disabled blocking syncs into QEMU.  */
    if (diff <= 0LL || true) {
        /* We are already a head of time. Respond and issue a sync.  */
        SYNCD(printf("%s: sync resp %lu\n", s->prefix, pkt->sync.timestamp));
        rp_write(s, (void *) &s->sync.rsp, enclen);
        return;
    }

    SYNCD(printf("%s: delayed sync resp - start diff=%ld (ts=%lu clk=%lu)\n",
          s->prefix, pkt->sync.timestamp - clk, pkt->sync.timestamp, clk));

    ptimer_transaction_begin(s->sync.ptimer_resp);
    ptimer_set_limit(s->sync.ptimer_resp, diff, 1);
    ptimer_run(s->sync.ptimer_resp, 1);
    s->sync.resp_timer_enabled = true;
    ptimer_transaction_commit(s->sync.ptimer_resp);
}

static void rp_say_hello(RemotePort *s)
{
    struct rp_pkt_hello pkt;
    uint32_t caps[] = {
        CAP_BUSACCESS_EXT_BASE,
        CAP_BUSACCESS_EXT_BYTE_EN,
        CAP_WIRE_POSTED_UPDATES,
        CAP_ATS,
    };
    size_t len;

    len = rp_encode_hello_caps(s->current_id++, 0, &pkt, RP_VERSION_MAJOR,
                               RP_VERSION_MINOR,
                               caps, caps, sizeof caps / sizeof caps[0]);
    rp_write(s, (void *) &pkt, len);

    if (sizeof caps) {
        rp_write(s, caps, sizeof caps);
    }
}

static void rp_say_sync(RemotePort *s, int64_t clk)
{
    struct rp_pkt_sync pkt;
    size_t len;

    len = rp_encode_sync(s->current_id++, 0, &pkt, clk);
    rp_write(s, (void *) &pkt, len);
}

static void syncresp_timer_hit(void *opaque)
{
    RemotePort *s = REMOTE_PORT(opaque);

    s->sync.resp_timer_enabled = false;
    SYNCD(printf("%s: delayed sync response - send\n", s->prefix));
    rp_write(s, (void *) &s->sync.rsp, sizeof s->sync.rsp.sync);
    memset(&s->sync.rsp, 0, sizeof s->sync.rsp);
}

static void sync_timer_hit(void *opaque)
{
    RemotePort *s = REMOTE_PORT(opaque);
    int64_t clk;
    RemotePortDynPkt rsp;

    clk = rp_normalized_vmclk(s);
    if (s->sync.resp_timer_enabled) {
        SYNCD(printf("%s: sync while delaying a resp! clk=%lu\n",
                     s->prefix, clk));
        s->sync.need_sync = true;
        rp_restart_sync_timer_bare(s);
        return;
    }

    /* Sync.  */
    s->doing_sync = true;
    s->sync.need_sync = false;
    qemu_mutex_lock(&s->rsp_mutex);
    /* Send the sync.  */
    rp_say_sync(s, clk);

    SYNCD(printf("%s: syncing wait for resp %lu\n", s->prefix, clk));
    rsp = rp_wait_resp(s);
    rp_dpkt_invalidate(&rsp);
    qemu_mutex_unlock(&s->rsp_mutex);
    s->doing_sync = false;

    rp_restart_sync_timer_bare(s);
}

static char *rp_sanitize_prefix(RemotePort *s)
{
    char *sanitized_name;
    char *c;

    sanitized_name = g_strdup(s->prefix);
    for (c = sanitized_name; *c != '\0'; c++) {
        if (*c == '/')
            *c = '_';
    }
    return sanitized_name;
}

static char *rp_autocreate_chardesc(RemotePort *s, bool server)
{
    char *prefix;
    char *chardesc;
    int r;

    prefix = rp_sanitize_prefix(s);
    r = asprintf(&chardesc, "unix:%s/qemu-rport-%s%s",
                 machine_path, prefix, server ? ",wait,server" : "");
    assert(r > 0);
    free(prefix);
    return chardesc;
}

static Chardev *rp_autocreate_chardev(RemotePort *s, char *name)
{
    Chardev *chr;
    char *chardesc;

    chardesc = rp_autocreate_chardesc(s, false);
    chr = qemu_chr_new_noreplay(name, chardesc, false, NULL);
    free(chardesc);

    if (!chr) {
        chardesc = rp_autocreate_chardesc(s, true);
        chr = qemu_chr_new_noreplay(name, chardesc, false, NULL);
        free(chardesc);
    }
    return chr;
}

void rp_process(RemotePort *s)
{
    while (true) {
        struct rp_pkt *pkt;
        unsigned int rpos;
        bool actioned = false;
        RemotePortDevice *dev;
        RemotePortDeviceClass *rpdc;

        qemu_mutex_lock(&s->rsp_mutex);
        if (!rp_has_work(s)) {
            qemu_mutex_unlock(&s->rsp_mutex);
            break;
        }
        rpos = s->rx_queue.rpos;

        pkt = s->rx_queue.pkt[rpos].pkt;
        D(qemu_log("%s: io-thread rpos=%d wpos=%d cmd=%d dev=%d\n",
                 s->prefix, s->rx_queue.rpos, s->rx_queue.wpos,
                 pkt->hdr.cmd, pkt->hdr.dev));

        /* To handle recursiveness, we need to advance the index
         * index before processing the packet.  */
        s->rx_queue.rpos++;
        s->rx_queue.rpos %= ARRAY_SIZE(s->rx_queue.pkt);
        qemu_mutex_unlock(&s->rsp_mutex);

        dev = s->devs[pkt->hdr.dev];
        if (dev) {
            rpdc = REMOTE_PORT_DEVICE_GET_CLASS(dev);
            if (rpdc->ops[pkt->hdr.cmd]) {
                rpdc->ops[pkt->hdr.cmd](dev, pkt);
                actioned = true;
            }
        }

        switch (pkt->hdr.cmd) {
        case RP_CMD_sync:
            rp_cmd_sync(s, pkt);
            break;
        default:
            assert(actioned);
        }

        s->rx_queue.inuse[rpos] = false;
        qemu_sem_post(&s->rx_queue.sem);
    }
}

static void rp_event_read(void *opaque)
{
    RemotePort *s = REMOTE_PORT(opaque);
    unsigned char buf[32];
    ssize_t r;

    /* We don't care about the data. Just read it out to clear the event.  */
    do {
#ifdef _WIN32
        r = qemu_recv_wrap(s->event.pipe.read, buf, sizeof buf, 0);
#else
        r = read(s->event.pipe.read, buf, sizeof buf);
#endif
        if (r == 0) {
            return;
        }
    } while (r == sizeof buf || (r < 0 && errno == EINTR));

    rp_process(s);
}

static void rp_event_notify(RemotePort *s)
{
    unsigned char d = 0;
    ssize_t r;

#ifdef _WIN32
    /* Mingw is sensitive about doing write's to socket descriptors.  */
    r = qemu_send_wrap(s->event.pipe.write, &d, sizeof d, 0);
#else
    r = qemu_write_full(s->event.pipe.write, &d, sizeof d);
#endif
    if (r == 0) {
        hw_error("%s: pipe closed\n", s->prefix);
    }
}

/* Handover a pkt to CPU or IO-thread context.  */
static void rp_pt_handover_pkt(RemotePort *s, RemotePortDynPkt *dpkt)
{
    bool full;

    /* Take the rsp lock around the wpos update, otherwise
       rp_wait_resp will race with us.  */
    qemu_mutex_lock(&s->rsp_mutex);
    s->rx_queue.wpos++;
    s->rx_queue.wpos %= ARRAY_SIZE(s->rx_queue.pkt);
    smp_mb();
    rp_event_notify(s);
    qemu_cond_signal(&s->progress_cond);
    qemu_mutex_unlock(&s->rsp_mutex);

    do {
        full = s->rx_queue.inuse[s->rx_queue.wpos];
        if (full) {
            qemu_log("%s: FULL rx queue %d\n", __func__, s->rx_queue.wpos);
	    if (qemu_sem_timedwait(&s->rx_queue.sem, 2 * 1000) != 0) {
#ifndef _WIN32
                int sval;

#ifndef CONFIG_SEM_TIMEDWAIT
                sval = s->rx_queue.sem.count;
#else
                sem_getvalue(&s->rx_queue.sem.sem, &sval);
#endif
                qemu_log("semwait: %d rpos=%u wpos=%u\n", sval,
                         s->rx_queue.rpos, s->rx_queue.wpos);
#endif
                qemu_log("Deadlock?\n");
	    }
        }
    } while (full);
}

static bool rp_pt_cmd_sync(RemotePort *s, struct rp_pkt *pkt)
{
    size_t enclen;
    int64_t clk;
    int64_t diff = 0;
    struct rp_pkt rsp;

    assert(!(pkt->hdr.flags & RP_PKT_FLAGS_response));

    if (use_icount) {
        clk = rp_normalized_vmclk(s);
        diff = pkt->sync.timestamp - clk;
    }
    enclen = rp_encode_sync_resp(pkt->hdr.id, pkt->hdr.dev, &rsp.sync,
                                 pkt->sync.timestamp);
    assert(enclen == sizeof rsp.sync);

    if (!use_icount || diff < s->sync.quantum) {
        /* We are still OK.  */
        rp_write(s, (void *) &rsp, enclen);
        return true;
    }

    /* We need IO or CPU thread sync.  */
    return false;
}

static bool rp_pt_process_pkt(RemotePort *s, RemotePortDynPkt *dpkt)
{
    struct rp_pkt *pkt = dpkt->pkt;

    D(qemu_log("%s: cmd=%x id=%d dev=%d rsp=%d\n", __func__, pkt->hdr.cmd,
             pkt->hdr.id, pkt->hdr.dev,
             pkt->hdr.flags & RP_PKT_FLAGS_response));

    if (pkt->hdr.dev >= ARRAY_SIZE(s->devs)) {
        /* FIXME: Respond with an error.  */
        return true;
    }

    if (pkt->hdr.flags & RP_PKT_FLAGS_response) {
        uint32_t dev = pkt->hdr.dev;
        uint32_t id = pkt->hdr.id;
        int i;

        if (pkt->hdr.flags & RP_PKT_FLAGS_posted) {
            printf("Drop response for posted packets\n");
            return true;
        }

        qemu_mutex_lock(&s->rsp_mutex);

        /* Try to find a per-device slot first.  */
        for (i = 0; i < ARRAY_SIZE(s->dev_state[dev].rsp_queue); i++) {
            if (s->devs[dev] && s->dev_state[dev].rsp_queue[i].used == true
                && s->dev_state[dev].rsp_queue[i].id == id) {
                break;
            }
        }

        if (i < ARRAY_SIZE(s->dev_state[dev].rsp_queue)) {
            /* Found a per device one.  */
            assert(s->dev_state[dev].rsp_queue[i].valid == false);

            rp_dpkt_swap(&s->dev_state[dev].rsp_queue[i].rsp, dpkt);
            s->dev_state[dev].rsp_queue[i].valid = true;

            qemu_cond_signal(&s->progress_cond);
        } else {
            rp_dpkt_swap(&s->rspqueue, dpkt);
            qemu_cond_signal(&s->progress_cond);
        }

        qemu_mutex_unlock(&s->rsp_mutex);
        return true;
    }

    switch (pkt->hdr.cmd) {
    case RP_CMD_hello:
        rp_cmd_hello(s, pkt);
        break;
    case RP_CMD_sync:
        if (rp_pt_cmd_sync(s, pkt)) {
            return true;
        }
        /* Fall-through.  */
    case RP_CMD_read:
    case RP_CMD_write:
    case RP_CMD_interrupt:
    case RP_CMD_ats_req:
    case RP_CMD_ats_inv:
        rp_pt_handover_pkt(s, dpkt);
        break;
    default:
        assert(0);
        break;
    }
    return false;
}

static int rp_read_pkt(RemotePort *s, RemotePortDynPkt *dpkt)
{
    struct rp_pkt *pkt = dpkt->pkt;
    int used;
    int r;

    r = rp_recv(s, pkt, sizeof pkt->hdr);
    if (r <= 0) {
        return r;
    }
    used = rp_decode_hdr((void *) &pkt->hdr);
    assert(used == sizeof pkt->hdr);

    if (pkt->hdr.len) {
        rp_dpkt_alloc(dpkt, sizeof pkt->hdr + pkt->hdr.len);
        /* pkt may move due to realloc.  */
        pkt = dpkt->pkt;
        r = rp_recv(s, &pkt->hdr + 1, pkt->hdr.len);
        if (r <= 0) {
            return r;
        }
        rp_decode_payload(pkt);
    }

    return used + r;
}

static void *rp_protocol_thread(void *arg)
{
    RemotePort *s = REMOTE_PORT(arg);
    unsigned int i;
    int r;

    /* Make sure we have a decent bufsize to start with.  */
    rp_dpkt_alloc(&s->rsp, sizeof s->rsp.pkt->busaccess + 1024);
    rp_dpkt_alloc(&s->rspqueue, sizeof s->rspqueue.pkt->busaccess + 1024);
    for (i = 0; i < ARRAY_SIZE(s->rx_queue.pkt); i++) {
        rp_dpkt_alloc(&s->rx_queue.pkt[i],
                      sizeof s->rx_queue.pkt[i].pkt->busaccess + 1024);
        s->rx_queue.inuse[i] = false;
    }

    rp_say_hello(s);

    while (1) {
        RemotePortDynPkt *dpkt;
        unsigned int wpos = s->rx_queue.wpos;
        bool handled;

        dpkt = &s->rx_queue.pkt[wpos];
        s->rx_queue.inuse[wpos] = true;

        r = rp_read_pkt(s, dpkt);
        if (r <= 0) {
            /* Disconnected.  */
            break;
        }
        if (0) {
            rp_pkt_dump("rport-pkt", (void *) dpkt->pkt,
                        sizeof dpkt->pkt->hdr + dpkt->pkt->hdr.len);
        }
        handled = rp_pt_process_pkt(s, dpkt);
        if (handled) {
            s->rx_queue.inuse[wpos] = false;
        }
    }

    if (!s->finalizing) {
        rp_fatal_error(s, "Disconnected");
    }
    return NULL;
}

static void rp_realize(DeviceState *dev, Error **errp)
{
    RemotePort *s = REMOTE_PORT(dev);
    int r;

    s->prefix = object_get_canonical_path(OBJECT(dev));

    s->peer.clk_base = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    qemu_mutex_init(&s->write_mutex);
    qemu_mutex_init(&s->rsp_mutex);
    qemu_cond_init(&s->progress_cond);

    if (!qemu_chr_fe_get_driver(&s->chr)) {
        char *name;
        Chardev *chr = NULL;
        static int nr = 0;

        r = asprintf(&name, "rport%d", nr);
        nr++;
        assert(r > 0);

        if (s->chrdev_id) {
            chr = qemu_chr_find(s->chrdev_id);
        }

        if (chr) {
            /* Found the chardev via commandline */
        } else if (s->chardesc) {
            chr = qemu_chr_new(name, s->chardesc, NULL);
        } else {
            if (!machine_path) {
                error_report("%s: Missing chardesc prop."
                             " Forgot -machine-path?\n",
                             s->prefix);
                exit(EXIT_FAILURE);
            }
            chr = rp_autocreate_chardev(s, name);
        }

        free(name);
        if (!chr) {
            error_report("%s: Unable to create remort-port channel %s\n",
                         s->prefix, s->chardesc);
            exit(EXIT_FAILURE);
        }

        qdev_prop_set_chr(dev, "chardev", chr);
        s->chrdev = chr;
    }

    /* Force RP sockets into blocking mode since our RP-thread will deal
     * with the IO and bypassing QEMUs main-loop.
     */
    qemu_chr_fe_set_blocking(&s->chr, true);

#ifdef _WIN32
    /* Create a socket connection between two sockets. We auto-bind
     * and read out the port selected by the kernel.
     */
    {
        char *name;
        SocketAddress *sock;
        int port;
        int listen_sk;

        sock = socket_parse("127.0.0.1:0", &error_abort);
        listen_sk = socket_listen(sock, 1, &error_abort);

        if (s->event.pipe.read < 0) {
            perror("socket read");
            exit(EXIT_FAILURE);
        }

        {
            struct sockaddr_in saddr;
            socklen_t slen = sizeof saddr;
            int r;

            r = getsockname(listen_sk, (struct sockaddr *) &saddr, &slen);
            if (r < 0) {
                perror("getsockname");
                exit(EXIT_FAILURE);
            }
            port = htons(saddr.sin_port);
        }

        name = g_strdup_printf("127.0.0.1:%d", port);
        s->event.pipe.write = inet_connect(name, &error_abort);
        g_free(name);
        if (s->event.pipe.write < 0) {
            perror("socket write");
            exit(EXIT_FAILURE);
        }

        for (;;) {
            struct sockaddr_in saddr;
            socklen_t slen = sizeof saddr;
            int fd;

            slen = sizeof(saddr);
            fd = qemu_accept(listen_sk, (struct sockaddr *)&saddr, &slen);
            if (fd < 0 && errno != EINTR) {
                close(listen_sk);
                return;
            } else if (fd >= 0) {
                close(listen_sk);
                s->event.pipe.read = fd;
                break;
            }
        }

        qemu_set_nonblock(s->event.pipe.read);
        qemu_set_fd_handler(s->event.pipe.read, rp_event_read, NULL, s);
    }
#else
    r = qemu_pipe(s->event.pipes);
    if (r < 0) {
        error_report("%s: Unable to create remort-port internal pipes\n",
                    s->prefix);
        exit(EXIT_FAILURE);
    }
    qemu_set_nonblock(s->event.pipe.read);
    qemu_set_fd_handler(s->event.pipe.read, rp_event_read, NULL, s);
#endif


    /* Pick up the quantum from the local property setup.
       After config negotiation with the peer, sync.quantum value might
       change.  */
    s->sync.quantum = s->peer.local_cfg.quantum;

    s->sync.ptimer = ptimer_init(sync_timer_hit, s, PTIMER_POLICY_DEFAULT);
    s->sync.ptimer_resp = ptimer_init(syncresp_timer_hit, s,
                                      PTIMER_POLICY_DEFAULT);

    /* The Sync-quantum is expressed in nano-seconds.  */
    ptimer_transaction_begin(s->sync.ptimer);
    ptimer_set_freq(s->sync.ptimer, 1000 * 1000 * 1000);
    ptimer_transaction_commit(s->sync.ptimer);

    ptimer_transaction_begin(s->sync.ptimer_resp);
    ptimer_set_freq(s->sync.ptimer_resp, 1000 * 1000 * 1000);
    ptimer_transaction_commit(s->sync.ptimer_resp);

    qemu_sem_init(&s->rx_queue.sem, ARRAY_SIZE(s->rx_queue.pkt) - 1);
    qemu_thread_create(&s->thread, "remote-port", rp_protocol_thread, s,
                       QEMU_THREAD_JOINABLE);

    rp_restart_sync_timer(s);
}

static void rp_unrealize(DeviceState *dev)
{
    RemotePort *s = REMOTE_PORT(dev);

    s->finalizing = true;

    /* Unregister handler.  */
    qemu_set_fd_handler(s->event.pipe.read, NULL, NULL, s);

    info_report("%s: Wait for remote-port to disconnect\n", s->prefix);
    qemu_chr_fe_disconnect(&s->chr);
    qemu_thread_join(&s->thread);

    close(s->event.pipe.read);
    close(s->event.pipe.write);
    object_unparent(OBJECT(s->chrdev));
}

static const VMStateDescription vmstate_rp = {
    .name = TYPE_REMOTE_PORT,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST(),
    }
};

static Property rp_properties[] = {
    DEFINE_PROP_CHR("chardev", RemotePort, chr),
    DEFINE_PROP_STRING("chardesc", RemotePort, chardesc),
    DEFINE_PROP_STRING("chrdev-id", RemotePort, chrdev_id),
    DEFINE_PROP_BOOL("sync", RemotePort, do_sync, false),
    DEFINE_PROP_UINT64("sync-quantum", RemotePort, peer.local_cfg.quantum,
                       1000000),
    DEFINE_PROP_END_OF_LIST(),
};

static void rp_init(Object *obj)
{
    RemotePort *s = REMOTE_PORT(obj);
    int t;
    int i;

    for (i = 0; i < REMOTE_PORT_MAX_DEVS; ++i) {
        char *name = g_strdup_printf("remote-port-dev%d", i);
        object_property_add_link(obj, name, TYPE_REMOTE_PORT_DEVICE,
                             (Object **)&s->devs[i],
                             qdev_prop_allow_set_link,
                             OBJ_PROP_LINK_STRONG);
        g_free(name);


        for (t = 0; t < RP_MAX_OUTSTANDING_TRANSACTIONS; t++) {
            s->dev_state[i].rsp_queue[t].used = false;
            s->dev_state[i].rsp_queue[t].valid = false;
            rp_dpkt_alloc(&s->dev_state[i].rsp_queue[t].rsp,
               sizeof s->dev_state[i].rsp_queue[t].rsp.pkt->busaccess + 1024);
        }
    }
}

struct rp_peer_state *rp_get_peer(RemotePort *s)
{
    return &s->peer;
}

static void rp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rp_realize;
    dc->unrealize = rp_unrealize;
    dc->vmsd = &vmstate_rp;
    device_class_set_props(dc, rp_properties);
}

static const TypeInfo rp_info = {
    .name          = TYPE_REMOTE_PORT,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(RemotePort),
    .instance_init = rp_init,
    .class_init    = rp_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { },
    },
};

static const TypeInfo rp_device_info = {
    .name          = TYPE_REMOTE_PORT_DEVICE,
    .parent        = TYPE_INTERFACE,
    .class_size    = sizeof(RemotePortDeviceClass),
};

static void rp_register_types(void)
{
    type_register_static(&rp_info);
    type_register_static(&rp_device_info);
}

type_init(rp_register_types)
