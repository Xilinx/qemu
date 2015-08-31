/*
 * QEMU model of Ronaldo CSU Secure Stream Switch (SSS)
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

#include "hw/sysbus.h"
#include "qemu/log.h"

#include "hw/stream.h"
#include "qemu/bitops.h"
#include "qapi/qmp/qerror.h"
#include "hw/register.h"

#ifndef RONALDO_CSU_SSS_ERR_DEBUG
#define RONALDO_CSU_SSS_ERR_DEBUG 0
#endif

#define TYPE_RONALDO_CSU_SSS "ronaldo.csu-sss"
#define TYPE_RONALDO_CSU_SSS_STREAM "ronaldo.csu-sss-stream"

#define RONALDO_CSU_SSS(obj) \
     OBJECT_CHECK(RonaldoCSUSSS, (obj), TYPE_RONALDO_CSU_SSS)

#define RONALDO_CSU_SSS_STREAM(obj) \
     OBJECT_CHECK(RonaldoCSUSSSStream, (obj), TYPE_RONALDO_CSU_SSS_STREAM)


typedef enum {
    DMA,
    PCAP,
    AES,
    SHA,
    PSTP,
    ROM, /* FIXME: ROM, may have no software visibility - delete? */
    NUM_REMOTES
} RonaldoCSUSSSRemote;

#define NO_REMOTE NUM_REMOTES

static const char *ronaldo_csu_sss_remote_names[] = {
    [PCAP] = "pcap",
    [DMA] = "dma",
    [AES] = "aes",
    [SHA] = "sha",
    [PSTP] = "pstp",
    [ROM] = "rom",
    /* FIXME: Add TMR */
};

static const uint32_t ronaldo_csu_sss_population[] = {
    [PCAP] = (1 << DMA) | (1 << AES) | (1 << PSTP),
    [DMA] = (1 << DMA) | (1 << AES) | (1 << PCAP) | (1 << PSTP),
    [AES] = (1 << DMA),
    [SHA] = (1 << DMA) | (1 << ROM),
    [PSTP] = (1 << PCAP),
    [NO_REMOTE] = 0,
};

#define R_CFG 0

static const int r_cfg_sss_shifts[] = {
    [PCAP] = 0,
    [DMA] = 4,
    [AES] = 8,
    [SHA] = 12,
    [PSTP] = 16,
    [ROM] = -1,
};

static const uint8_t r_cfg_sss_encodings[] = {
    [PCAP] = 0x3,
    [DMA] = 0x5,
    [AES] = 0xa,
    [SHA] = 0,
    [PSTP] = 0xc,
    [ROM] = 0x0,
};

#define R_CFG_SSS_LENGTH 4
#define R_CFG_RSVD 0xFFF00000

#define R_MAX (R_CFG + 1)

typedef struct RonaldoCSUSSS RonaldoCSUSSS;
typedef struct RonaldoCSUSSSStream RonaldoCSUSSSStream;

struct RonaldoCSUSSSStream {
    DeviceState parent_obj;

    RonaldoCSUSSS *sss;
};

struct RonaldoCSUSSS {
    SysBusDevice busdev;
    MemoryRegion iomem;
    StreamSlave *tx_devs[NUM_REMOTES];
    RonaldoCSUSSSStream rx_devs[NUM_REMOTES];

    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];

    StreamCanPushNotifyFn notifys[NUM_REMOTES];
    void *notify_opaques[NUM_REMOTES];
};

static void ronaldo_csu_sss_notify_all(RonaldoCSUSSS *s)
{
    RonaldoCSUSSSRemote remote;

    for (remote = 0; remote < NUM_REMOTES; ++remote) {
        if (s->notifys[remote]) {
            s->notifys[remote](s->notify_opaques[remote]);
            s->notifys[remote] = NULL;
        }
    }
}

static void ronaldo_csu_sss_reset(DeviceState *dev)
{
    RonaldoCSUSSS *s = RONALDO_CSU_SSS(dev);
    int i;
 
    for (i = 0; i < R_MAX; ++i) {
        register_reset(&s->regs_info[i]);
    }
    ronaldo_csu_sss_notify_all(s);
}

