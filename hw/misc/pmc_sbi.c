/*
 * QEMU model of the PMC SBI
 *
 * Copyright (c) 2020 Xilinx Inc
 *
 * This code is licensed under the GNU GPL.
 */
#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/register.h"
#include "hw/irq.h"
#include "qemu/fifo.h"
#include "hw/remote-port-proto.h"
#include "hw/remote-port-device.h"
#include "hw/stream.h"
#include "qemu/log.h"
#include "hw/fdt_generic_util.h"

REG32(SBI_MODE, 0x0)
    FIELD(SBI_MODE, JTAG, 1, 1)
    FIELD(SBI_MODE, SELECT, 0, 1)
REG32(SBI_CTRL, 0x4)
    FIELD(SBI_CTRL, APB_ERR_RES, 5, 1)
    FIELD(SBI_CTRL, INTERFACE, 2, 3)
    FIELD(SBI_CTRL, SOFT_RST, 1, 1)
    FIELD(SBI_CTRL, ENABLE, 0, 1)
REG32(SMAP_CTRL, 0x8)
    FIELD(SMAP_CTRL, BURST_SIZE, 1, 2)
    FIELD(SMAP_CTRL, MODE, 0, 1)
REG32(SBI_IRQ_STATUS, 0x300)
    FIELD(SBI_IRQ_STATUS, DATA_RDY, 2, 1)
    FIELD(SBI_IRQ_STATUS, SMAP_ABORT, 1, 1)
    FIELD(SBI_IRQ_STATUS, INV_APB, 0, 1)
REG32(SBI_IRQ_MASK, 0x304)
    FIELD(SBI_IRQ_MASK, DATA_RDY, 2, 1)
    FIELD(SBI_IRQ_MASK, SMAP_ABORT, 1, 1)
    FIELD(SBI_IRQ_MASK, INV_APB, 0, 1)
REG32(SBI_IRQ_ENABLE, 0x308)
    FIELD(SBI_IRQ_ENABLE, DATA_RDY, 2, 1)
    FIELD(SBI_IRQ_ENABLE, SMAP_ABORT, 1, 1)
    FIELD(SBI_IRQ_ENABLE, INV_APB, 0, 1)
REG32(SBI_IRQ_DISABLE, 0x30c)
    FIELD(SBI_IRQ_DISABLE, DATA_RDY, 2, 1)
    FIELD(SBI_IRQ_DISABLE, SMAP_ABORT, 1, 1)
    FIELD(SBI_IRQ_DISABLE, INV_APB, 0, 1)
REG32(SBI_IRQ_TRIGGER, 0x310)
    FIELD(SBI_IRQ_TRIGGER, DATA_RDY, 2, 1)
    FIELD(SBI_IRQ_TRIGGER, SMAP_ABORT, 1, 1)
    FIELD(SBI_IRQ_TRIGGER, INV_APB, 0, 1)
REG32(SBI_RAM, 0x500)
    FIELD(SBI_RAM, EMASA, 6, 1)
    FIELD(SBI_RAM, EMAB, 3, 3)
    FIELD(SBI_RAM, EMAA, 0, 3)
REG32(SBI_ECO, 0x1000)

#define R_MAX (R_SBI_ECO + 1)

#define SMAP_INTERFACE      0
#define JTAG_INTERFACE      1
#define AXI_SLAVE_INTERFACE 2

#define SBI_DATA_LOADING_MODE 0
#define SBI_READ_BACK_MODE    1

#define SMAP_NORMAL_MODE 0
#define SMAP_BURST_MODE  1

#define SMAP_CS_B    1
#define SMAP_RDWR_B  0

#define TYPE_SBI "pmc.slave-boot"

#define SBI(obj) \
    OBJECT_CHECK(SlaveBootInt, (obj), TYPE_SBI)

#ifndef SBI_ERR_DEBUG
#define SBI_ERR_DEBUG 0
#endif

