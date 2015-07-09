/*
 * Arm PrimeCell PL022 Synchronous Serial Port
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "hw/sysbus.h"
#include "hw/ssi.h"
#include "qemu/fifo.h"

//#define DEBUG_PL022 1

#ifdef DEBUG_PL022
#define DPRINTF(fmt, ...) \
do { fprintf(stderr, "pl022: " fmt , ## __VA_ARGS__); } while (0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "pl022: error: " fmt , ## __VA_ARGS__); exit(1);} while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "pl022: error: " fmt , ## __VA_ARGS__);} while (0)
#endif

#define PL022_CR1_LBM 0x01
#define PL022_CR1_SSE 0x02
#define PL022_CR1_MS  0x04
#define PL022_CR1_SDO 0x08

#define PL022_SR_TFE  0x01
#define PL022_SR_TNF  0x02
#define PL022_SR_RNE  0x04
#define PL022_SR_RFF  0x08
#define PL022_SR_BSY  0x10

#define PL022_INT_ROR 0x01
#define PL022_INT_RT  0x04
#define PL022_INT_RX  0x04
#define PL022_INT_TX  0x08

#define TYPE_PL022 "pl022"
#define PL022(obj) OBJECT_CHECK(PL022State, (obj), TYPE_PL022)

#define PL022_FIFO_DEPTH 8

typedef struct PL022State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t cr0;
    uint32_t cr1;
    uint32_t bitmask;
    uint32_t sr;
    uint32_t cpsr;
    uint32_t is;
    uint32_t im;

    Fifo tx_fifo;
    Fifo rx_fifo;

    qemu_irq irq;
    SSIBus *ssi;
} PL022State;

static const unsigned char pl022_id[8] =
  { 0x22, 0x10, 0x04, 0x00, 0x0d, 0xf0, 0x05, 0xb1 };

static void pl022_update(PL022State *s)
{
    uint32_t tx_fifo_len = fifo_num_used(&s->tx_fifo);
    uint32_t rx_fifo_len = fifo_num_used(&s->rx_fifo);

    s->sr = 0;
    if (tx_fifo_len == 0) {
        s->sr |= PL022_SR_TFE;
    }
    if (tx_fifo_len != PL022_FIFO_DEPTH) {
        s->sr |= PL022_SR_TNF;
    }
    if (rx_fifo_len != 0) {
        s->sr |= PL022_SR_RNE;
    }
    if (rx_fifo_len == PL022_FIFO_DEPTH) {
        s->sr |= PL022_SR_RFF;
    }
    if (tx_fifo_len) {
        s->sr |= PL022_SR_BSY;
    }
    s->is = 0;
    if (rx_fifo_len >= 4) {
        s->is |= PL022_INT_RX;
    }
    if (tx_fifo_len <= 4) {
        s->is |= PL022_INT_TX;
    }

    qemu_set_irq(s->irq, (s->is & s->im) != 0);
}

static void pl022_xfer(PL022State *s)
{
    int val;

    if ((s->cr1 & PL022_CR1_SSE) == 0) {
        pl022_update(s);
        DPRINTF("Disabled\n");
        return;
    }

    DPRINTF("Maybe xfer %" PRId32 "/%" PRId32 "\n",
            fifo_num_used(&s->tx_fifo), fifo_num_used(&s->rx_fifo));
    /* ??? We do not emulate the line speed.
       This may break some applications.  The are two problematic cases:
        (a) A driver feeds data into the TX FIFO until it is full,
         and only then drains the RX FIFO.  On real hardware the CPU can
         feed data fast enough that the RX fifo never gets chance to overflow.
        (b) A driver transmits data, deliberately allowing the RX FIFO to
         overflow because it ignores the RX data anyway.

       We choose to support (a) by stalling the transmit engine if it would
       cause the RX FIFO to overflow.  In practice much transmit-only code
       falls into (a) because it flushes the RX FIFO to determine when
       the transfer has completed.  */
    while (!fifo_is_empty(&s->tx_fifo) && !fifo_is_full(&s->rx_fifo)) {
        DPRINTF("xfer\n");
        val = fifo_pop16(&s->tx_fifo);
        if (s->cr1 & PL022_CR1_LBM) {
            /* Loopback mode.  */
        } else {
            val = ssi_transfer(s->ssi, val);
        }
        fifo_push16(&s->rx_fifo, val & s->bitmask);
    }
    pl022_update(s);
}

