/*
 * QEMU replacement block for ZynqMP boot logic.
 *
 * Copyright (c) 2017 Xilinx Inc.
 * Written by Edgar E. Iglesias.
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
#include "hw/ptimer.h"
#include "qemu/bitops.h"
#include "qapi/error.h"
#include "sysemu/dma.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "hw/core/cpu.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "sysemu/reset.h"

#include "hw/misc/xlnx-zynqmp-pmufw-cfg.h"

#ifndef XILINX_ZYNQMP_BOOT_DEBUG
#define XILINX_ZYNQMP_BOOT_DEBUG 0
#endif

#define TYPE_XILINX_ZYNQMP_BOOT "xlnx,zynqmp-boot"

#define XILINX_ZYNQMP_BOOT(obj) \
     OBJECT_CHECK(ZynqMPBoot, (obj), TYPE_XILINX_ZYNQMP_BOOT)

/* IPI message buffers */
#define IPI_BUFFER_BASEADDR     0xFF990000U
#define IPI_BUFFER_RPU_0_BASE   (IPI_BUFFER_BASEADDR + 0x0U)
#define IPI_BUFFER_RPU_1_BASE   (IPI_BUFFER_BASEADDR + 0x200U)
#define IPI_BUFFER_APU_BASE     (IPI_BUFFER_BASEADDR + 0x400U)
#define IPI_BUFFER_PMU_BASE     (IPI_BUFFER_BASEADDR + 0xE00U)

#define IPI_BUFFER_TARGET_PMU_OFFSET    0x1C0U

#define IPI_BUFFER_REQ_OFFSET   0x0U
#define IPI_BUFFER_RESP_OFFSET  0x20U

/* IPI Base Address */
#define IPI_BASEADDR            0XFF300000
#define IPI_APU_IXR_PMU_0_MASK         (1 << 16)

#define IPI_TRIG_OFFSET         0
#define IPI_OBS_OFFSET          4

/* Power Management IPI interrupt number */
#define PM_INT_NUM              0
#define IPI_PMU_PM_INT_MASK     (IPI_APU_IXR_PMU_0_MASK << PM_INT_NUM)

#define IPI_APU_MASK            1U

#define PAYLOAD_ARG_CNT         6
#define PM_SET_CONFIGURATION    2

#define CPU_NONE 0xFFFFFFFF

#define DB_PRINT_L(lvl, fmt, args...) do { \
    if (XILINX_ZYNQMP_BOOT_DEBUG >= lvl) { \
        qemu_log("%s: " fmt, __func__, ## args); \
    } \
} while (0);

#define DB_PRINT(fmt, args...) DB_PRINT_L(1, fmt, ## args)

typedef enum {
    STATE_WAIT_RST = 0,
    STATE_WAIT_PMUFW,
    STATE_PMUFW_SETCFG,
    STATE_WAIT_PMUFW_READY,
    STATE_RELEASE_CPU,
    STATE_DONE,
} BootState;

typedef struct ZynqMPBoot {
    SysBusDevice parent_obj;

    MemoryRegion *dma_mr;
    AddressSpace *dma_as;

    ptimer_state *ptimer;

    BootState state;

    /* ZynqMP Boot reset is active-low.  */
    bool n_reset;

    bool boot_ready;

    struct {
        uint32_t cpu_num;
        bool use_pmufw;
        bool load_pmufw_cfg;
    } cfg;

    unsigned char *buf;
} ZynqMPBoot;

static const MemTxAttrs mattr_secure = { .secure = true };

static void boot_store32(ZynqMPBoot *s, uint64_t addr, uint32_t v)
{
    address_space_write(s->dma_as, addr, mattr_secure, (void *) &v, sizeof v);
}

static uint32_t boot_load32(ZynqMPBoot *s, uint64_t addr)
{
    uint32_t v;
    address_space_read(s->dma_as, addr, mattr_secure, (void *) &v, sizeof v);
    return v;
}

/*
 * Check if the the PMU is ready.
 */
