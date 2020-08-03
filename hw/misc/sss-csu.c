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
#include "hw/misc/sss.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/register.h"
#include "hw/misc/sss.h"

#ifndef ZYNQMP_CSU_SSS_ERR_DEBUG
#define ZYNQMP_CSU_SSS_ERR_DEBUG 0
#endif

#define TYPE_ZYNQMP_CSU_SSS "zynqmp.csu-sss"

#define ZYNQMP_CSU_SSS(obj) \
     OBJECT_CHECK(ZynqMPCSUSSS, (obj), TYPE_ZYNQMP_CSU_SSS)

REG32(CFG, 0x00)
#define R_MAX (R_CFG + 1)
#define R_CSU_SSS_FIELD_LENGTH 4

typedef enum {
    DMA,
    AES,
    SHA,
    PCAP,
    PSTP,
    ROM, /* FIXME: ROM, may have no software visibility - delete? */
    CSU_NUM_REMOTES
} CSUSSSRemote;

#define NO_REMOTE CSU_NUM_REMOTES

static const char *zynqmp_csu_sss_remote_names[] = {
    [DMA] = "dma",
    [AES] = "aes",
    [SHA] = "sha",
    [PCAP] = "pcap",
    [PSTP] = "pstp",
    [ROM] = "rom",
    /* FIXME: Add TMR */
};

static const uint32_t zynqmp_csu_sss_population[] = {
    [PCAP] = (1 << DMA) | (1 << AES) | (1 << PSTP),
    [DMA] = (1 << DMA) | (1 << AES) | (1 << PCAP) | (1 << PSTP),
    [AES] = (1 << DMA),
    [SHA] = (1 << DMA) | (1 << ROM),
    [PSTP] = (1 << PCAP),
    [NO_REMOTE] = 0,
};

static const int r_csu_cfg_sss_shifts[] = {
    [PCAP] = 0,
    [DMA] = 4,
    [AES] = 8,
    [SHA] = 12,
    [PSTP] = 16,
    [ROM] = -1,
};

static const uint8_t r_csu_cfg_sss_encodings[] = {
    [PCAP] = 0x3,
    [DMA] = 0x5,
    [AES] = 0xa,
    [SHA] = 0,
    [PSTP] = 0xc,
    [ROM] = 0x0,
};

typedef struct ZynqMPCSUSSS ZynqMPCSUSSS;

struct ZynqMPCSUSSS {
    SSSBase parent;
    MemoryRegion iomem;

    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];
};

static uint32_t zynqmp_csu_get_sss_regfield(SSSBase *p, int remote)
{
    ZynqMPCSUSSS *s = ZYNQMP_CSU_SSS(p);

    return extract32(s->regs[R_CFG], r_csu_cfg_sss_shifts[remote],
                      R_CSU_SSS_FIELD_LENGTH);
}

static void r_cfg_post_write(RegisterInfo *reg, uint64_t val)
{
    SSSBase *s = SSS_BASE(reg->opaque);

    sss_notify_all(s);
}

static const RegisterAccessInfo zynqmp_csu_sss_regs_info[] = {
    { .name = "R_CFG", .addr = A_CFG,
      .ro = 0xFFF00000,
      .post_write = r_cfg_post_write },
};

static const MemoryRegionOps sss_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void zynqmp_csu_sss_realize(DeviceState *dev, Error **errp)
{
    ZynqMPCSUSSS *s = ZYNQMP_CSU_SSS(dev);
    SSSBase *p = SSS_BASE(dev);
    Error *local_errp = NULL;
    int r;

    for (r = 0; r < NO_REMOTE; ++r) {
        SSSStream *ss = SSS_STREAM(&p->rx_devs[r]);

        object_property_add_link(OBJECT(ss), "sss", TYPE_ZYNQMP_CSU_SSS,
                             (Object **)&ss->sss,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
        if (local_errp) {
            goto zynqmp_csu_sss_realize_fail;
        }
        object_property_set_link(OBJECT(ss), "sss", OBJECT(s), &local_errp);
        if (local_errp) {
            goto zynqmp_csu_sss_realize_fail;
        }
        object_property_set_bool(OBJECT(ss), "realized", true, &error_fatal);
    }
    return;

zynqmp_csu_sss_realize_fail:
    if (!*errp) {
        *errp = local_errp;
    }


}

static void sss_reset(DeviceState *dev)
{
    ZynqMPCSUSSS *s = ZYNQMP_CSU_SSS(dev);
    SSSBase *p = SSS_BASE(dev);
    int i;

    for (i = 0; i < R_MAX; ++i) {
        register_reset(&s->regs_info[i]);
    }
    sss_notify_all(p);
}

static void zynqmp_csu_sss_init(Object *obj)
{
    SSSBase *p = SSS_BASE(obj);
    ZynqMPCSUSSS *s = ZYNQMP_CSU_SSS(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;
    char *name;
    int remote;

    p->sss_population = zynqmp_csu_sss_population;
    p->r_sss_shifts = r_csu_cfg_sss_shifts;
    p->r_sss_encodings = r_csu_cfg_sss_encodings;
    p->num_remotes = CSU_NUM_REMOTES;
    p->notifys = g_new0(StreamCanPushNotifyFn, CSU_NUM_REMOTES);
    p->notify_opaques = g_new0(void *, CSU_NUM_REMOTES);
    p->get_sss_regfield = zynqmp_csu_get_sss_regfield;

    p->rx_devs = (SSSStream *) g_new(SSSStream, CSU_NUM_REMOTES);
    p->tx_devs = (StreamSlave **) g_new0(StreamSlave *, CSU_NUM_REMOTES);

    for (remote = 0 ; remote != NO_REMOTE; remote++) {
        name = g_strdup_printf("stream-connected-%s",
                               zynqmp_csu_sss_remote_names[remote]);
        object_property_add_link(OBJECT(s), name, TYPE_STREAM_SLAVE,
                             (Object **)&p->tx_devs[remote],
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
        g_free(name);
        object_initialize(&p->rx_devs[remote], sizeof(SSSStream),
                          TYPE_SSS_STREAM);
        name = g_strdup_printf("stream-connected-%s-target",
                               zynqmp_csu_sss_remote_names[remote]);
        object_property_add_child(OBJECT(s), name,
                                 (Object *)&p->rx_devs[remote]);
        g_free(name);
    }

    memory_region_init(&s->iomem, obj, TYPE_ZYNQMP_CSU_SSS, R_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), zynqmp_csu_sss_regs_info,
                              ARRAY_SIZE(zynqmp_csu_sss_regs_info),
                              s->regs_info, s->regs,
                              &sss_ops,
                              ZYNQMP_CSU_SSS_ERR_DEBUG,
                              R_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                0x0,
                                &reg_array->mem);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_zynqmp_csu_sss = {
    .name = "zynqmp_csu_sss",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, ZynqMPCSUSSS, R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void csu_sss_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = sss_reset;
    dc->realize = zynqmp_csu_sss_realize;
    dc->vmsd = &vmstate_zynqmp_csu_sss;
}

static const TypeInfo zynqmp_csu_sss_info = {
    .name          = TYPE_ZYNQMP_CSU_SSS,
    .parent        = TYPE_SSS_BASE,
    .instance_size = sizeof(ZynqMPCSUSSS),
    .class_init    = csu_sss_class_init,
    .instance_init = zynqmp_csu_sss_init,
};

static void sss_register_types(void)
{
    type_register_static(&zynqmp_csu_sss_info);
}

type_init(sss_register_types)
