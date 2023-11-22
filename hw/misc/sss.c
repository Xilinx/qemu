/*
 * QEMU model of ZynqMP CSU Secure Stream Switch (SSS)
 *
 * For the most part, a dummy device model. Consumes as much data off the stream
 * interface as you can throw at it and produces zeros as fast as the sink is
 * willing to accept them.
 *
 * Copyright (c) 2013 Peter Xilinx Inc
 * Copyright (c) 2013 Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qapi/error.h"
#include "qemu/log.h"

#include "qemu/bitops.h"
#include "qapi/qmp/qerror.h"
#include "hw/misc/sss.h"

void sss_notify_all(SSSBase *s)
{
    int remote;

    for (remote = 0; remote < s->num_remotes; ++remote) {
        if (s->notifys[remote]) {
            s->notifys[remote](s->notify_opaques[remote]);
            s->notifys[remote] = NULL;
        }
    }
}

static inline int
sss_lookup_rx_remote(SSSBase *s, SSSStream *ss)
{
    int ret;

    for (ret = 0; ret < s->num_remotes; ++ret) {
        if (ss == &s->rx_devs[ret]) {
            break;
        }
    }
    return ret;
}

/**
 * sss_lookup_tx_remote:
 *
 * Check for TX remotes connected to @rx_remote by the switch.  Since the
 * initiator can reach multiple targets, the first available target after
 * @start will be returned.
 */
static inline int
sss_lookup_tx_remote(SSSBase *s, int rx_remote, int start)
{
    uint32_t enc;
    int ret;

    if (rx_remote == NOT_REMOTE(s)) {
        return NOT_REMOTE(s);
    }

    for (ret = start; ret < NOT_REMOTE(s); ++ret) {
        if (s->r_sss_shifts[ret] == -1) {
            /* This unit has no input. Ignore it.  */
            continue;
        }

        enc = s->get_sss_regfield(s, ret);
        if (s->r_sss_encodings[rx_remote] == enc) {
            break;
        }
    }
    return (s->sss_population[ret] & (1 << rx_remote)) ?
                    ret : NOT_REMOTE(s);
}

static inline size_t sss_num_tx_remote(SSSBase *s, int rx_remote)
{
    uint32_t enc;
    size_t ret = 0;
    size_t i;

    for (i = 0; i < NOT_REMOTE(s); i++) {
        if (s->r_sss_shifts[i] == -1) {
            /* This unit has no input. Ignore it.  */
            continue;
        }

        enc = s->get_sss_regfield(s, i);
        if (s->r_sss_encodings[rx_remote] == enc) {
            ret++;
        }
    }

    return ret;
}

static void sss_stream_abort(StreamSink *obj)
{
    SSSStream *ss = SSS_STREAM(obj);
    SSSBase *s = SSS_BASE(ss->sss);
    int rx = sss_lookup_rx_remote(s, ss);

    /* Basically clear the packet if there are pending.  */
    memset(&s->pending_transactions[rx], 0, sizeof(SSSPendingTransaction));
}

static bool
sss_stream_can_push(StreamSink *obj, StreamCanPushNotifyFn notify,
                    void *notify_opaque)
{
    SSSStream *ss = SSS_STREAM(obj);
    SSSBase *s = SSS_BASE(ss->sss);
    /* Find the initiator ID for that StreamSink.  */
    int rx = sss_lookup_rx_remote(s, ss);
    int tx = 0;
    bool ret = false;

    tx = sss_lookup_tx_remote(s, rx, 0);
    while (tx != NOT_REMOTE(s) && (s->tx_devs[tx])) {
        ret = true;
        /*
         * If there is a pending transaction running, the number of data
         * might be not synchronised between all the targets, some of them
         * might already got the complete packet, and they might return false
         * here.  Return true for those target port.
         */
        if ((!s->pending_transactions[rx].active
            || s->pending_transactions[rx].remaining[tx])
            && (!stream_can_push(s->tx_devs[tx], notify, notify_opaque))) {
            ret = false;
            break;
        }

        /* Check for the next target port id.  */
        tx = sss_lookup_tx_remote(s, rx, tx + 1);
    }

    s->notifys[rx] = notify;
    s->notify_opaques[rx] = notify_opaque;

    return ret;
}