#define DPRINT(args, ...) \
    do { \
        if (SBI_ERR_DEBUG) { \
            fprintf(stderr, args, ## __VA_ARGS__); \
        } \
    } while (0)

#define IF_BURST(pnum) \
    (ARRAY_FIELD_EX32(s->regs, SMAP_CTRL, MODE) == SMAP_BURST_MODE && \
     ARRAY_FIELD_EX32(s->regs, SBI_CTRL, INTERFACE) == SMAP_INTERFACE && \
     (pnum >= (1 << ARRAY_FIELD_EX32(s->regs, SMAP_CTRL, BURST_SIZE)) * 1024))

#define IF_NON_BURST(pnum) \
    ((ARRAY_FIELD_EX32(s->regs, SMAP_CTRL, MODE) == SMAP_NORMAL_MODE) && \
     (pnum >= 4))

#define SMAP_BURST_SIZE(s) \
        ((1 << ARRAY_FIELD_EX32(s->regs, SMAP_CTRL, BURST_SIZE)) * 1024)

typedef struct SlaveBootInt {
    SysBusDevice parent_obj;

    StreamSlave *tx_dev;
    Fifo fifo;
    qemu_irq irq;
    StreamCanPushNotifyFn notify;
    void *notify_opaque;
    MemoryRegion iomem;
    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];
    int BusWidthDetectCounter;

    /* Select Map */
    uint8_t cs;           /* active low */
    uint8_t busy_line;    /* active high */
    uint8_t rdwr;         /* 0: data load
                           * 1: read-back */
    CharBackend chr; /* Data bus */
    qemu_irq smap_busy;
} SlaveBootInt;

static int sbi_can_receive_from_dma(SlaveBootInt *s)
{
    if (!ARRAY_FIELD_EX32(s->regs, SBI_MODE, SELECT)) {
        return 1;
    }

    if (IF_BURST(fifo_num_free(&s->fifo))) {
        return 0;
    }

    if (IF_NON_BURST(fifo_num_free(&s->fifo))) {
        return 0;
    }

    return 1;
}

static void sbi_update_irq(SlaveBootInt *s)
{
    bool pending;

    if (IF_BURST(s->fifo.num)) {
        ARRAY_FIELD_DP32(s->regs, SBI_IRQ_STATUS, DATA_RDY, 1);
    }

    if (IF_NON_BURST(s->fifo.num)) {
        ARRAY_FIELD_DP32(s->regs, SBI_IRQ_STATUS, DATA_RDY, 1);
    }
    pending = !!(s->regs[R_SBI_IRQ_STATUS] & ~s->regs[R_SBI_IRQ_MASK]);
    qemu_set_irq(s->irq, pending);
}

static void ss_update_busy_line(SlaveBootInt *s)
{
    uint32_t num = fifo_num_free(&s->fifo);

    if (!ARRAY_FIELD_EX32(s->regs, SBI_CTRL, ENABLE)) {
        s->busy_line = 1;
        goto update_busy;
    }

    if (ARRAY_FIELD_EX32(s->regs, SMAP_CTRL, MODE) == SMAP_BURST_MODE) {
        if (ARRAY_FIELD_EX32(s->regs, SBI_MODE, SELECT)
            == SBI_DATA_LOADING_MODE) {
            s->busy_line = (num >= SMAP_BURST_SIZE(s)) ? 0 : 1;
        } else {
            /* Read Back Mode */
            s->busy_line = (s->fifo.num >= SMAP_BURST_SIZE(s)) ?
                        0 : 1;
            if (s->notify) {
                s->notify(s->notify_opaque);
            }
        }
    } else {
        /* Normal Mode: Busy Line status changes for availablity of 4byte
         * data/free-space while data-load/read-back
         */
        if (ARRAY_FIELD_EX32(s->regs, SBI_MODE, SELECT)
            == SBI_DATA_LOADING_MODE) {
            s->busy_line = num >= 4 ? 0 : 1;
        } else {
            s->busy_line = s->fifo.num >= 4 ? 0 : 1;
            if (s->notify) {
                s->notify(s->notify_opaque);
            }
        }
    }
update_busy:
    /* FIXME: Update only if SMAP interface selected*/
    qemu_set_irq(s->smap_busy, s->busy_line);
}

static void ss_stream_out(SlaveBootInt *s);
static void smap_data_rdwr(SlaveBootInt *s)
{
    if (!s->cs) {
        if (!s->rdwr) {
            qemu_chr_fe_accept_input(&s->chr);
        } else {
            ss_stream_out(s);
        }
    }

    ss_update_busy_line(s);
    sbi_update_irq(s);
}

static void ss_reset(DeviceState *);

static void sbi_ctrl_postw(RegisterInfo *reg, uint64_t val64)
{
    SlaveBootInt *s = SBI(reg->opaque);
    uint32_t val = val64;

    if (val & R_SBI_CTRL_SOFT_RST_MASK) {
        ss_reset(DEVICE(s));
        ARRAY_FIELD_DP32(s->regs, SBI_CTRL, SOFT_RST, 0);
    }

    ss_update_busy_line(s);
}

