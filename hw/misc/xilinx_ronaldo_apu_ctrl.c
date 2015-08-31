/*
 * QEMU model of Ronaldo APU Core Functionality
 *
 * For the most part, a dummy device model.
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

#include "qemu/bitops.h"
#include "qapi/qmp/qerror.h"
#include "hw/register.h"
#include "cpu-qom.h"
#include "hw/fdt_generic_util.h"

#ifndef RONALDO_APU_ERR_DEBUG
#define RONALDO_APU_ERR_DEBUG 0
#endif

#define TYPE_RONALDO_APU "xlnx.apu"

#define RONALDO_APU(obj) \
     OBJECT_CHECK(RonaldoAPU, (obj), TYPE_RONALDO_APU)

#ifndef XILINX_RONALDO_APU_ERR_DEBUG
#define XILINX_RONALDO_APU_ERR_DEBUG 0
#endif

#define DB_PRINT_L(lvl, fmt, args...) do {\
    if (XILINX_RONALDO_APU_ERR_DEBUG >= lvl) {\
        qemu_log(TYPE_RONALDO_APU ": %s:" fmt, __func__, ## args);\
    } \
} while (0);

#define DB_PRINT(fmt, args...) DB_PRINT_L(1, fmt, ## args)

REG32(RVBARADDR0L, 0x40)
REG32(RVBARADDR0H, 0x44)
REG32(RVBARADDR1L, 0x48)
REG32(RVBARADDR1H, 0x4c)
REG32(RVBARADDR2L, 0x50)
REG32(RVBARADDR2H, 0x54)
REG32(RVBARADDR3L, 0x58)
REG32(RVBARADDR3H, 0x5c)
REG32(PWRCTL, 0x90)
    FIELD(PWRCTL, CPUPWRDWNREQ, 3, 0)

#define R_MAX ((R_PWRCTL) + 1)

#define NUM_CPUS 4

typedef struct RonaldoAPU RonaldoAPU;

struct RonaldoAPU {
    SysBusDevice busdev;
    MemoryRegion iomem;

    ARMCPU *cpus[NUM_CPUS];
    /* WFIs towards PMU. */
    qemu_irq wfi_out[4];
    /* CPU Power status towards INTC Redirect. */
    qemu_irq cpu_power_status[4];

    uint8_t cpu_pwrdwn_req;
    uint8_t cpu_in_wfi;

    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];
};

static void update_wfi_out(void *opaque)
{
    RonaldoAPU *s = RONALDO_APU(opaque);
    unsigned int i, wfi_pending;

    wfi_pending = s->cpu_pwrdwn_req & s->cpu_in_wfi;
    for (i = 0; i < NUM_CPUS; i++) {
        qemu_set_irq(s->wfi_out[i], !!(wfi_pending & (1 << i)));
        /* Redirect interrutps only if planning to suspend and
         * wfi 0->1. This way, if interrupts are disabled, but
         * arrive, wfi will be skipped.
         */
        if (wfi_pending & (1 << i)) {
            qemu_set_irq(s->cpu_power_status[i], 1);
        }
    }
}

static void ronaldo_apu_reset(DeviceState *dev)
{
    RonaldoAPU *s = RONALDO_APU(dev);
    int i;
 
    for (i = 0; i < R_MAX; ++i) {
        register_reset(&s->regs_info[i]);
    }

    s->cpu_pwrdwn_req = 0;
    s->cpu_in_wfi = 0;
    update_wfi_out(s);
}

static void ronaldo_apu_rvbar_post_write(RegisterInfo *reg, uint64_t val)
{
    RonaldoAPU *s = RONALDO_APU(reg->opaque);
    int i;

    for (i = 0; i < NUM_CPUS; ++i) {
        uint64_t rvbar = s->regs[R_RVBARADDR0L + 2 * i] +
                         ((uint64_t)s->regs[R_RVBARADDR0H + 2 * i] << 32);
        object_property_set_int(OBJECT(s->cpus[i]), rvbar, "rvbar",
                                &error_abort);
        DB_PRINT("Set RVBAR %d to %" PRIx64 "\n", i, rvbar);
    }
}

static void ronaldo_apu_pwrctl_post_write(RegisterInfo *reg, uint64_t val)
{
    RonaldoAPU *s = RONALDO_APU(reg->opaque);
    unsigned int i, new;

    for (i = 0; i < NUM_CPUS; i++) {
        new = val & (1 << i);
        /* Check if CPU Power status has changed. If yes, update
         * GPIOs.
         */
        if (!new && (s->cpu_pwrdwn_req & (1 << i))) {
            qemu_set_irq(s->cpu_power_status[i], 0);
        }
        s->cpu_pwrdwn_req &= ~(1 << i);
        s->cpu_pwrdwn_req |= new;
    }
    update_wfi_out(s);
}

