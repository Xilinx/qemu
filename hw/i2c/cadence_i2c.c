/*
 *  Cadence I2C controller
 *
 *  Copyright (C) 2012 Xilinx Inc.
 *  Copyright (C) 2012 Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/bitops.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "hw/i2c/i2c.h"
#include "qemu/fifo8.h"
#include "qemu/log.h"

#define TYPE_CADENCE_I2C                  "cdns.i2c-r1p10"
#define CADENCE_I2C(obj)                  \
    OBJECT_CHECK(CadenceI2CState, (obj), TYPE_CADENCE_I2C)

/* Cadence I2C memory map */
#define R_CONTROL                            (0x00 / 4)
#define CONTROL_DIV_A_SHIFT                  14
#define CONTROL_DIV_A_WIDTH                  2
#define CONTROL_DIV_B_SHIFT                  8
#define CONTROL_DIV_B_WIDTH                  6
#define CONTROL_CLR_FIFO                     (1 << 6)
#define CONTROL_SLVMON                       (1 << 5)
#define CONTROL_HOLD                         (1 << 4)
#define CONTROL_ACKEN                        (1 << 3)
#define CONTROL_NEA                          (1 << 2)
#define CONTROL_MS                           (1 << 1)
#define CONTROL_RW                           (1 << 0)
#define R_STATUS                             (0x04 / 4)
#define STATUS_BA                            (1 << 8)
#define STATUS_RXOVF                         (1 << 7)
#define STATUS_TXDV                          (1 << 6)
#define STATUS_RXDV                          (1 << 5)
#define STATUS_RXRW                          (1 << 3)
#define R_ADDRESS                            (0x08 / 4)
#define R_DATA                               (0x0C / 4)
#define R_ISR                                (0x10 / 4)
#define ISR_RX_UNF                           (1 << 7)
#define ISR_TX_OVF                           (1 << 6)
#define ISR_RX_OVF                           (1 << 5)
#define ISR_SLV_RDY                          (1 << 4)
#define ISR_TO                               (1 << 3)
#define ISR_NACK                             (1 << 2)
#define ISR_DATA                             (1 << 1)
#define ISR_COMP                             (1 << 0)
#define R_TRANSFER_SIZE                      (0x14 / 4)
#define R_INTRPT_MASK                        (0x20 / 4)
#define R_INTRPT_ENABLE                      (0x24 / 4)
#define R_INTRPT_DISABLE                     (0x28 / 4)
#define R_MAX                                (R_INTRPT_DISABLE + 1)

/* Just approximate for the moment */

#define NS_PER_PCLK 10ull

/* FIXME: this struct defintion is generic, may belong in bitops or somewhere
 * like that
 */

typedef struct CadenceI2CRegInfo {
    const char *name;
    uint32_t ro;
    uint32_t wtc;
    uint32_t reset;
    int width;
}  CadenceI2CRegInfo;

static const CadenceI2CRegInfo cadence_i2c_reg_info[] = {
    [R_CONTROL]        = {.name = "CONTROL", .width = 16,
                          .ro = CONTROL_CLR_FIFO | (1 << 7) },
    [R_STATUS]         = {.name = "STATUS", .width = 9, .ro = ~0 },
    [R_ADDRESS]        = {.name = "ADDRESS", .width = 10 },
    [R_DATA]           = {.name = "DATA", .width = 8 },
    [R_ISR]            = {.name = "ISR", .width = 10, .wtc = 0x2FF,
                          .ro = 0x100 },
    [R_TRANSFER_SIZE]  = {.name = "TRANSFER_SIZE", .width = 8 },
    [R_INTRPT_MASK]    = {.name = "INTRPT_MASK", .width = 10, .ro = ~0,
                          .reset = 0x2FF },
    [R_INTRPT_ENABLE]  = {.name = "INTRPT_ENABLE", .width = 10, .wtc = ~0 },
    [R_INTRPT_DISABLE] = {.name = "INTRPT_DISABLE", .width = 10, .wtc = ~0 },
};

#ifndef CADENCE_I2C_DEBUG
#define CADENCE_I2C_DEBUG 0
#endif
#define DB_PRINT(fmt, args...) do {\
    if (CADENCE_I2C_DEBUG) {\
        fprintf(stderr, "CADENCE_I2C: %s:" fmt, __func__, ## args);\
    } \
} while (0);

#define FIFO_WIDTH 16

typedef struct CadenceI2CState {
    SysBusDevice busdev;
    MemoryRegion iomem;
    i2c_bus *bus;
    qemu_irq irq;

    QEMUTimer *transfer_timer;

    bool rw;

    Fifo8 fifo;
    uint32_t regs[R_MAX];
} CadenceI2CState;