static uint64_t sbi_mode_prew(RegisterInfo *reg, uint64_t val64)
{
    SlaveBootInt *s = SBI(reg->opaque);
    uint32_t val = val64;

    if (!s->cs) {
        if ((s->regs[R_SBI_MODE] & R_SBI_MODE_SELECT_MASK) ^
            (val & R_SBI_MODE_SELECT_MASK) && s->smap_busy) {
            DPRINT("Warning: Changing SBI mode when cs is asserted\n");
        }
    }

    if (!s->smap_busy) {
        s->rdwr = FIELD_EX32(val, SBI_MODE, SELECT);
    }
    return val64;
}

static uint64_t sbi_irq_enable_prew(RegisterInfo *reg, uint64_t val64)
{

    SlaveBootInt *s = SBI(reg->opaque);
    uint32_t val = val64;

    s->regs[R_SBI_IRQ_MASK] &= ~val;
    return 0;
}

static uint64_t sbi_irq_disable_prew(RegisterInfo *reg, uint64_t val64)
{
    SlaveBootInt *s = SBI(reg->opaque);
    uint32_t val = val64;

    s->regs[R_SBI_IRQ_MASK] |= val;
    return 0;
}

static uint64_t sbi_irq_trigger_prew(RegisterInfo *reg, uint64_t val64)
{
    SlaveBootInt *s = SBI(reg->opaque);
    uint32_t val = val64;

    s->regs[R_SBI_IRQ_STATUS] |= val;
    return 0;
}

static void ss_stream_notify(void *opaque)
{
    SlaveBootInt *s = SBI(opaque);
    uint32_t num = 0;
    uint8_t *data;

    while (stream_can_push(s->tx_dev, ss_stream_notify, s)) {
        if (fifo_is_empty(&s->fifo) || fifo_num_used(&s->fifo) < 4) {
            break;
        }
        /* num is equal to number of bytes read as its a fifo of width 1byte.
         * the same dosent holds good if width is grater than 1 byte
         */
        data = (uint8_t *) fifo_pop_buf(&s->fifo,
                        4, &num);

        stream_push(s->tx_dev, data, num, false);
    }
    ss_update_busy_line(s);
    sbi_update_irq(s);
}

static void ss_stream_out(SlaveBootInt *s)
{
    uint8_t *data;
    uint32_t len;

    if (!ARRAY_FIELD_EX32(s->regs, SBI_MODE, SELECT)) {
        return;
    }

    /*FIXME: Impement JTAG, AXI interface */
    while (!s->cs && s->rdwr) {
        if (IF_BURST(s->fifo.num)) {
            data = (uint8_t *) fifo_pop_buf(&s->fifo,
                        SMAP_BURST_SIZE(s),
                        &len);
            qemu_chr_fe_write(&s->chr, data, len);
        }

        if (IF_NON_BURST(s->fifo.num)) {
            data = (uint8_t *) fifo_pop_buf(&s->fifo, 4, &len);
            qemu_chr_fe_write(&s->chr, data, len);
        }

        ss_update_busy_line(s);
        if (s->busy_line) {
            break;
        }
    }
}

static bool ss_stream_can_push(StreamSlave *obj,
                StreamCanPushNotifyFn notify,
                void *notify_opaque)
{
    SlaveBootInt *s = SBI(obj);
    /* FIXME: Check for SMAP mode
     *        Add AXI Slave interface
     *        Add JTAG Interface
     */

    if (!s->smap_busy) {
        smap_data_rdwr(s);
    }
    if (sbi_can_receive_from_dma(s)) {
        /* return false and store the notify opts */
        s->notify = notify;
        s->notify_opaque = notify_opaque;
        return false;
    } else {
        /* Read to receive */
        s->notify = NULL;
        s->notify_opaque = NULL;
        return true;
    }
}

static size_t ss_stream_push(StreamSlave *obj, uint8_t *buf, size_t len,
                             bool eop)
{
    SlaveBootInt *s = SBI(obj);
    uint32_t free = fifo_num_free(&s->fifo);

    /* FIXME: Implement Other Interfaces mentioned above */
    fifo_push_all(&s->fifo, buf, free);
    ss_update_busy_line(s);
    sbi_update_irq(s);
    return free > len ? len : free;
}

