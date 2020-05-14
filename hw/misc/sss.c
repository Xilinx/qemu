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

static inline int
sss_lookup_tx_remote(SSSBase *s, int rx_remote)
{
    uint32_t enc;
    if (rx_remote == NOT_REMOTE(s)) {
        return NOT_REMOTE(s);
    }

    int ret;

    for (ret = 0; ret < NOT_REMOTE(s); ++ret) {
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

static bool
sss_stream_can_push(StreamSlave *obj, StreamCanPushNotifyFn notify,
                                void *notify_opaque)
{
    SSSStream *ss = SSS_STREAM(obj);
    SSSBase *s = SSS_BASE(ss->sss);
    int rx = sss_lookup_rx_remote(s, ss);
    int tx = sss_lookup_tx_remote(s, rx);

    if (tx != NOT_REMOTE(s) && s->tx_devs[tx] &&
            stream_can_push(s->tx_devs[tx], notify, notify_opaque)) {
        return true;
    }

    s->notifys[rx] = notify;
    s->notify_opaques[rx] = notify_opaque;
    return false;
}

static size_t sss_stream_push(StreamSlave *obj, uint8_t *buf,
                              size_t len, bool eop)
{
    SSSStream *ss = SSS_STREAM(obj);
    SSSBase *s = SSS_BASE(ss->sss);
    int rx = sss_lookup_rx_remote(s, ss);
    int tx = sss_lookup_tx_remote(s, rx);

    return (tx != NOT_REMOTE(s)) ?
            stream_push(s->tx_devs[tx], buf, len, eop) : 0;
}

static void sss_stream_class_init(ObjectClass *klass, void *data)
{
    StreamSlaveClass *ssc = STREAM_SLAVE_CLASS(klass);

    ssc->push = sss_stream_push;
    ssc->can_push = sss_stream_can_push;
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
        { TYPE_STREAM_SLAVE },
        { }
    }
};

static void sss_register_types(void)
{
    type_register_static(&sss_info);
    type_register_static(&sss_stream_info);
}

type_init(sss_register_types)
