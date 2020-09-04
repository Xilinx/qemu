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
#include "hw/register.h"
#include "hw/irq.h"
#include "qemu/timer.h"
#include "qemu/bitops.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/fdt_generic_util.h"

#include "qapi/qmp/qerror.h"

#ifndef XLNX_SWDT_ERR_DEBUG
#define XLNX_SWDT_ERR_DEBUG 0
#endif

#define TYPE_XLNX_SWDT "xlnx.swdt"

#define XLNX_SWDT(obj) \
     OBJECT_CHECK(SWDTState, (obj), TYPE_XLNX_SWDT)

REG32(MODE, 0x0)
    FIELD(MODE, ZKEY, 12, 12)
    FIELD(MODE, IRQLN, 7, 2)
    FIELD(MODE, RSTLN, 4, 3)
    FIELD(MODE, IRQEN, 2, 1)
    FIELD(MODE, RSTEN, 1, 1)
    FIELD(MODE, WDEN, 0, 1)
REG32(CONTROL, 0x4)
    FIELD(CONTROL, CKEY, 14, 12)
    FIELD(CONTROL, CRV, 2, 12)
    FIELD(CONTROL, CLKSEL, 0, 2)
REG32(RESTART, 0x8)
    FIELD(RESTART, RSTKEY, 0, 16)
REG32(STATUS, 0xc)
    FIELD(STATUS, WDZ, 0, 1)

#define SWDT_R_MAX (R_STATUS + 1)

typedef struct SWDTState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;
    qemu_irq rst;
    qemu_irq wdt_timeout_irq;
    QEMUTimer *timer;
    /* model the irq and rst line high time. */
    QEMUTimer *irq_done_timer;
    QEMUTimer *rst_done_timer;

    uint64_t pclk;
    uint32_t current_mode;
    uint32_t current_control;
    uint32_t regs[SWDT_R_MAX];
    RegisterInfo regs_info[SWDT_R_MAX];
} SWDTState;

static void swdt_done_irq_update(SWDTState *s)
{
    uint64_t irqln = muldiv64(1000000000,
                              4 << ARRAY_FIELD_EX32(s->regs, MODE, IRQLN),
                              s->pclk);

    qemu_set_irq(s->irq, 1);
    timer_mod(s->irq_done_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + irqln);
}

static void swdt_reset_irq_update(SWDTState *s)
{
    uint64_t rstln = muldiv64(1000000000,
                              2 << ARRAY_FIELD_EX32(s->regs, MODE, RSTLN),
                              s->pclk);

    qemu_set_irq(s->rst, 1);
    timer_mod(s->rst_done_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + rstln);
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
    bool do_a_reset = ARRAY_FIELD_EX32(s->regs, MODE, RSTEN);
    bool do_an_irq = ARRAY_FIELD_EX32(s->regs, MODE, IRQEN);

    s->regs[R_STATUS] = 1;
    qemu_set_irq(s->wdt_timeout_irq, 1);

    if (do_a_reset) {
        swdt_reset_irq_update(s);
    }
    if (do_an_irq) {
        swdt_done_irq_update(s);
    }
}

static uint32_t swdt_reload_value(SWDTState *s)
{
    return (ARRAY_FIELD_EX32(s->regs, CONTROL, CRV) << 12) + 0xFFF;
}