static size_t sss_stream_push(StreamSink *obj, uint8_t *buf,
                              size_t len, bool eop)
{
    SSSStream *ss = SSS_STREAM(obj);
    SSSBase *s = SSS_BASE(ss->sss);
    int rx = sss_lookup_rx_remote(s, ss);
    int tx;
    int tx_count = sss_num_tx_remote(s, rx);
    size_t remaining = len;
    uint8_t chunk[MULTI_BUF_SIZE];
    size_t out_len;
    bool out_eop = false;
    size_t consumed;

    if (s->pending_transactions[rx].active) {
        /*
         * Check if there is already a transaction pending, in which case it
         * needs to be flushed before continuing.  The beggining of the buf in
         * the transaction is what has been copied in the data field.
         */
        bool transfer_completed = true;

        for (tx = 0; tx < MAX_REMOTE; tx++) {
            if (s->pending_transactions[rx].remaining[tx]) {
                /*
                 * In case the mapping changed and the remote target is not
                 * accessible to the source: drop the data.
                 */
                if (sss_lookup_tx_remote(s, rx, tx) != tx) {
                    s->pending_transactions[rx].remaining[tx] = 0;
                    continue;
                }

                /* Some data remains for tx, try to flush them.  */
                out_len = MIN(remaining,
                              s->pending_transactions[rx].remaining[tx]);
                g_assert(out_len <= MULTI_BUF_SIZE);
                memcpy(chunk,
                       s->pending_transactions[rx].data
                       + s->pending_transactions[rx].data_len
                       - s->pending_transactions[rx].remaining[tx],
                       out_len);
                consumed = stream_push(s->tx_devs[tx], chunk, out_len, out_eop);
                s->pending_transactions[rx].remaining[tx] -= consumed;
                if (s->pending_transactions[rx].remaining[tx]) {
                    /* There are still remaining data unfortunately.  */
                    transfer_completed = false;
                }
            }
        }

        if (transfer_completed) {
            /*
             * All the targets got the data.  Remove the active flag, and
             * resume the normal operations.
             */
            remaining -= s->pending_transactions[rx].data_len;
            s->pending_transactions[rx].active = false;
        } else {
            /* There are still data..  */
            return 0;
        }
    }

    switch (tx_count) {
    case 0:
        return 0;
    case 1:
        /*
         * The simple case where we have only one remote.  Let's keep it simple
         * and pass the target the whole data.
         */
        tx = sss_lookup_tx_remote(s, rx, 0);
        return (tx != NOT_REMOTE(s)) ?
            stream_push(s->tx_devs[tx], buf, len, eop) : 0;
    default:
        break;
    }

    /*
     * Handle multiple targets.  The risk here is that all the targets don't
     * accept the same amount of data.  In that case the transfer should stop
     * and wait that all the targets can have data again.
     *
     * So pass the data chunk by chunk and keep a copy.  If any of the target
     * is refusing the data, stops and store the status.
     */
    while (remaining && !s->pending_transactions[rx].active) {
        tx = sss_lookup_tx_remote(s, rx, 0);
        /* Compute the length for the next transaction.  */
        out_len = MIN(remaining, MULTI_BUF_SIZE);
        /* For the last chunk and if input has EOP, send an EOP.  */
        out_eop = ((remaining - out_len) == 0) ? eop : false;

        while (tx != NOT_REMOTE(s) && (s->tx_devs[tx])) {
            /* This has to be done, because some target corrupt the data.  */
            memcpy(chunk, &buf[len - remaining], out_len);
            consumed = stream_push(s->tx_devs[tx], chunk, out_len, out_eop);
            if (consumed != out_len) {
                /* The target didn't accept the complete transaction.  */
                if (!s->pending_transactions[rx].active) {
                    memset(&s->pending_transactions[rx], 0,
                           sizeof(SSSPendingTransaction));
                    s->pending_transactions[rx].active = true;
                    memcpy(s->pending_transactions[rx].data,
                           &buf[len - remaining],
                           out_len);
                    s->pending_transactions[rx].data_len = out_len;
                }
                s->pending_transactions[rx].remaining[tx] = out_len - consumed;
            }
            /* Check for the next target port id.  */
            tx = sss_lookup_tx_remote(s, rx, tx + 1);
        }

        /*
         * Always assume that the last block hasn't been consumed in case the
         * last block didn't go through.
         */
        remaining = s->pending_transactions[rx].active
            ? remaining
            : remaining - out_len;
    }

    return len - remaining;
}

static void sss_stream_class_init(ObjectClass *klass, void *data)
{
    StreamSinkClass *ssc = STREAM_SINK_CLASS(klass);

    ssc->push = sss_stream_push;
    ssc->can_push = sss_stream_can_push;
    ssc->abort = sss_stream_abort;
}

static const TypeInfo sss_info = {
    .name         = TYPE_SSS_BASE,
    .parent       = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SSSBase),
};

static const TypeInfo sss_stream_info = {
    .name          = TYPE_SSS_STREAM,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(SSSStream),
    .class_init    = sss_stream_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_STREAM_SINK },
        { }
    }
};

static void sss_register_types(void)
{
    type_register_static(&sss_info);
    type_register_static(&sss_stream_info);
}

type_init(sss_register_types)
