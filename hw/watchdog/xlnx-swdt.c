/*
 * QEMU model of the SWDT
 *
 * Copyright (c) 2016 Xilinx Inc.
 *
 * Written by Konrad Frederic <fred.konrad@greensocs.com>
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
#include "hw/register-dep.h"
#include "qemu/timer.h"
#include "qemu/bitops.h"
#include "qapi/error.h"
#include "qemu/log.h"

#include "qapi/qmp/qerror.h"

#ifndef XLNX_SWDT_ERR_DEBUG
#define XLNX_SWDT_ERR_DEBUG 0
#endif

#define TYPE_XLNX_SWDT "xlnx.swdt"

#define XLNX_SWDT(obj) \
     OBJECT_CHECK(SWDTState, (obj), TYPE_XLNX_SWDT)

DEP_REG32(SWDT_MODE, 0x0)
    DEP_FIELD(SWDT_MODE, ZKEY, 11, 12)
    DEP_FIELD(SWDT_MODE, IRQLN, 2, 7)
    DEP_FIELD(SWDT_MODE, RSTLN, 3, 4)
    DEP_FIELD(SWDT_MODE, IRQEN, 1, 2)
    DEP_FIELD(SWDT_MODE, RSTEN, 1, 1)
    DEP_FIELD(SWDT_MODE, WDEN, 1, 0)
DEP_REG32(SWDT_CONTROL, 0x4)
    DEP_FIELD(SWDT_CONTROL, CKEY, 12, 14)
    DEP_FIELD(SWDT_CONTROL, CRV, 12, 2)
    DEP_FIELD(SWDT_CONTROL, CLKSEL, 2, 0)
DEP_REG32(SWDT_RESTART, 0x8)
    DEP_FIELD(SWDT_RESTART, RSTKEY, 16, 0)
DEP_REG32(SWDT_STATUS, 0xC)
    DEP_FIELD(SWDT_STATUS, WDZ, 1, 0)

#define R_MAX (R_SWDT_STATUS + 1)

typedef struct SWDTState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;
    qemu_irq rst;
    QEMUTimer *timer;
    /* model the irq and rst line high time. */
    QEMUTimer *irq_done_timer;
    QEMUTimer *rst_done_timer;

    uint64_t pclk;
    uint32_t current_mode;
    uint32_t current_control;
    uint32_t regs[R_MAX];
    DepRegisterInfo regs_info[R_MAX];
} SWDTState;

static void swdt_done_irq_update(SWDTState *s)
{
    qemu_set_irq(s->irq, 1);
    timer_mod(s->irq_done_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)
                                 + muldiv64(1000000000,
                                      4 << DEP_AF_EX32(s->regs, SWDT_MODE, IRQLN),
                                      s->pclk));
}

static void swdt_reset_irq_update(SWDTState *s)
{
    qemu_set_irq(s->rst, 1);
    timer_mod(s->rst_done_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)
                           + muldiv64(1000000000,
                                      2 << DEP_AF_EX32(s->regs, SWDT_MODE, RSTLN),
                                      s->pclk));
}

static void swdt_irq_done(void *opaque)
{
    SWDTState *s = XLNX_SWDT(opaque);

    qemu_set_irq(s->irq, 0);
}

static void swdt_reset_done(void *opaque)
{
    SWDTState *s = XLNX_SWDT(opaque);

    qemu_set_irq(s->rst, 0);
}

static void swdt_time_elapsed(void *opaque)
{
    SWDTState *s = XLNX_SWDT(opaque);
    bool do_a_reset = DEP_AF_EX32(s->regs, SWDT_MODE, RSTEN);
    bool do_an_irq = DEP_AF_EX32(s->regs, SWDT_MODE, IRQEN);

    s->regs[R_SWDT_STATUS] = 1;

    if (do_a_reset) {
        swdt_reset_irq_update(s);
    }
    if (do_an_irq) {
        swdt_done_irq_update(s);
    }
}

static uint32_t swdt_reload_value(SWDTState *s)
{
    return (DEP_AF_EX32(s->regs, SWDT_CONTROL, CRV) << 12) + 0xFFF;
}