static uint64_t pl022_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    PL022State *s = (PL022State *)opaque;
    int val;

    if (offset >= 0xfe0 && offset < 0x1000) {
        return pl022_id[(offset - 0xfe0) >> 2];
    }
    switch (offset) {
    case 0x00: /* CR0 */
      return s->cr0;
    case 0x04: /* CR1 */
      return s->cr1;
    case 0x08: /* DR */
        if (!fifo_is_empty(&s->rx_fifo)) {
            val = fifo_pop16(&s->rx_fifo);
            DPRINTF("RX %02x\n", val);
            pl022_xfer(s);
        } else {
            val = 0;
        }
        return val;
    case 0x0c: /* SR */
        return s->sr;
    case 0x10: /* CPSR */
        return s->cpsr;
    case 0x14: /* IMSC */
        return s->im;
    case 0x18: /* RIS */
        return s->is;
    case 0x1c: /* MIS */
        return s->im & s->is;
    case 0x20: /* DMACR */
        /* Not implemented.  */
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pl022_read: Bad offset %x\n", (int)offset);
        return 0;
    }
}

static void pl022_write(void *opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{
    PL022State *s = (PL022State *)opaque;

    switch (offset) {
    case 0x00: /* CR0 */
        s->cr0 = value;
        /* Clock rate and format are ignored.  */
        s->bitmask = (1 << ((value & 15) + 1)) - 1;
        break;
    case 0x04: /* CR1 */
        s->cr1 = value;
        if ((s->cr1 & (PL022_CR1_MS | PL022_CR1_SSE))
                   == (PL022_CR1_MS | PL022_CR1_SSE)) {
            BADF("SPI slave mode not implemented\n");
        }
        pl022_xfer(s);
        break;
    case 0x08: /* DR */
        if (!fifo_is_full(&s->tx_fifo)) {
            DPRINTF("TX %02x\n", (unsigned)value);
            fifo_push16(&s->tx_fifo, value & s->bitmask);
            pl022_xfer(s);
        }
        break;
    case 0x10: /* CPSR */
        /* Prescaler.  Ignored.  */
        s->cpsr = value & 0xff;
        break;
    case 0x14: /* IMSC */
        s->im = value;
        pl022_update(s);
        break;
    case 0x20: /* DMACR */
        if (value) {
            qemu_log_mask(LOG_UNIMP, "pl022: DMA not implemented\n");
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pl022_write: Bad offset %x\n", (int)offset);
    }
}

static void pl022_reset(PL022State *s)
{
    fifo_reset(&s->rx_fifo);
    fifo_reset(&s->tx_fifo);
    s->im = 0;
    s->is = PL022_INT_TX;
    s->sr = PL022_SR_TFE | PL022_SR_TNF;
}

static const MemoryRegionOps pl022_ops = {
    .read = pl022_read,
    .write = pl022_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_pl022 = {
    .name = "pl022_ssp",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(cr0, PL022State),
        VMSTATE_UINT32(cr1, PL022State),
        VMSTATE_UINT32(bitmask, PL022State),
        VMSTATE_UINT32(sr, PL022State),
        VMSTATE_UINT32(cpsr, PL022State),
        VMSTATE_UINT32(is, PL022State),
        VMSTATE_UINT32(im, PL022State),
        VMSTATE_FIFO(tx_fifo, PL022State),
        VMSTATE_FIFO(rx_fifo, PL022State),
        VMSTATE_END_OF_LIST()
    }
};

static int pl022_init(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    PL022State *s = PL022(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &pl022_ops, s, "pl022", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    s->ssi = ssi_create_bus(dev, "ssi");
    fifo_create16(&s->tx_fifo, PL022_FIFO_DEPTH);
    fifo_create16(&s->rx_fifo, PL022_FIFO_DEPTH);
    pl022_reset(s);
    vmstate_register(dev, -1, &vmstate_pl022, s);
    return 0;
}

static void pl022_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = pl022_init;
}

static const TypeInfo pl022_info = {
    .name          = TYPE_PL022,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PL022State),
    .class_init    = pl022_class_init,
};

static void pl022_register_types(void)
{
    type_register_static(&pl022_info);
}

type_init(pl022_register_types)
