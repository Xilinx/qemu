/*
 * QEMU remote port.
 *
 * Copyright (c) 2013 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 * This code is licensed under the GNU GPL.
 */
#ifndef REMOTE_PORT_H__
#define REMOTE_PORT_H__

#include <stdbool.h>
#include "hw/remote-port-proto.h"
#include "hw/remote-port-device.h"
#include "hw/ptimer.h"

#define TYPE_REMOTE_PORT "remote-port"
#define REMOTE_PORT(obj) OBJECT_CHECK(RemotePort, (obj), TYPE_REMOTE_PORT)

struct RemotePort {
    DeviceState parent;

    QemuThread thread;
    union {
       int pipes[2];
       struct {
           int read;
           int write;
       } pipe;
    } event;
    CharDriverState *chr;
    bool do_sync;
    /* To serialize writes to fd.  */
    QemuMutex write_mutex;

    char *chardesc;
    struct rp_peer_state peer;

    struct {
        QEMUBH *bh;
        QEMUBH *bh_resp;
        ptimer_state *ptimer;
        ptimer_state *ptimer_resp;
        bool resp_timer_enabled;
        bool need_sync;
        struct rp_pkt rsp;
        uint64_t quantum;
    } sync;

    QemuMutex rsp_mutex;
    QemuCond progress_cond;

    struct {
        /* This array must be sized minimum 2 and always a power of 2.  */
        RemotePortDynPkt pkt[16];
        QemuSemaphore sem;
        unsigned int wpos;
        unsigned int rpos;
    } rx_queue;

    /*
     * rsp holds responses for the remote side.
     * Used by the slave.
     */
    RemotePortDynPkt rsp;

    /*
     * rspqueue holds received responses from the remote side.
     * Only one for the moment but it might grow.
     * Used by the master.
     */
    RemotePortDynPkt rspqueue;

    bool resets[32];

    const char *prefix;
    const char *remote_prefix;

    uint32_t current_id;

#define REMOTE_PORT_MAX_DEVS 16
    RemotePortDevice *devs[REMOTE_PORT_MAX_DEVS];
};

bool rp_time_warp_enable(bool en);

#endif