static const RegisterAccessInfo ronaldo_apu_regs_info[] = {
#define RVBAR_REGDEF(n) \
    {   .name = "RVBAR CPU " #n " Low",  .decode.addr = A_RVBARADDR ## n ## L, \
            .reset = 0xffff0000ul,                                             \
            .post_write = ronaldo_apu_rvbar_post_write,                        \
    },{ .name = "RVBAR CPU " #n " High", .decode.addr = A_RVBARADDR ## n ## H, \
            .post_write = ronaldo_apu_rvbar_post_write,                        \
    }
    RVBAR_REGDEF(0),
    RVBAR_REGDEF(1),
    RVBAR_REGDEF(2),
    RVBAR_REGDEF(3), { .name = "PWRCTL",  .decode.addr = A_PWRCTL,
        .post_write = ronaldo_apu_pwrctl_post_write,
    }
};

static const MemoryRegionOps ronaldo_apu_ops = {
    .read = register_read_memory_le,
    .write = register_write_memory_le,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void ronaldo_apu_handle_wfi(void *opaque, int irq, int level)
{
    RonaldoAPU *s = RONALDO_APU(opaque);

    s->cpu_in_wfi = deposit32(s->cpu_in_wfi, irq, 1, level);
    update_wfi_out(s);
}

static void ronaldo_apu_realize(DeviceState *dev, Error **errp)
{
    RonaldoAPU *s = RONALDO_APU(dev);
    const char *prefix = object_get_canonical_path(OBJECT(dev));
    int i;

    for (i = 0; i < ARRAY_SIZE(ronaldo_apu_regs_info); ++i) {
        RegisterInfo *r = &s->regs_info[i];

        *r = (RegisterInfo) {
            .data = (uint8_t *)&s->regs[
                    ronaldo_apu_regs_info[i].decode.addr/4],
            .data_size = sizeof(uint32_t),
            .access = &ronaldo_apu_regs_info[i],
            .debug = RONALDO_APU_ERR_DEBUG,
            .prefix = prefix,
            .opaque = s,
        };
        memory_region_init_io(&r->mem, OBJECT(dev), &ronaldo_apu_ops, r,
                              r->access->name, 4);
        memory_region_add_subregion(&s->iomem, r->access->decode.addr, &r->mem);
    }
    return;
}

static void ronaldo_apu_init(Object *obj)
{
    RonaldoAPU *s = RONALDO_APU(obj);
    int i;

    memory_region_init(&s->iomem, obj, "MMIO", R_MAX * 4);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);

    for (i = 0; i < NUM_CPUS; ++i) {
        char *prop_name = g_strdup_printf("cpu%d", i);
        object_property_add_link(obj, prop_name, TYPE_ARM_CPU,
                                 (Object **)&s->cpus[i],
                                 qdev_prop_allow_set_link_before_realize,
                                 OBJ_PROP_LINK_UNREF_ON_RELEASE,
                                 &error_abort);
        g_free(prop_name);
    }

    /* wfi_out is used to connect to PMU GPIs. */
    qdev_init_gpio_out_named(DEVICE(obj), s->wfi_out, "wfi_out", 4);
    /* CPU_POWER_STATUS is used to connect to INTC redirect. */
    qdev_init_gpio_out_named(DEVICE(obj), s->cpu_power_status,
                             "CPU_POWER_STATUS", 4);
    /* wfi_in is used as input from CPUs as wfi request. */
    qdev_init_gpio_in_named(DEVICE(obj), ronaldo_apu_handle_wfi, "wfi_in", 4);
}

static const VMStateDescription vmstate_ronaldo_apu = {
    .name = "ronaldo_apu",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, RonaldoAPU, R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static const FDTGenericGPIOSet ronaldo_apu_controller_gpios[] = {
    {
        .names = &fdt_generic_gpio_name_set_gpio,
        .gpios = (FDTGenericGPIOConnection[])  {
            { .name = "wfi_in", .fdt_index = 0, .range = 4 },
            { .name = "CPU_POWER_STATUS", .fdt_index = 4, .range = 4 },
            { },
        },
    },
    { },
};

static const FDTGenericGPIOSet ronaldo_apu_client_gpios[] = {
    {
        .names = &fdt_generic_gpio_name_set_gpio,
        .gpios = (FDTGenericGPIOConnection[])  {
            { .name = "wfi_out",          .fdt_index = 0, .range = 4 },
            { },
        },
    },
    { },
};

static void ronaldo_apu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    FDTGenericGPIOClass *fggc = FDT_GENERIC_GPIO_CLASS(klass);

    dc->reset = ronaldo_apu_reset;
    dc->realize = ronaldo_apu_realize;
    dc->vmsd = &vmstate_ronaldo_apu;
    fggc->controller_gpios = ronaldo_apu_controller_gpios;
    fggc->client_gpios = ronaldo_apu_client_gpios;
}

static const TypeInfo ronaldo_apu_info = {
    .name          = TYPE_RONALDO_APU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RonaldoAPU),
    .class_init    = ronaldo_apu_class_init,
    .instance_init = ronaldo_apu_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_FDT_GENERIC_GPIO },
        { }
    },
};

static void ronaldo_apu_register_types(void)
{
    type_register_static(&ronaldo_apu_info);
}

type_init(ronaldo_apu_register_types)