static inline void cadence_i2c_update_status(CadenceI2CState *s)
{
    if (s->regs[R_STATUS] & STATUS_BA) {
        uint64_t delay = NS_PER_PCLK;
        delay *= extract32(s->regs[R_CONTROL], CONTROL_DIV_A_SHIFT,
                           CONTROL_DIV_A_WIDTH) + 1;
        delay *= extract32(s->regs[R_CONTROL], CONTROL_DIV_B_SHIFT,
                           CONTROL_DIV_B_WIDTH) + 1;
        delay *= 10; /* 8 bits + ACK/NACK, approximate as 10 cycles/op */
        DB_PRINT("scheduling transfer operation with delay of %lldns\n",
                 (unsigned long long)delay);
        qemu_mod_timer(s->transfer_timer, qemu_get_clock_ns(vm_clock) + delay);
    }

    DB_PRINT("irq state: %d\n", !!(s->regs[R_ISR] & ~s->regs[R_INTRPT_MASK]));
    qemu_set_irq(s->irq, !!(s->regs[R_ISR] & ~s->regs[R_INTRPT_MASK]));
}

static void cadence_i2c_do_stop(CadenceI2CState *s)
{
        if (!(s->regs[R_CONTROL] & CONTROL_HOLD) &&
                (s->regs[R_STATUS] & STATUS_BA)) {
            DB_PRINT("sending stop condition");
            i2c_end_transfer(s->bus);
            s->regs[R_STATUS] &= ~STATUS_BA;
        }
}

static void cadence_i2c_do_txrx(void *opaque)
{
    CadenceI2CState *s = opaque;

    if (!!(s->regs[R_CONTROL] & CONTROL_RW) != s->rw) {
        return;
    }

    DB_PRINT("doing trasnfer at time %llx\n",
             (unsigned long long)qemu_get_clock_ns(vm_clock));
    if (!(s->regs[R_CONTROL] & CONTROL_RW)) { /* write */
        if (fifo8_is_empty(&s->fifo)) {
            cadence_i2c_do_stop(s);
        } else {
            uint8_t t = fifo8_pop(&s->fifo);
            if (i2c_send(s->bus, t)) {
                s->regs[R_ISR] |= ISR_NACK;
            }
            if (fifo8_is_empty(&s->fifo)) {
                s->regs[R_ISR] |= ISR_COMP;
            }
            if (s->regs[R_TRANSFER_SIZE]) {
                s->regs[R_TRANSFER_SIZE]--;
            }
            if (s->fifo.num <= 2) {
                s->regs[R_ISR] |= ISR_DATA;
            }
        }
    } else { /* read */
        /* noting to transfer? - stop */
        if (!s->regs[R_TRANSFER_SIZE]) {
            cadence_i2c_do_stop(s);
            DB_PRINT("stopping read transfer\n");
        /* fifo full without hold - overflow */
        } else if (fifo8_is_full(&s->fifo) &&
                   !(s->regs[R_CONTROL] & CONTROL_HOLD)) {
            i2c_recv(s->bus);
            s->regs[R_ISR] |= ISR_RX_OVF;
            s->regs[R_STATUS] |= STATUS_RXOVF;
            DB_PRINT("nacking becuase the fifo is full!\n");
            i2c_nack(s->bus);
            cadence_i2c_do_stop(s);
        /* fifo not full - do a byte sucessfully */
        } else if (!fifo8_is_full(&s->fifo)) {
            uint8_t r = i2c_recv(s->bus);
            DB_PRINT("receiving from I2C bus: %02x\n", r);
            fifo8_push(&s->fifo, r);
            s->regs[R_STATUS] |= STATUS_RXDV;
            if (s->fifo.num >= FIFO_WIDTH - 2) {
                s->regs[R_ISR] |= ISR_DATA;
            }
            if (!(s->regs[R_CONTROL] & CONTROL_ACKEN)) {
                i2c_nack(s->bus);
            }
            s->regs[R_TRANSFER_SIZE]--;
            if (!s->regs[R_TRANSFER_SIZE]) {
                DB_PRINT("Nacking last byte of read transaction\n");
                i2c_nack(s->bus);
                s->regs[R_ISR] |= ISR_COMP;
            }
        }
        /* fallthrough with no action if fifo full with HOLD==1 */
    }

    cadence_i2c_update_status(s);
}

static inline void cadence_i2c_check_reg_access(hwaddr offset, uint32_t val,
                                                bool rnw)
{
    if (!cadence_i2c_reg_info[offset >> 2].name) {
        qemu_log_mask(LOG_UNIMP, "cadence i2c: %s offset %x\n",
                      rnw ? "read from" : "write to", (unsigned)offset);
        DB_PRINT("%s offset %x\n",
                      rnw ? "read from" : "write to", (unsigned)offset);
    } else {
        DB_PRINT("%s %s [%#02x] %s %#08x\n", rnw ? "read" : "write",
                 cadence_i2c_reg_info[offset >> 2].name, (unsigned) offset,
                 rnw ? "->" : "<-", val);
    }
}