static uint64_t swdt_next_trigger(SWDTState *s)
{
    uint64_t clksel = muldiv64(1000000000,
                               8 << (3 * ARRAY_FIELD_EX32(s->regs,
                                                          CONTROL, CLKSEL)),
                               s->pclk);

    return (clksel * swdt_reload_value(s)) +
               qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

/* Reload the counter, mod the timer. */
static void swdt_counter_reload(SWDTState *s)
{
    bool watchdog_enabled = ARRAY_FIELD_EX32(s->regs, MODE, WDEN);

    if (watchdog_enabled) {
        s->regs[R_STATUS] = 0;
        timer_mod(s->timer, swdt_next_trigger(s));
    } else {
        timer_del(s->timer);
    }
}

static void swdt_mode_postw(RegisterInfo *reg, uint64_t val64)
{
    SWDTState *s = XLNX_SWDT(reg->opaque);
    bool valid = (ARRAY_FIELD_EX32(s->regs, MODE, ZKEY) == 0xABC);

    if (!valid) {
        /* The write is not valid, just restore the old value of the register.
         */
        s->regs[R_MODE] = s->current_mode;
        return;
    }
    /* Backup the mode in case a non valid write happens. */
    s->current_mode = s->regs[R_MODE];

    swdt_counter_reload(s);
}

static uint64_t swdt_mode_postr(RegisterInfo *reg, uint64_t val)
{
    SWDTState *s = XLNX_SWDT(reg->opaque);

    /* The ZKEY is write only */
    return s->regs[R_MODE] & ~R_MODE_ZKEY_MASK;
}

static void swdt_control_postw(RegisterInfo *reg, uint64_t val64)
{
    SWDTState *s = XLNX_SWDT(reg->opaque);
    bool valid = (ARRAY_FIELD_EX32(s->regs, CONTROL, CKEY) == 0x248);

    if (!valid) {
        /* The write is not valid, just restore the old value of the register.
         */
        s->regs[R_CONTROL] = s->current_control;
        return;
    }

    /* Backup the mode in case a non valid write happens. */
    s->current_control = s->regs[R_CONTROL];
}

static void swdt_restart_key_postw(RegisterInfo *reg, uint64_t val64)
{
    SWDTState *s = XLNX_SWDT(reg->opaque);
    bool valid = (ARRAY_FIELD_EX32(s->regs, RESTART, RSTKEY) == 0x1999);

    if (valid) {
        swdt_counter_reload(s);
    }

    /* Read as 0 (but we probably don't care). */
    s->regs[R_RESTART] = 0x0000;
}

static const RegisterAccessInfo swdt_regs_info[] = {
    {   .name = "MODE",  .addr = A_MODE,
        .reset = 0x1c2,
        .rsvd = 0xe08,
        .post_write = swdt_mode_postw,
        .post_read = swdt_mode_postr,
    },{ .name = "CONTROL",  .addr = A_CONTROL,
        .reset = 0x3ffc,
        .post_write = swdt_control_postw,
    },{ .name = "RESTART",  .addr = A_RESTART,
        .post_write = swdt_restart_key_postw,
    },{ .name = "STATUS",  .addr = A_STATUS,
        .ro = 0x1,
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
        register_reset(&s->regs_info[i]);
    }

    swdt_counter_reload(s);
    swdt_irq_done(s);
    swdt_reset_done(s);
}

static const MemoryRegionOps swdt_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void swdt_realize(DeviceState *dev, Error **errp)
{
    SWDTState *s = XLNX_SWDT(dev);

    if (!s->pclk) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND, "pclk");
        return;
    }
}

static void swdt_init(Object *obj)
{
    SWDTState *s = XLNX_SWDT(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    memory_region_init(&s->iomem, obj, TYPE_XLNX_SWDT, SWDT_R_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), swdt_regs_info,
                              ARRAY_SIZE(swdt_regs_info),
                              s->regs_info, s->regs,
                              &swdt_ops,
                              XLNX_SWDT_ERR_DEBUG,
                              SWDT_R_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                0x0,
                                &reg_array->mem);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    qdev_init_gpio_out_named(DEVICE(obj), &s->wdt_timeout_irq,
                             "wdt_timeout_error_out", 1);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, swdt_time_elapsed, s);
    s->irq_done_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, swdt_irq_done, s);
    s->rst_done_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, swdt_reset_done, s);
}

static Property swdt_properties[] = {
    /* pclk in Hz */
    DEFINE_PROP_UINT64("pclk", SWDTState, pclk, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static const FDTGenericGPIOSet wdt_client_gpios[] = {
    {
        .names = &fdt_generic_gpio_name_set_gpio,
        .gpios = (FDTGenericGPIOConnection[]) {
            { .name = "wdt_timeout_error_out", .fdt_index = 0, .range = 1 },
            { },
        }
    },
    { },
};

static const VMStateDescription vmstate_swdt = {
    .name = TYPE_XLNX_SWDT,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, SWDTState, SWDT_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void swdt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    FDTGenericGPIOClass *fggc = FDT_GENERIC_GPIO_CLASS(klass);

    dc->reset = swdt_reset;
    dc->realize = swdt_realize;
    dc->vmsd = &vmstate_swdt;
    device_class_set_props(dc, swdt_properties);
    fggc->client_gpios = wdt_client_gpios;
}

static const TypeInfo swdt_info = {
    .name          = TYPE_XLNX_SWDT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SWDTState),
    .class_init    = swdt_class_init,
    .instance_init = swdt_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_FDT_GENERIC_GPIO },
        { }
    },
};

static void swdt_register_types(void)
{
    type_register_static(&swdt_info);
}

type_init(swdt_register_types)
