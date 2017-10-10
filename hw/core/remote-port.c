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
#include "hw/ptimer.h"
#include "qemu/sockets.h"
#include "qemu/thread.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qom/cpu.h"

#include <semaphore.h>
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

static void rp_process(RemotePort *s);
static void rp_event_read(void *opaque);
static void sync_timer_hit(void *opaque);
static void syncresp_timer_hit(void *opaque);

static void rp_pkt_dump(const char *prefix, const char *buf, size_t len)
{
    qemu_hexdump(buf, stdout, prefix, len);
}

uint32_t rp_new_id(RemotePort *s)
{
    return s->current_id++;
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

static inline int64_t rp_denormalize_clk(RemotePort *s, int64_t rclk)
{
    int64_t clk;
    clk = rclk + s->peer.clk_base;
    return clk;
}

void rp_restart_sync_timer(RemotePort *s)
{
    if (!use_icount || !s->do_sync) {
        return;
    }

    if (s->sync.quantum) {
        ptimer_stop(s->sync.ptimer);
        ptimer_set_limit(s->sync.ptimer, s->sync.quantum, 1);
        ptimer_run(s->sync.ptimer, 1);
    } else {
        ptimer_stop(s->sync.ptimer);
        ptimer_set_limit(s->sync.ptimer, 10 * 1000, 1);
        ptimer_run(s->sync.ptimer, 1);
    }
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
        rp_fatal_error(s, "Disconnected");
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
    r = qemu_chr_fe_write(&s->chr, buf, count);
    qemu_mutex_unlock(&s->write_mutex);
    if (r <= 0) {
        error_report("%s: Disconnected r=%zd buf=%p count=%zd\n",
                     s->prefix, r, buf, count);
        rp_fatal_error(s, "Bad write");
    }
    return r;
}

/* Warp time if cpus are idle. diff is max time in ns to warp.  */
static int64_t rp_time_warp(RemotePort *s, int64_t diff)
{
    int64_t future = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + diff;
    int64_t clk;

    if (!time_warp_enable) {
        return 0;
    }

    /* If cpus are idle. warp.  */
    do {
        bool cpus_idle;

        cpus_idle = tcg_idle_clock_warp(future);
        clk = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        if (!cpus_idle) {
            break;
        }
    } while (clk < future);

    return future - clk;
}

static void rp_idle(CPUState *cpu, run_on_cpu_data data)
{
    int64_t deadline = qemu_clock_deadline_ns_all(QEMU_CLOCK_VIRTUAL);
    if (deadline == INT32_MAX) {
            return;
    }
    rp_time_warp((void *)(uintptr_t) data.target_ptr, deadline);
}

void rp_leave_iothread(RemotePort *s)
{
    if (use_icount && all_cpu_threads_idle() && time_warp_enable) {
        async_run_on_cpu(first_cpu, rp_idle, RUN_ON_CPU_TARGET_PTR((uintptr_t) s));
    }
}

RemotePortDynPkt rp_wait_resp(RemotePort *s)
{
    while (!rp_dpkt_is_valid(&s->rspqueue)) {
        rp_event_read(s);
        qemu_cond_wait(&s->progress_cond, &s->rsp_mutex);
    }
    return s->rspqueue;
}

void rp_sync_vmclock(RemotePort *s, int64_t lclk, int64_t rclk)
{
    int64_t diff;

    /* FIXME: syncing is broken at the moment.  */
    return;

    if (!time_warp_enable) {
        return;
    }

    /* Warp all the way to dest. We need to account for
       spent time in the remote call.  */
    while (lclk < rclk) {
        diff = rclk - lclk;
        tcg_clock_warp(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + diff);
        lclk = rp_normalized_vmclk(s);
    }

    if (lclk < rclk) {
        printf("Wrong sync! local=%" PRIu64 " remote=%" PRIu64 "\n", lclk, rclk);
    }
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

    /* If cpus are idle. warp.  */
    clk = rp_normalized_vmclk(s);
    diff = pkt->sync.timestamp - clk;
    if (diff > 0LL && use_icount && time_warp_enable) {
        diff = rp_time_warp(s, diff);
    }

    enclen = rp_encode_sync_resp(pkt->hdr.id, pkt->hdr.dev, &s->sync.rsp.sync,
                                 pkt->sync.timestamp);
    assert(enclen == sizeof s->sync.rsp.sync);

    /* We are already a head of time.  */
    if (all_cpu_threads_idle() || 0) {
        if (pkt->sync.timestamp >  clk && use_icount)
            rp_sync_vmclock(s, clk, pkt->sync.timestamp);
    }

    /* We have temporarily disabled blocking syncs into QEMU.  */
    if (diff <= 0LL || true) {
        /* We are already a head of time. Respond and issue a sync.  */
        SYNCD(printf("%s: sync resp %lu\n", s->prefix, pkt->sync.timestamp));
        rp_write(s, (void *) &s->sync.rsp, enclen);
        return;
    }

    SYNCD(printf("%s: delayed sync resp - start diff=%ld (ts=%lu clk=%lu)\n",
          s->prefix, pkt->sync.timestamp - clk, pkt->sync.timestamp, clk));
    ptimer_set_limit(s->sync.ptimer_resp, diff, 1);
    ptimer_run(s->sync.ptimer_resp, 1);
    s->sync.resp_timer_enabled = true;
}

static void rp_say_hello(RemotePort *s)
{
    struct rp_pkt_hello pkt;
    uint32_t caps[] = {
        CAP_BUSACCESS_EXT_BASE,
        CAP_BUSACCESS_EXT_BYTE_EN,
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

    rp_leave_iothread(s);
}

static void sync_timer_hit(void *opaque)
{
    RemotePort *s = REMOTE_PORT(opaque);
    int64_t clk;
    int64_t rclk;
    RemotePortDynPkt rsp;

    if (!use_icount) {
        hw_error("Sync timer without icount??\n");
    }

    clk = rp_normalized_vmclk(s);
    if (s->sync.resp_timer_enabled) {
        SYNCD(printf("%s: sync while delaying a resp! clk=%lu\n",
                     s->prefix, clk));
        s->sync.need_sync = true;
        rp_restart_sync_timer(s);
        rp_leave_iothread(s);
        return;
    }

    /* Sync.  */
    s->sync.need_sync = false;
    qemu_mutex_lock(&s->rsp_mutex);
    /* Send the sync.  */
    rp_say_sync(s, clk);

    SYNCD(printf("%s: syncing wait for resp %lu\n", s->prefix, clk));
    rsp = rp_wait_resp(s);
    rclk = rsp.pkt->sync.timestamp;
    rp_dpkt_invalidate(&rsp);
    qemu_mutex_unlock(&s->rsp_mutex);

    rp_sync_vmclock(s, clk, rclk);
    rp_restart_sync_timer(s);
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
    r = asprintf(&chardesc, "unix:%s/qemu-rport-%s,wait%s",
                 machine_path, prefix, server ? ",server" : "");
    assert(r > 0);
    free(prefix);
    return chardesc;
}

static Chardev *rp_autocreate_chardev(RemotePort *s, char *name)
{
    Chardev *chr;
    char *chardesc;

    chardesc = rp_autocreate_chardesc(s, false);
    chr = qemu_chr_new_noreplay(name, chardesc);
    free(chardesc);

    if (!chr) {
        chardesc = rp_autocreate_chardesc(s, true);
        chr = qemu_chr_new_noreplay(name, chardesc);
        free(chardesc);
    }
    return chr;
}

static unsigned int rp_has_work(RemotePort *s)
{
    unsigned int work = s->rx_queue.wpos - s->rx_queue.rpos;
    return work;
}

static void rp_process(RemotePort *s)
{
    while (rp_has_work(s)) {
        struct rp_pkt *pkt;
        unsigned int rpos = s->rx_queue.rpos;
        bool actioned = false;
        RemotePortDevice *dev;
        RemotePortDeviceClass *rpdc;

        rpos &= ARRAY_SIZE(s->rx_queue.pkt) - 1;

        pkt = s->rx_queue.pkt[rpos].pkt;
        D(qemu_log("%s: io-thread rpos=%d wpos=%d cmd=%d\n", s->prefix,
                 s->rx_queue.rpos, s->rx_queue.wpos, pkt->hdr.cmd));

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

        s->rx_queue.rpos++;
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
            hw_error("%s: pipe closed?\n", s->prefix);
        }
    } while (r == sizeof buf || (r < 0 && errno == EINTR));

    rp_process(s);
    rp_leave_iothread(s);
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
    /* Take the rsp lock around the wpos update, otherwise
       rp_wait_resp will race with us.  */
    qemu_mutex_lock(&s->rsp_mutex);
    s->rx_queue.wpos++;
    smp_mb();
    rp_event_notify(s);
    qemu_cond_signal(&s->progress_cond);
    qemu_mutex_unlock(&s->rsp_mutex);
    while (1) {
        if (qemu_sem_timedwait(&s->rx_queue.sem, 2 * 1000) == 0) {
            break;
        }
#ifndef _WIN32
        {
            int sval;
            sem_getvalue(&s->rx_queue.sem.sem, &sval);
            printf("semwait: %d rpos=%u wpos=%u\n", sval,
                   s->rx_queue.rpos, s->rx_queue.wpos);
        }
#endif
    }
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

static void rp_pt_process_pkt(RemotePort *s, RemotePortDynPkt *dpkt)
{
    struct rp_pkt *pkt = dpkt->pkt;

    D(qemu_log("%s: cmd=%x rsp=%d\n", __func__, pkt->hdr.cmd,
             pkt->hdr.flags & RP_PKT_FLAGS_response));

    if (pkt->hdr.dev >= ARRAY_SIZE(s->devs)) {
        /* FIXME: Respond with an error.  */
        return;
    }

    if (pkt->hdr.flags & RP_PKT_FLAGS_response) {
        qemu_mutex_lock(&s->rsp_mutex);
        rp_dpkt_swap(&s->rspqueue, dpkt);
        qemu_cond_signal(&s->progress_cond);
        qemu_mutex_unlock(&s->rsp_mutex);
        return;
    }

    switch (pkt->hdr.cmd) {
    case RP_CMD_hello:
        rp_cmd_hello(s, pkt);
        break;
    case RP_CMD_sync:
        if (rp_pt_cmd_sync(s, pkt)) {
            return;
        }
        /* Fall-through.  */
    case RP_CMD_read:
    case RP_CMD_write:
    case RP_CMD_interrupt:
        rp_pt_handover_pkt(s, dpkt);
        break;
    default:
        assert(0);
        break;
    }
}

static void rp_read_pkt(RemotePort *s, RemotePortDynPkt *dpkt)
{
    struct rp_pkt *pkt = dpkt->pkt;
    int used;

    rp_recv(s, pkt, sizeof pkt->hdr);
    used = rp_decode_hdr((void *) &pkt->hdr);
    assert(used == sizeof pkt->hdr);

    if (pkt->hdr.len) {
        rp_dpkt_alloc(dpkt, sizeof pkt->hdr + pkt->hdr.len);
        /* pkt may move due to realloc.  */
        pkt = dpkt->pkt;
        rp_recv(s, &pkt->hdr + 1, pkt->hdr.len);
        rp_decode_payload(pkt);
    }
}

static void *rp_protocol_thread(void *arg)
{
    RemotePort *s = REMOTE_PORT(arg);
    unsigned int i;

    /* Make sure we have a decent bufsize to start with.  */
    rp_dpkt_alloc(&s->rsp, sizeof s->rsp.pkt->busaccess + 1024);
    rp_dpkt_alloc(&s->rspqueue, sizeof s->rspqueue.pkt->busaccess + 1024);
    for (i = 0; i < ARRAY_SIZE(s->rx_queue.pkt); i++) {
        rp_dpkt_alloc(&s->rx_queue.pkt[i],
                      sizeof s->rx_queue.pkt[i].pkt->busaccess + 1024);
    }

    rp_say_hello(s);

    while (1) {
        RemotePortDynPkt *dpkt;
        unsigned int wpos = s->rx_queue.wpos;

        wpos &= ARRAY_SIZE(s->rx_queue.pkt) - 1;
        dpkt = &s->rx_queue.pkt[wpos];

        rp_read_pkt(s, dpkt);
        if (0) {
            rp_pkt_dump("rport-pkt", (void *) dpkt->pkt,
                        sizeof dpkt->pkt->hdr + dpkt->pkt->hdr.len);
        }
        rp_pt_process_pkt(s, dpkt);
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
            chr = qemu_chr_new(name, s->chardesc);
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
        listen_sk = socket_listen(sock, &error_abort);

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

    s->sync.bh = qemu_bh_new(sync_timer_hit, s);
    s->sync.bh_resp = qemu_bh_new(syncresp_timer_hit, s);
    s->sync.ptimer = ptimer_init(s->sync.bh, PTIMER_POLICY_DEFAULT);
    s->sync.ptimer_resp = ptimer_init(s->sync.bh_resp, PTIMER_POLICY_DEFAULT);

    /* The Sync-quantum is expressed in nano-seconds.  */
    ptimer_set_freq(s->sync.ptimer, 1000 * 1000 * 1000);
    ptimer_set_freq(s->sync.ptimer_resp, 1000 * 1000 * 1000);

    qemu_sem_init(&s->rx_queue.sem, ARRAY_SIZE(s->rx_queue.pkt) - 1);
    qemu_thread_create(&s->thread, "remote-port", rp_protocol_thread, s,
                       QEMU_THREAD_JOINABLE);
    rp_restart_sync_timer(s);
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
    DEFINE_PROP_UINT64("sync-quantum", RemotePort, peer.local_cfg.quantum, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void rp_init(Object *obj)
{
    RemotePort *s = REMOTE_PORT(obj);
    int i;

    /* Disable icount IDLE time warping. remoteport will take care of it.  */
    qemu_icount_enable_idle_timewarps(false);

    for (i = 0; i < REMOTE_PORT_MAX_DEVS; ++i) {
        char *name = g_strdup_printf("remote-port-dev%d", i);
        object_property_add_link(obj, name, TYPE_REMOTE_PORT_DEVICE,
                             (Object **)&s->devs[i],
                             qdev_prop_allow_set_link,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &error_abort);
        g_free(name);
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
    dc->vmsd = &vmstate_rp;
    dc->props = rp_properties;
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