static bool pm_ipi_ready(ZynqMPBoot *s)
{
    uint32_t r;

    r = boot_load32(s, IPI_BASEADDR + IPI_OBS_OFFSET);
    r &= IPI_PMU_PM_INT_MASK;
    return !r;
}

/*
 * Send an IPI to the PMU.
 */
static void pm_ipi_send(ZynqMPBoot *s,
                        uint32_t payload[PAYLOAD_ARG_CNT])
{
    unsigned int i;
    unsigned int offset = 0;
    uintptr_t buffer_base = IPI_BUFFER_APU_BASE +
                            IPI_BUFFER_TARGET_PMU_OFFSET +
                            IPI_BUFFER_REQ_OFFSET;

    assert(pm_ipi_ready(s));

    /* Write payload into IPI buffer */
    for (i = 0; i < PAYLOAD_ARG_CNT; i++) {
            boot_store32(s, buffer_base + offset, payload[i]);
            offset += 4;
    }
    /* Generate IPI to PMU */
    boot_store32(s, IPI_BASEADDR + IPI_TRIG_OFFSET, IPI_PMU_PM_INT_MASK);
}

static void release_cpu_set_pc(CPUState *cpu, run_on_cpu_data arg)
{
    cpu_set_pc(cpu, arg.target_ptr);
}

static void release_cpu(ZynqMPBoot *s)
{
    CPUState *cpu = qemu_get_cpu(s->cfg.cpu_num);
    CPUClass *cc = CPU_GET_CLASS(cpu);
    vaddr pc = 0;
    uint32_t r;

    DB_PRINT("Starting CPU#%d release\n", s->cfg.cpu_num)

    /*
     * Save and restore PC accross reset to keep ELF loaded entry point valid.
     */
    if (cc->get_pc) {
        pc = cc->get_pc(cpu);
    }
    if (s->cfg.cpu_num < 4) {
        /* Release the APU.  */
        r = boot_load32(s, 0xfd1a0104);
        r &= ~(1 << s->cfg.cpu_num);
        boot_store32(s, 0xfd1a0104, 0x80000000 | r);
    } else {
        /* FIXME: Not implemented yet.  */
    }
    if (cc->set_pc) {
        DB_PRINT("Setting CPU#%d PC to 0x%" PRIx64 "\n", s->cfg.cpu_num, pc)
        run_on_cpu(cpu, release_cpu_set_pc, RUN_ON_CPU_TARGET_PTR(pc));
    }
}

static bool check_for_pmufw(ZynqMPBoot *s)
{
    uint32_t r;

    r = boot_load32(s, 0xFFD80000);
    return r & (1 << 4);
}

static void roll_timer(ZynqMPBoot *s)
{
    ptimer_set_limit(s->ptimer, 200000, 1);
    ptimer_run(s->ptimer, 1);
}

static void boot_sequence(void *opaque)
{
    ZynqMPBoot *s = XILINX_ZYNQMP_BOOT(opaque);
    uint32_t pay[6] = {};

    switch (s->state) {
    case STATE_WAIT_PMUFW:
        if (!s->cfg.use_pmufw) {
            s->state = STATE_RELEASE_CPU;
            boot_sequence(s);
            return;
        }

        if (!check_for_pmufw(s)) {
            roll_timer(s);
            return;
        }

        if (s->cfg.load_pmufw_cfg) {
            s->state = STATE_PMUFW_SETCFG;
        } else {
            s->state = STATE_RELEASE_CPU;
        }
        boot_sequence(s);
        break;

    case STATE_PMUFW_SETCFG:
        if (!pm_ipi_ready(s)) {
            roll_timer(s);
            return;
        }

        /* Save DDR contents.  */
        s->buf = g_malloc(sizeof pmufw_cfg);
        address_space_read(s->dma_as, 0, mattr_secure,
                           s->buf, sizeof pmufw_cfg);
        address_space_write(s->dma_as, 0, mattr_secure,
                            (void *) pmufw_cfg, sizeof pmufw_cfg);
        pay[0] = PM_SET_CONFIGURATION;
        pay[1] = 0;
        pm_ipi_send(s, pay);
        s->state = STATE_WAIT_PMUFW_READY;
        boot_sequence(s);
        break;

    case STATE_WAIT_PMUFW_READY:
        if (!pm_ipi_ready(s)) {
            roll_timer(s);
            return;
        }

        /* Restore DDR contents.  */
        address_space_write(s->dma_as, 0, mattr_secure,
                            s->buf, sizeof pmufw_cfg);
        g_free(s->buf);
        s->buf = NULL;

        s->state = STATE_RELEASE_CPU;
        boot_sequence(s);
        break;

    case STATE_RELEASE_CPU:
        if (s->cfg.cpu_num != CPU_NONE) {
            release_cpu(s);
        }
        s->state = STATE_DONE;
        s->boot_ready = false;
        break;

    case STATE_DONE:
    case STATE_WAIT_RST:
        /* These states are not handled here.  */
        g_assert_not_reached();
        break;
    };
}

