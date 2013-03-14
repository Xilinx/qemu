/*
 * QEMU model of ZynqMP CSU Stream PCAP
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
#include "hw/qdev.h"
#include "qemu/log.h"

#include "hw/stream.h"
#include "qemu/bitops.h"

#ifndef ZYNQMP_CSU_PCAP_ERR_DEBUG
#define ZYNQMP_CSU_PCAP_ERR_DEBUG 1
#endif

#define TYPE_ZYNQMP_CSU_PCAP "zynqmp.csu-pcap"

#define ZYNQMP_CSU_PCAP(obj) \
     OBJECT_CHECK(ZynqMPCSUPCAP, (obj), TYPE_ZYNQMP_CSU_PCAP)

/* FIXME: This is a random number, maybe match to PCAP fifo size or just pick
 * something reasonable that keep QEMU performing good
 */

#define CHUNK_SIZE (8 << 10)

typedef struct ZynqMPCSUPCAP {
    DeviceState parent_obj;
    StreamSlave *tx_dev;
    MemoryRegion iomem;

} ZynqMPCSUPCAP;

static void zynqmp_csu_pcap_notify(void *opaque)
{
    ZynqMPCSUPCAP *s = ZYNQMP_CSU_PCAP(opaque);
    static uint8_t zeros[CHUNK_SIZE];

    /* blast away - fire as many zeros as the target wants to accept */
    while (stream_can_push(s->tx_dev, zynqmp_csu_pcap_notify, s)) {
        size_t ret = stream_push(s->tx_dev, zeros, CHUNK_SIZE, STREAM_ATTR_EOP);
        /* FIXME: Check - assuming PCAP must be 32-bit aligned xactions */
        assert(!(ret % 4));
    }
}

static void zynqmp_csu_pcap_reset(DeviceState *dev)
{
    ZynqMPCSUPCAP *s = ZYNQMP_CSU_PCAP(dev);

    zynqmp_csu_pcap_notify(s);
}

static size_t zynqmp_csu_pcap_stream_push(StreamSlave *obj, uint8_t *buf,
                                          size_t len, uint32_t attr)
{
    assert(!(len % 4));
    /* consume all the data with no action */
    return len;
}

static void zynqmp_csu_pcap_init(Object *obj)
{
    ZynqMPCSUPCAP *s = ZYNQMP_CSU_PCAP(obj);

    /* Real HW has a link, but no way of initiating this link */
    object_property_add_link(obj, "stream-connected-pcap", TYPE_STREAM_SLAVE,
                             (Object **) &s->tx_dev,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             NULL);
}

/* FIXME: With no regs we are actually stateless. Although post load we need
 * to call notify() to start up the fire-hose of zeros again.
 */

static const VMStateDescription vmstate_zynqmp_csu_pcap = {
    .name = "zynqmp_csu_pcap",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST(),
    }
};

static void zynqmp_csu_pcap_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    StreamSlaveClass *ssc = STREAM_SLAVE_CLASS(klass);

    dc->reset = zynqmp_csu_pcap_reset;
    dc->vmsd = &vmstate_zynqmp_csu_pcap;

    ssc->push = zynqmp_csu_pcap_stream_push;
}

static const TypeInfo zynqmp_csu_pcap_info = {
    .name          = TYPE_ZYNQMP_CSU_PCAP,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(ZynqMPCSUPCAP),
    .class_init    = zynqmp_csu_pcap_class_init,
    .instance_init = zynqmp_csu_pcap_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_STREAM_SLAVE },
        { }
    }
};

static void zynqmp_csu_pcap_register_types(void)
{
    type_register_static(&zynqmp_csu_pcap_info);
}

type_init(zynqmp_csu_pcap_register_types)