/*** Chardev Stream handlers */
static int ss_sbi_can_receive(void *opaque)
{
    SlaveBootInt *s = SBI(opaque);
    uint32_t num = fifo_num_free(&s->fifo);
    uint32_t recvb = 0;

    if (s->cs || s->rdwr) {
        /* Data lines are in tristate when cs is high or
         * Master is in Read back mode
         * */
        return 0;
    }

    /* Check for Busy Line
     * Check on Fifo Space
     */
    if (ARRAY_FIELD_EX32(s->regs, SBI_CTRL, ENABLE) &&
        !ARRAY_FIELD_EX32(s->regs, SBI_MODE, SELECT)) {
        if (IF_BURST(num)) {
            recvb = (1 << ARRAY_FIELD_EX32(s->regs, SMAP_CTRL, BURST_SIZE)) *
                     1024;
        } else if (num >= 4) {
            recvb = 4;
        }
        /* if busy line is low */
        if (!s->busy_line) {
            return recvb;
        }
    }
    return 0;
}

static void ss_sbi_receive(void *opaque, const uint8_t *buf, int size)
{
    SlaveBootInt *s = SBI(opaque);
    uint32_t free = fifo_num_free(&s->fifo);

    while (s->BusWidthDetectCounter < 16) {
        /* First 16 bytes are used by harware for input port width
         * detection. We dont need to do that, so discard without
         * copying them to buffer
         */
        s->BusWidthDetectCounter++;
        buf++;
        size--;
        if (!size) {
            break;
        }
    }

    DPRINT("%s: Payload of size: %d recv\n", __func__, size);
    if (size <= free) {
        fifo_push_all(&s->fifo, buf, size);
        if (IF_BURST(free)) {
            ss_stream_notify(s);
            ARRAY_FIELD_DP32(s->regs, SBI_IRQ_STATUS, DATA_RDY, 1);
        }

        if (IF_NON_BURST(free)) {
            ss_stream_notify(s);
            ARRAY_FIELD_DP32(s->regs, SBI_IRQ_STATUS, DATA_RDY, 1);
        }
    }

    ss_update_busy_line(s);
    sbi_update_irq(s);
}
/***/

static void smap_update(void *opaque, int n, int level)
{
    SlaveBootInt *s = SBI(opaque);
    switch (n) {
    case SMAP_CS_B:
        s->cs = level;
        break;
    case SMAP_RDWR_B:
        if (!s->cs && (s->rdwr ^ level)) {
            ARRAY_FIELD_DP32(s->regs, SBI_IRQ_STATUS, SMAP_ABORT, 1);
        }
        s->rdwr = level;
        break;
    };
    smap_data_rdwr(s);
}

static RegisterAccessInfo slave_boot_regs_info[] = {
    {   .name = "SBI_MODE",  .addr = A_SBI_MODE,
        .reset = 0x2,
        .rsvd = 0xfffffffe,
        .pre_write = sbi_mode_prew,
    },{ .name = "SBI_CTRL",  .addr = A_SBI_CTRL,
        .reset = 0x20,
        .rsvd = 0xffffffc0,
        .post_write = sbi_ctrl_postw,
    },{ .name = "SMAP_CTRL",  .addr = A_SMAP_CTRL,
        .rsvd = 0xfffffff8,
    },{ .name = "SBI_IRQ_STATUS",  .addr = A_SBI_IRQ_STATUS,
        .rsvd = 0xfffffff8,
        .w1c = 0x7,
    },{ .name = "SBI_IRQ_MASK",  .addr = A_SBI_IRQ_MASK,
        .reset = 0x7,
        .rsvd = 0xfffffff8,
        .ro = 0x7,
    },{ .name = "SBI_IRQ_ENABLE",  .addr = A_SBI_IRQ_ENABLE,
        .rsvd = 0xfffffff8,
        .pre_write = sbi_irq_enable_prew,
    },{ .name = "SBI_IRQ_DISABLE",  .addr = A_SBI_IRQ_DISABLE,
        .rsvd = 0xfffffff8,
        .pre_write = sbi_irq_disable_prew,
    },{ .name = "SBI_IRQ_TRIGGER",  .addr = A_SBI_IRQ_TRIGGER,
        .rsvd = 0xfffffff8,
        .pre_write = sbi_irq_trigger_prew,
    },{ .name = "SBI_RAM",  .addr = A_SBI_RAM,
        .reset = 0x5b,
        .rsvd = 0xffffff80,
    },{ .name = "SBI_ECO",  .addr = A_SBI_ECO,
    }
};