static uint64_t swdt_next_trigger(SWDTState *s)
{
    return (muldiv64(1000000000,
                     8 << (3 * DEP_AF_EX32(s->regs, SWDT_CONTROL, CLKSEL)), s->pclk)
           * swdt_reload_value(s)) + qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

/* Reload the counter, mod the timer. */
static void swdt_counter_reload(SWDTState *s)
{
    bool watchdog_enabled = DEP_AF_EX32(s->regs, SWDT_MODE, WDEN);

    if (watchdog_enabled) {
        s->regs[R_SWDT_STATUS] = 0;
        timer_mod(s->timer, swdt_next_trigger(s));
    } else {
        timer_del(s->timer);
    }
}

static void swdt_mode_postw(DepRegisterInfo *reg, uint64_t val64)
{
    SWDTState *s = XLNX_SWDT(reg->opaque);
    bool valid = (DEP_AF_EX32(s->regs, SWDT_MODE, ZKEY) == 0xABC);

    if (!valid) {
        /* The write is not valid, just restore the old value of the register.
         */
        s->regs[R_SWDT_MODE] = s->current_mode;
        return;
    }
    /* Backup the mode in case a non valid write happens. */
    s->current_mode = s->regs[R_SWDT_MODE];

    swdt_counter_reload(s);
}

static void swdt_control_postw(DepRegisterInfo *reg, uint64_t val64)
{
    SWDTState *s = XLNX_SWDT(reg->opaque);
    bool valid = (DEP_AF_EX32(s->regs, SWDT_CONTROL, CKEY) == 0x248);

    if (!valid) {
        /* The write is not valid, just restore the old value of the register.
         */
        s->regs[R_SWDT_CONTROL] = s->current_control;
        return;
    }
    /* Backup the mode in case a non valid write happens. */
    s->current_control = s->regs[R_SWDT_CONTROL];
}

static void swdt_restart_key_postw(DepRegisterInfo *reg, uint64_t val64)
{
    SWDTState *s = XLNX_SWDT(reg->opaque);
    bool valid = (DEP_AF_EX32(s->regs, SWDT_RESTART, RSTKEY) == 0x1999);

    if (valid) {
        swdt_counter_reload(s);
    }

    /* Read as 0 (but we probably don't care). */
    s->regs[R_SWDT_RESTART] = 0x0000;
}

static DepRegisterAccessInfo swdt_regs_info[] = {
    {   .name = "SWDT_MODE",  .decode.addr = A_SWDT_MODE,
        .reset = 0x000001C2,
        .rsvd = 0x00000E08,
        .ro = 0x00000E08,
        .post_write = swdt_mode_postw,
    },{ .name = "SWDT_CONTROL",  .decode.addr = A_SWDT_CONTROL,
        .reset = 0x00003FFC,
        .post_write = swdt_control_postw,
    },{ .name = "SWDT_RESTART",  .decode.addr = A_SWDT_RESTART,
        .reset = 0x00000000,
        .post_write = swdt_restart_key_postw,
    },{ .name = "SWDT_STATUS",  .decode.addr = A_SWDT_STATUS,
        .reset = 0x00000000,
        .ro = 0x00000001
    }
};

static void swdt_reset(DeviceState *dev)
{
    SWDTState *s = XLNX_SWDT(dev);
    unsigned int i;

    /* The reset value in the registers are ok but don't have the key.
     * so the write will be invalid and we need to set the backup value
     * to the init value.
     */
    s->current_mode = 0x000001C2;
    s->current_control = 0x00003FFC;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        dep_register_reset(&s->regs_info[i]);
    }

    swdt_counter_reload(s);
    swdt_irq_done(s);
    swdt_reset_done(s);
}

static const MemoryRegionOps swdt_ops = {
    .read = dep_register_read_memory_le,
    .write = dep_register_write_memory_le,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void swdt_realize(DeviceState *dev, Error **errp)
{
    SWDTState *s = XLNX_SWDT(dev);
    const char *prefix = object_get_canonical_path(OBJECT(dev));
    unsigned int i;

    if (!s->pclk) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND, "pclk");
        return;
    }

    for (i = 0; i < ARRAY_SIZE(swdt_regs_info); ++i) {
        DepRegisterInfo *r = &s->regs_info[i];

        *r = (DepRegisterInfo) {
            .data = (uint8_t *)&s->regs[swdt_regs_info[i].decode.addr / 4],
            .data_size = sizeof(uint32_t),
            .access = &swdt_regs_info[i],
            .debug = XLNX_SWDT_ERR_DEBUG,
            .prefix = prefix,
            .opaque = s,
        };
        memory_region_init_io(&r->mem, OBJECT(dev), &swdt_ops, r,
                              r->access->name, 4);
        memory_region_add_subregion(&s->iomem, r->access->decode.addr, &r->mem);
    }
}

static void swdt_init(Object *obj)
{
    SWDTState *s = XLNX_SWDT(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init(&s->iomem, obj, TYPE_XLNX_SWDT, R_MAX * 4);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, swdt_time_elapsed, s);
    s->irq_done_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, swdt_irq_done, s);
    s->rst_done_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, swdt_reset_done, s);
}

static Property swdt_properties[] = {
    /* pclk in Hz */
    DEFINE_PROP_UINT64("pclk", SWDTState, pclk, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_swdt = {
    .name = TYPE_XLNX_SWDT,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, SWDTState, R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void swdt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = swdt_reset;
    dc->realize = swdt_realize;
    dc->vmsd = &vmstate_swdt;
    dc->props = swdt_properties;
}

static const TypeInfo swdt_info = {
    .name          = TYPE_XLNX_SWDT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SWDTState),
    .class_init    = swdt_class_init,
    .instance_init = swdt_init,
};

static void swdt_register_types(void)
{
    type_register_static(&swdt_info);
}

type_init(swdt_register_types)