static inline RonaldoCSUSSSRemote
ronaldo_csu_sss_lookup_rx_remote(RonaldoCSUSSS *s, RonaldoCSUSSSStream *ss)
{
    RonaldoCSUSSSRemote ret;

    for (ret = 0; ret < NUM_REMOTES; ++ret) {
        if (ss == &s->rx_devs[ret]) {
            break;
        }
    }
    return ret;
}

static inline RonaldoCSUSSSRemote
ronaldo_csu_sss_lookup_tx_remote(RonaldoCSUSSS *s,
                                 RonaldoCSUSSSRemote rx_remote)
{
    uint32_t enc;
	if (rx_remote == NO_REMOTE) {
		return NO_REMOTE;
	}

    RonaldoCSUSSSRemote ret;

    for (ret = 0; ret < NUM_REMOTES; ++ret) {
        if (r_cfg_sss_shifts[ret] == -1) {
            /* This unit has no input. Ignore it.  */
            continue;
        }

        enc = extract32(s->regs[R_CFG], r_cfg_sss_shifts[ret],
                        R_CFG_SSS_LENGTH);
        if (r_cfg_sss_encodings[rx_remote] == enc) {
            break;
        }
    }
    return (ronaldo_csu_sss_population[ret] & (1 << rx_remote)) ?
                    ret : NO_REMOTE;
}
static bool
ronaldo_csu_sss_stream_can_push(StreamSlave *obj, StreamCanPushNotifyFn notify,
                                void *notify_opaque)
{
    RonaldoCSUSSSStream *ss = RONALDO_CSU_SSS_STREAM(obj);
    RonaldoCSUSSS *s = RONALDO_CSU_SSS(ss->sss);
    RonaldoCSUSSSRemote rx = ronaldo_csu_sss_lookup_rx_remote(s, ss);
    RonaldoCSUSSSRemote tx = ronaldo_csu_sss_lookup_tx_remote(s, rx);

    if (tx != NO_REMOTE && s->tx_devs[tx] &&
            stream_can_push(s->tx_devs[tx], notify, notify_opaque)) {
        return true;
    }

    s->notifys[rx] = notify;
    s->notify_opaques[rx] = notify_opaque;
    return false;
}



static size_t ronaldo_csu_sss_stream_push(StreamSlave *obj, uint8_t *buf,
                                          size_t len, uint32_t attr)
{
    RonaldoCSUSSSStream *ss = RONALDO_CSU_SSS_STREAM(obj);
    RonaldoCSUSSS *s = RONALDO_CSU_SSS(ss->sss);
    RonaldoCSUSSSRemote rx = ronaldo_csu_sss_lookup_rx_remote(s, ss);
    RonaldoCSUSSSRemote tx = ronaldo_csu_sss_lookup_tx_remote(s, rx);

    return (tx != NO_REMOTE) ? stream_push(s->tx_devs[tx], buf, len, attr) : 0;
}

static void r_cfg_post_write(RegisterInfo *reg, uint64_t val) {
    RonaldoCSUSSS *s = RONALDO_CSU_SSS(reg->opaque);

    ronaldo_csu_sss_notify_all(s);
}

static const RegisterAccessInfo ronaldo_csu_sss_regs_info[] = {
    [R_CFG] = { .name = "R_CFG", .ro = R_CFG_RSVD, .post_write = r_cfg_post_write },
};