static void irq_handler(void *opaque, int irq, int level)
{
    ZynqMPBoot *s = XILINX_ZYNQMP_BOOT(opaque);

    if (!s->n_reset && level) {
        s->boot_ready = true;
    }
    s->n_reset = level;
}

static void zynqmp_boot_reset(void *opaque)
{
    ZynqMPBoot *s = XILINX_ZYNQMP_BOOT(opaque);

    if (s->boot_ready) {
        /* Start the boot sequence.  */
        DB_PRINT("Starting the boot sequence\n");
        s->state = STATE_WAIT_PMUFW;
        ptimer_transaction_begin(s->ptimer);
        boot_sequence(s);
        ptimer_transaction_commit(s->ptimer);
    }
}

static void zynqmp_boot_realize(DeviceState *dev, Error **errp)
{
    ZynqMPBoot *s = XILINX_ZYNQMP_BOOT(dev);

    if (s->cfg.cpu_num > 3 && s->cfg.cpu_num != CPU_NONE) {
        error_setg(errp, "cpu-num %u is out of range\n", s->cfg.cpu_num);
    }

    s->dma_as = s->dma_mr ? address_space_init_shareable(s->dma_mr, NULL)
                          : &address_space_memory;

    qemu_register_reset_loader(zynqmp_boot_reset, dev);

    s->ptimer = ptimer_init(boot_sequence, s, PTIMER_POLICY_DEFAULT);
    ptimer_transaction_begin(s->ptimer);
    ptimer_set_freq(s->ptimer, 1000000);
    ptimer_transaction_commit(s->ptimer);
}

static void zynqmp_boot_unrealize(DeviceState *dev)
{
    qemu_unregister_reset_loader(zynqmp_boot_reset, dev);
}

static void zynqmp_boot_init(Object *obj)
{
    ZynqMPBoot *s = XILINX_ZYNQMP_BOOT(obj);

    qdev_init_gpio_in(DEVICE(obj), irq_handler, 1);
    object_property_add_link(obj, "dma", TYPE_MEMORY_REGION,
                             (Object **)&s->dma_mr,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
}

static Property zynqmp_boot_props[] = {
    DEFINE_PROP_UINT32("cpu-num", ZynqMPBoot, cfg.cpu_num, CPU_NONE),
    DEFINE_PROP_BOOL("use-pmufw", ZynqMPBoot, cfg.use_pmufw, false),
    DEFINE_PROP_BOOL("load-pmufw-cfg", ZynqMPBoot, cfg.load_pmufw_cfg, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void zynqmp_boot_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = zynqmp_boot_realize;
    device_class_set_props(dc, zynqmp_boot_props);
    dc->unrealize = zynqmp_boot_unrealize;
}

static const TypeInfo zynqmp_boot_info = {
    .name          = TYPE_XILINX_ZYNQMP_BOOT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ZynqMPBoot),
    .class_init    = zynqmp_boot_class_init,
    .instance_init = zynqmp_boot_init,
};

static void zynqmp_boot_register_types(void)
{
    type_register_static(&zynqmp_boot_info);
}

type_init(zynqmp_boot_register_types)