static uint64_t cadence_i2c_read(void *opaque, hwaddr offset,
                                 unsigned size)
{
    CadenceI2CState *s = (CadenceI2CState *)opaque;
    const CadenceI2CRegInfo *info = &cadence_i2c_reg_info[offset >> 2];
    uint32_t ret = s->regs[offset >> 2];

    cadence_i2c_check_reg_access(offset, ret, true);
    if (!info->name) {
        return 0;
    }
    ret &= (1ull << info->width) - 1;

    if (offset >> 2 == R_DATA) {
        if (fifo8_is_empty(&s->fifo)) {
            s->regs[R_ISR] |= ISR_RX_UNF;
        } else {
            s->regs[R_STATUS] &= ~STATUS_RXOVF;
            ret = fifo8_pop(&s->fifo);
            if (fifo8_is_empty(&s->fifo)) {
                s->regs[R_STATUS] &= ~STATUS_RXDV;
            }
        }
    }
    return ret;
}

static void cadence_i2c_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    CadenceI2CState *s = (CadenceI2CState *)opaque;
    const CadenceI2CRegInfo *info = &cadence_i2c_reg_info[offset >> 2];
    uint32_t new_value = value;
    uint32_t ro_mask;

    cadence_i2c_check_reg_access(offset, value, false);
    if (!info->name) {
        return;
    }
    offset >>= 2;
    assert(!(info->wtc & info->ro));
    /* preserve read-only and write to clear bits */
    ro_mask = info->ro | info->wtc | ~((1ull << info->width) - 1);
    new_value &= ~ro_mask;
    new_value |= ro_mask & s->regs[offset];
    /* do write to clear */
    new_value &= ~(value & info->wtc);
    s->regs[offset] = new_value;

    switch (offset) {
    case R_CONTROL:
        if (value & CONTROL_CLR_FIFO) {
            DB_PRINT("clearing fifo\n");
            s->regs[R_TRANSFER_SIZE] = 0;
            s->regs[R_STATUS] &= ~STATUS_RXOVF;
            fifo8_reset(&s->fifo);
        }
        if (new_value & CONTROL_SLVMON) {
            qemu_log_mask(LOG_UNIMP, "slave monitor mode selected "
                          "(un-implemented)");
        }
        break;
    case R_ADDRESS:
        s->rw = s->regs[R_CONTROL] & CONTROL_RW;
        if (!(s->regs[R_CONTROL] & CONTROL_NEA)) {
            qemu_log_mask(LOG_UNIMP, "cadence i2c: 10 bit addressing selected "
                          "(unimplmented)");
        }
        i2c_start_transfer(s->bus, new_value & 0x7f,
                           s->regs[R_CONTROL] & CONTROL_RW);
        s->regs[R_STATUS] |= STATUS_BA;
        break;
    case R_DATA:
        if (fifo8_is_full(&s->fifo)) {
            s->regs[R_ISR] |= ISR_TX_OVF;
        } else if (fifo8_is_empty(&s->fifo)) {
            fifo8_push(&s->fifo, new_value);
        } else {
            s->regs[R_TRANSFER_SIZE]++;
            fifo8_push(&s->fifo, new_value);
        }
        break;
    case R_INTRPT_ENABLE:
        s->regs[R_INTRPT_MASK] &= ~value;
        break;
    case R_INTRPT_DISABLE:
        s->regs[R_INTRPT_MASK] |= value;
        break;
    }
    cadence_i2c_update_status(s);
}

static const MemoryRegionOps cadence_i2c_ops = {
    .read = cadence_i2c_read,
    .write = cadence_i2c_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription cadence_i2c_vmstate = {
    .name = TYPE_CADENCE_I2C,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_FIFO8(fifo, CadenceI2CState),
        VMSTATE_UINT32_ARRAY(regs, CadenceI2CState, R_MAX),
        VMSTATE_TIMER(transfer_timer, CadenceI2CState),
        VMSTATE_END_OF_LIST()
    }
};

static void cadence_i2c_reset(DeviceState *d)
{
    CadenceI2CState *s = CADENCE_I2C(d);
    int i;

    qemu_del_timer(s->transfer_timer);
    for (i = 0; i < R_MAX; ++i) {
        s->regs[i] = cadence_i2c_reg_info[i].name ?
                cadence_i2c_reg_info[i].reset : 0;
    }
    fifo8_reset(&s->fifo);
}

static void cadence_i2c_realize(DeviceState *dev, Error **errp)
{
    CadenceI2CState *s = CADENCE_I2C(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, &cadence_i2c_ops, s, TYPE_CADENCE_I2C,
                          R_MAX * 4);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->bus = i2c_init_bus(dev, "i2c");
    i2c_auto_connect_slaves(DEVICE(dev), s->bus, 0, 128);

    s->transfer_timer = qemu_new_timer_ns(vm_clock, cadence_i2c_do_txrx, s);

    fifo8_create(&s->fifo, FIFO_WIDTH);
}

static void cadence_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &cadence_i2c_vmstate;
    dc->reset = cadence_i2c_reset;
    dc->realize = cadence_i2c_realize;
}

static const TypeInfo cadence_i2c_type_info = {
    .name = TYPE_CADENCE_I2C,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CadenceI2CState),
    .class_init = cadence_i2c_class_init,
};

static void cadence_i2c_register_types(void)
{
    type_register_static(&cadence_i2c_type_info);
}

type_init(cadence_i2c_register_types)