static const MemoryRegionOps ronaldo_csu_sss_ops = {
    .read = register_read_memory_le,
    .write = register_write_memory_le,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void ronaldo_csu_sss_realize(DeviceState *dev, Error **errp)
{
    RonaldoCSUSSS *s = RONALDO_CSU_SSS(dev);
    Error *local_errp = NULL;
    RonaldoCSUSSSRemote r;
    const char *prefix = object_get_canonical_path(OBJECT(dev));
    int i;

    for (i = 0; i < R_MAX; ++i) {
        RegisterInfo *r = &s->regs_info[i];

        *r = (RegisterInfo) {
            .data = (uint8_t *)&s->regs[i],
            .data_size = sizeof(uint32_t),
            .access = &ronaldo_csu_sss_regs_info[i],
            .debug = RONALDO_CSU_SSS_ERR_DEBUG,
            .prefix = prefix,
            .opaque = s,
        };
        memory_region_init_io(&r->mem, OBJECT(dev), &ronaldo_csu_sss_ops, r,
                              "sss-regs", 4);
        memory_region_add_subregion(&s->iomem, i * 4, &r->mem);
    }

    for (r = 0; r < NUM_REMOTES; ++r) {
        RonaldoCSUSSSStream *ss = RONALDO_CSU_SSS_STREAM(&s->rx_devs[r]);

        object_property_add_link(OBJECT(ss), "sss", TYPE_RONALDO_CSU_SSS,
                                 (Object **)&ss->sss,
                                 qdev_prop_allow_set_link_before_realize,
                                 OBJ_PROP_LINK_UNREF_ON_RELEASE,
                                 &local_errp);
        if (local_errp) {
            goto ronaldo_csu_sss_realize_fail;
        }
        object_property_set_link(OBJECT(ss), OBJECT(s), "sss", &local_errp);
        if (local_errp) {
            goto ronaldo_csu_sss_realize_fail;
        }
    }
    return;

ronaldo_csu_sss_realize_fail:
    if (!*errp) {
        *errp = local_errp;
    }
}

static void ronaldo_csu_sss_init(Object *obj)
{
    RonaldoCSUSSS *s = RONALDO_CSU_SSS(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RonaldoCSUSSSRemote r;

    for (r = 0; r < NUM_REMOTES; ++r) {
        char *name = g_strdup_printf("stream-connected-%s",
                                     ronaldo_csu_sss_remote_names[r]);
        object_property_add_link(obj, name, TYPE_STREAM_SLAVE,
                                 (Object **)&s->tx_devs[r],
                                 qdev_prop_allow_set_link_before_realize,
                                 OBJ_PROP_LINK_UNREF_ON_RELEASE,
                                 NULL);
        g_free(name);
        name = g_strdup_printf("stream-connected-%s-target",
                               ronaldo_csu_sss_remote_names[r]);
        object_initialize(&s->rx_devs[r], sizeof(s->rx_devs[r]),
                          TYPE_RONALDO_CSU_SSS_STREAM);
        object_property_add_child(OBJECT(s), name, (Object *)&s->rx_devs[r],
                                  &error_abort);
        g_free(name);
    }

    memory_region_init_io(&s->iomem, obj, &ronaldo_csu_sss_ops, s,
                          "ronaldo.csu-stream-switch", R_MAX * 4);
    sysbus_init_mmio(sbd, &s->iomem);
}

/* FIXME: With no regs we are actually stateless. Although post load we need
 * to call notify() to start up the fire-hose of zeros again.
 */

static const VMStateDescription vmstate_ronaldo_csu_sss = {
    .name = "ronaldo_csu_sss",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, RonaldoCSUSSS, R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void ronaldo_csu_sss_stream_class_init(ObjectClass *klass, void *data)
{
    StreamSlaveClass *ssc = STREAM_SLAVE_CLASS(klass);

    ssc->push = ronaldo_csu_sss_stream_push;
    ssc->can_push = ronaldo_csu_sss_stream_can_push;
}

static void ronaldo_csu_sss_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = ronaldo_csu_sss_reset;
    dc->realize = ronaldo_csu_sss_realize;
    dc->vmsd = &vmstate_ronaldo_csu_sss;
}

static const TypeInfo ronaldo_csu_sss_info = {
    .name          = TYPE_RONALDO_CSU_SSS,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RonaldoCSUSSS),
    .class_init    = ronaldo_csu_sss_class_init,
    .instance_init = ronaldo_csu_sss_init,
};

static const TypeInfo ronaldo_csu_sss_stream_info = {
    .name          = TYPE_RONALDO_CSU_SSS_STREAM,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(RonaldoCSUSSSStream),
    .class_init    = ronaldo_csu_sss_stream_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_STREAM_SLAVE },
        { }
    }
};

static void ronaldo_csu_sss_register_types(void)
{
    type_register_static(&ronaldo_csu_sss_info);
    type_register_static(&ronaldo_csu_sss_stream_info);
}

type_init(ronaldo_csu_sss_register_types)
