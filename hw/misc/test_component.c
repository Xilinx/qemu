/*
 * Implementation of irq test component, used to emulate interrupt generation
 * capabilities for testing purposes.
 *
 * 2015 Aggios, Inc.
 *
 * Written by Strahinja Jankovic <strahinja.jankovic@aggios.com>
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
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

#ifndef IRQ_TEST_COMPONENT_ERR_DEBUG
#define IRQ_TEST_COMPONENT_ERR_DEBUG 0
#endif

#define TYPE_IRQ_TEST_COMPONENT "qemu.irq-test-component"

#define IRQ_TEST_COMPONENT(obj) \
     OBJECT_CHECK(IRQTestComponent, (obj), TYPE_IRQ_TEST_COMPONENT)

REG32(CONFIG, 0x0)
REG32(STATUS, 0x4)
    FIELD(STATUS, POWER, 0, 1)
    FIELD(STATUS, HALT, 1, 1)
REG32(DATA, 0x8)
REG32(IRQ_STATUS, 0x10)
    FIELD(IRQ_STATUS, IRQ0, 0, 1)
REG32(IRQ_MASK, 0x14)
    FIELD(IRQ_MASK, IRQ0, 0, 1)
REG32(IRQ_ENABLE, 0x18)
    FIELD(IRQ_ENABLE, IRQ0, 0, 1)
REG32(IRQ_DISABLE, 0x1c)
    FIELD(IRQ_DISABLE, IRQ0, 0, 1)
REG32(IRQ_TRIGGER, 0x20)
    FIELD(IRQ_TRIGGER, IRQ0, 0, 1)

#define R_MAX ((R_IRQ_TRIGGER) + 1)

typedef struct IRQTestComponent {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    qemu_irq irq;
    qemu_irq pmu_wake;

    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];
} IRQTestComponent;

static void itc_update_irq(void *opaque)
{
    IRQTestComponent *s = IRQ_TEST_COMPONENT(opaque);
    bool pending = s->regs[R_IRQ_STATUS] & (~s->regs[R_IRQ_MASK]) &
                    R_IRQ_STATUS_IRQ0_MASK;

    qemu_set_irq(s->irq, pending);
    qemu_set_irq(s->pmu_wake, pending);
}

/* Used to trigger irq from qtest. */
static void itc_generate_irq(void *opaque, int n, int level)
{
    IRQTestComponent *s = IRQ_TEST_COMPONENT(opaque);

    s->regs[R_IRQ_STATUS] &= ~R_IRQ_STATUS_IRQ0_MASK;
    s->regs[R_IRQ_STATUS] |= level;

    itc_update_irq(s);
}

/* Generate status read. */

static uint64_t itc_status_postr(RegisterInfo *reg, uint64_t val64)
{
    IRQTestComponent *s = IRQ_TEST_COMPONENT(reg->opaque);
    DeviceState *dev = DEVICE(s);
    uint32_t tmp = val64;

    /* Update pwrstat with current power and halt status */
    tmp = deposit32(tmp, R_STATUS_POWER_SHIFT, 1, dev->ps.power);
    tmp = deposit32(tmp, R_STATUS_HALT_SHIFT, 1, dev->ps.halt);

    return tmp;
}

static void itc_irq_status_postw(RegisterInfo *reg, uint64_t val64)
{
    IRQTestComponent *s = IRQ_TEST_COMPONENT(reg->opaque);

    itc_update_irq(s);
}

static void itc_irq_enable_postw(RegisterInfo *reg, uint64_t val64)
{
    IRQTestComponent *s = IRQ_TEST_COMPONENT(reg->opaque);
    uint32_t val = val64;

    s->regs[R_IRQ_MASK] &= ~val;
    itc_update_irq(s);
}

static void itc_irq_disable_postw(RegisterInfo *reg, uint64_t val64)
{
    IRQTestComponent *s = IRQ_TEST_COMPONENT(reg->opaque);
    uint32_t val = val64;

    s->regs[R_IRQ_MASK] |= val;
    itc_update_irq(s);
}

static void itc_irq_trigger_postw(RegisterInfo *reg, uint64_t val64)
{
    IRQTestComponent *s = IRQ_TEST_COMPONENT(reg->opaque);
    uint32_t val = val64;

    s->regs[R_IRQ_STATUS] |= val;
    itc_update_irq(s);
}

static RegisterAccessInfo irq_test_comp_regs_info[] = {
    {   .name = "CONFIG",  .addr = A_CONFIG,
        .rsvd = 0xffffffff,
    },{   .name = "STATUS",  .addr = A_STATUS,
        .rsvd = 0xfffffffe,
        .ro = 0xfffffffe,
        .post_read = itc_status_postr,
    },{   .name = "DATA",  .addr = A_DATA,
    },{   .name = "IRQ_STATUS",  .addr = A_IRQ_STATUS,
        .w1c = 0xffffffff,
        .rsvd = 0xfffffffe,
        .post_write = itc_irq_status_postw,
    },{   .name = "IRQ_MASK",  .addr = A_IRQ_MASK,
        .ro = 0xffffffff,
        .rsvd = 0xfffffffe,
        .reset = 0xffffffff,
    },{   .name = "IRQ_ENABLE",  .addr = A_IRQ_ENABLE,
        .rsvd = 0xfffffffe,
        .post_write = itc_irq_enable_postw,
    },{   .name = "IRQ_DISABLE",  .addr = A_IRQ_DISABLE,
        .rsvd = 0xfffffffe,
        .post_write = itc_irq_disable_postw,
    },{   .name = "IRQ_TRIGGER",  .addr = A_IRQ_TRIGGER,
        .rsvd = 0xfffffffe,
        .post_write = itc_irq_trigger_postw,
    }
};

static void irq_test_comp_reset(DeviceState *dev)
{
    IRQTestComponent *s = IRQ_TEST_COMPONENT(dev);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }
    itc_update_irq(s);
}

static const MemoryRegionOps irq_test_comp_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void irq_test_comp_init(Object *obj)
{
    IRQTestComponent *s = IRQ_TEST_COMPONENT(obj);
    DeviceState *dev = DEVICE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    RegisterInfoArray *reg_array;

    memory_region_init(&s->iomem, obj, TYPE_IRQ_TEST_COMPONENT, R_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), irq_test_comp_regs_info,
                              ARRAY_SIZE(irq_test_comp_regs_info),
                              s->regs_info, s->regs,
                              &irq_test_comp_ops,
                              IRQ_TEST_COMPONENT_ERR_DEBUG,
                              R_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                0x0,
                                &reg_array->mem);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    /* Pin used from qtest to trigger interrupt. */
    qdev_init_gpio_in(dev, itc_generate_irq, 1);
    /* Pin used to signal wakeup request to PMU */
    qdev_init_gpio_out_named(dev, &s->pmu_wake, "wake", 1);
}

static const VMStateDescription vmstate_irq_test_comp = {
    .name = TYPE_IRQ_TEST_COMPONENT,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, IRQTestComponent, R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void irq_test_comp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = irq_test_comp_reset;
    dc->vmsd = &vmstate_irq_test_comp;
}

static const TypeInfo irq_test_comp_info = {
    .name          = TYPE_IRQ_TEST_COMPONENT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IRQTestComponent),
    .instance_init = irq_test_comp_init,
    .class_init    = irq_test_comp_class_init,
};

static void irq_test_comp_register_types(void)
{
    type_register_static(&irq_test_comp_info);
}

type_init(irq_test_comp_register_types)