static void sbi_write(void *opaque, hwaddr addr, uint64_t value,
                      unsigned size)
{
    RegisterInfoArray *reg_array = opaque;
    SlaveBootInt *s = SBI(reg_array->r[0]->opaque);

    register_write_memory(opaque, addr, value, size);
    smap_data_rdwr(s);
}

static const MemoryRegionOps ss_ops = {
    .read = register_read_memory,
    .write = sbi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void ss_realize(DeviceState *dev, Error **errp)
{
    SlaveBootInt *s = SBI(dev);
    const char *port_name;
    Chardev *chr;

    port_name = g_strdup("smap_busy_b");
    qdev_init_gpio_out_named(dev, &s->smap_busy, port_name, 1);
    g_free((gpointer) port_name);

    port_name = g_strdup("smap_in_b");
    qdev_init_gpio_in_named(dev, smap_update, port_name, 2);
    g_free((gpointer) port_name);

    chr = qemu_chr_find("sbi");
    qdev_prop_set_chr(dev, "chardev", chr);
    if (!qemu_chr_fe_get_driver(&s->chr)) {
        DPRINT("SBI interface not connected\n");
    } else {
        qemu_chr_fe_set_handlers(&s->chr, ss_sbi_can_receive, ss_sbi_receive,
                                 NULL, NULL, s, NULL, true);
    }

    fifo_create8(&s->fifo, 1024 * 4);
}

static void ss_reset(DeviceState *dev)
{
    SlaveBootInt *s = SBI(dev);
    uint32_t i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }
    fifo_reset(&s->fifo);
    s->busy_line = 1;
    qemu_set_irq(s->smap_busy, s->busy_line);
    ss_update_busy_line(s);
    sbi_update_irq(s);
    /* Note : cs always 0 when rp is not connected
     * i.e slave always respond to master data irrespective of
     * master state
     *
     * as rdwr is also 0, initial state of sbi is data load. Hack this bit
     * to become 1, when sbi changes to write mode. So, its assumed in
     * non remote-port model master should expect data when slave wishes
     * to send.
     */
}

static void ss_init(Object *obj)
{
    SlaveBootInt *s = SBI(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    object_property_add_link(obj, "stream-connected-sbi", TYPE_STREAM_SLAVE,
                             (Object **)&s->tx_dev,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);

    memory_region_init(&s->iomem, obj, TYPE_SBI, R_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), slave_boot_regs_info,
                              ARRAY_SIZE(slave_boot_regs_info),
                              s->regs_info, s->regs,
                              &ss_ops,
                              false,
                              R_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                0x0,
                                &reg_array->mem);

    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static const FDTGenericGPIOSet sbi_controller_gpios[] = {
    {
        .names = &fdt_generic_gpio_name_set_gpio,
        .gpios = (FDTGenericGPIOConnection []) {
            { .name = "smap_busy_b", .fdt_index = 0,
              .range = 1},
            { },
        },
    },
    { },
};

static const FDTGenericGPIOSet sbi_client_gpios[] = {
    {
        .names = &fdt_generic_gpio_name_set_gpio,
        .gpios = (FDTGenericGPIOConnection []) {
           { .name = "smap_in_b", .fdt_index = 0,
             .range = 2},
           { },
        },
    },
    { },
};

static Property sbi_props[] = {
        DEFINE_PROP_CHR("chardev", SlaveBootInt, chr),
        DEFINE_PROP_END_OF_LIST(),
};

static void ss_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    StreamSlaveClass *ssc = STREAM_SLAVE_CLASS(klass);
    FDTGenericGPIOClass *fggc = FDT_GENERIC_GPIO_CLASS(klass);
    dc->realize = ss_realize;
    dc->reset = ss_reset;
    device_class_set_props(dc, sbi_props);
    ssc->push = ss_stream_push;
    ssc->can_push = ss_stream_can_push;
    fggc->controller_gpios = sbi_controller_gpios;
    fggc->client_gpios = sbi_client_gpios;
}

static TypeInfo ss_info = {
    .name = TYPE_SBI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SlaveBootInt),
    .instance_init = ss_init,
    .class_init = ss_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_STREAM_SLAVE },
        { TYPE_FDT_GENERIC_GPIO},
        {}
    }
};

static void ss_register(void)
{
    type_register_static(&ss_info);
}

type_init(ss_register)
