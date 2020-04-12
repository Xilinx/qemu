/*
 * SI570,SI571 Dummy Crystal Oscillator
 *
 * Copyright (c) 2016 Xilinx Inc.
 * Written by Sai Pavan Boddu <saipava@xilinx.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/i2c/i2c.h"

#ifndef SI57X_DEBUG
#define SI57X_DEBUG 0
#endif

#define DPRINT(fmt, args...) \
    do { \
        if (SI57X_DEBUG) { \
            qemu_log("%s: "fmt, __func__, ## args); \
        } \
    } while (0)

#define HS_DIV_OFFSET 5
#define HS_DIV_MASK   0xE0

#define N1_DIV_MSB_OFFSET 6
#define N1_DIV_MSB_MASK   0x3F

#define N1_DIV_LSB_OFFSET 0
#define N1_DIV_LSB_MASK   0xC0

#define CTRL_REG0   135
#define CTRL_REG1   137

#define REG0 0
#define REG1 1
#define REG2 2
#define REG3 3
#define REG4 4
#define REG5 5
#define REG6 6
#define REG7 7

/* Mapping CTRL_REG0/1 to offset 6,7 */
#define CTRL_REG0_REL   6
    #define CTRL_REG0_RST_REG   7
    #define CTRL_REG0_NEWFREQ   6
    #define CTRL_REG0_FREZ_M    5
    #define CTRL_REG0_FREZ_VCDC 4
    #define CTRL_REG0_RECALL    0
#define CTRL_REG1_REL   7
    #define CTRL_REG1_FREZ_DCO  4

#define M(x) (1 << x)

#define TYPE_SI57X "si57x"
#define SI57X(obj) \
    OBJECT_CHECK(Si57xState, (obj), TYPE_SI57X)

typedef struct Si57xState {
    /* <private> */
    I2CSlave parent_obj;

    /* <public> */
    uint64_t rfreq; /* RFREQ Multiplier */
    /* HS_DIV, N1 Dividers */
    uint16_t hs_div;
    uint16_t n1;
    /* Fxtal is not needed as it cannot be read */

    /* Temperature Stability */
    uint16_t temp_stab;
    uint8_t regs[8];
    uint8_t state;
    uint8_t ptr;
} Si57xState;

enum states {
    IDEAL,
    ADDRESSING,
    ADDRESSING_DONE,
    WRITING,
    READING,
};

enum temp_stability {
    TEMP_STAB_7PPM = 7,
    TEMP_STAB_20PPM = 20,
    TEMP_STAB_50PPM = 50,
};

static bool rfreq_is_updating(Si57xState *s)
{
    uint8_t addr = s->ptr;

    /* Reg 1 to 5 Belogs to RFREQ */
    if (addr > REG0) {
        if (addr == REG1) {
            /* Only Bits 0 to 5 of REG1 belongs to RFREQ */
            return (s->regs[addr] & 0x3F) ? true : false;
        }
        return true;
    }
    return false;
}

/* Issue warnings when the required fields are updated without asserting
 * Freez functionality.
 */
static void si57x_freez_filter(Si57xState *s, int data)
{
    if (rfreq_is_updating(s)) {
        /* If RFREQ is updating, make sure FREEZ_M or FREEZ_DCO is high */
        if ((s->regs[CTRL_REG0_REL] & M(CTRL_REG0_FREZ_M)) ||
            (s->regs[CTRL_REG1_REL] & M(CTRL_REG1_FREZ_DCO))) {
            DPRINT("Update RFREQ 0x%x\n", data);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "Update RFREQ without asserting"
                          " FREEZ_M/FREEZ_DCO\n");
        }
    } else {
        /* If HS_DIV, N1 are updating, make sure FREEZ_DCO is high */
        if (!(s->regs[CTRL_REG1_REL] & M(CTRL_REG1_FREZ_DCO))) {
            qemu_log_mask(LOG_GUEST_ERROR, "Updateing HS_DIV/N1 without"
                          " FREEZ_DCO assert\n");
        }
    }
}

static void si57x_reset(DeviceState *dev)
{
    Si57xState *s = SI57X(dev);
    /* Fill Fxtal, HD_DIV, N1 with example values as the default values are not
     * specified in the documentation. Use the example values as mentioned in:
     * https://www.silabs.com/Support%20Documents/TechnicalDocs/si570.pdf
     */
    /* HS_DIV = 0 */
    s->regs[REG0] = 0;

    /* N1_DIV = 0x7 */
    s->regs[REG1] = 0x3 << N1_DIV_LSB_OFFSET;
    s->regs[REG0] |= 0x1 << N1_DIV_MSB_OFFSET;

    /* RFREQ = 0x2BC011EB8 */
    s->regs[REG5] = 0xB8;
    s->regs[REG4] = 0x1E;
    s->regs[REG3] = 0x01;
    s->regs[REG2] = 0xBC;
    s->regs[REG1] |= 0x2;

    s->regs[CTRL_REG0_REL] &= ~M(CTRL_REG0_RST_REG);
    /* By combining HS_DIV, N1 and RFREQ the user can calculate Fxtal.
     * We can assume the default Fxtal, i.e  114.285000000 MHz
     */
}

static void si57x_ctrl0_pw(Si57xState *s)
{
    if (s->regs[CTRL_REG0_REL] & M(CTRL_REG0_RST_REG)) {
        si57x_reset(DEVICE(s));
    }
    s->regs[CTRL_REG0_REL] &= ~M(CTRL_REG0_NEWFREQ);
    s->regs[CTRL_REG1_REL] &= ~M(CTRL_REG1_FREZ_DCO);
}

/* SI57X registers are distributed at address 7-12, 13-18, 135, 137.
 * So we remap them internally to offset 0 to 7.
 * This function maps the registers for devices having Temperature stability of
 * 50PPM, 20PPM and 7PPM.
 */
static void si57x_set_addr(Si57xState *s, uint8_t addr)
{
    if (addr > 18) {
        switch (addr) {
        case CTRL_REG0:
            s->ptr = 6;
            break;
        case CTRL_REG1:
            s->ptr = 7;
            break;
        }
        DPRINT("Setting ptr to %d\n", s->ptr);
        return;
    }

    switch (s->temp_stab) {
    case TEMP_STAB_50PPM:
    case TEMP_STAB_20PPM:
        s->ptr = addr - 7;
        break;
    case TEMP_STAB_7PPM:
        s->ptr = addr - 13;
        break;
    }
    DPRINT("Setting ptr to %d\n", s->ptr);
}

/* Master Tx i.e Slave Rx */
static int si57x_tx(I2CSlave *s, uint8_t data)
{
    Si57xState *slave = SI57X(s);
    uint8_t addr;

    if (slave->state == ADDRESSING) {
        DPRINT("addr: 0x%x\n", data);
        si57x_set_addr(slave, data);
        slave->state = ADDRESSING_DONE;
    } else {
        DPRINT("data: 0x%x\n", data);
        slave->state = WRITING;
        addr = slave->ptr;
        if (addr < 6) {
            si57x_freez_filter(slave, data);
            slave->regs[addr] = data;
            slave->ptr++;
        } else {
            switch (addr) {
            case CTRL_REG0_REL:
                slave->regs[addr] = data;
                si57x_ctrl0_pw(slave);
                break;
            case CTRL_REG1_REL:
                slave->regs[addr] = data;
                break;
            }
        }
    }

    return 0;
}

/* Master Rx i.e Slave Tx */
static uint8_t si57x_rx(I2CSlave *s)
{
    Si57xState *slave = SI57X(s);

    DPRINT("data: 0x%x\n", slave->regs[slave->ptr]);

    return slave->regs[slave->ptr];
}

static int si57x_event(I2CSlave *i2c, enum i2c_event event)
{
    Si57xState *s = SI57X(i2c);

    switch (event) {
    case I2C_START_SEND:
        s->state = ADDRESSING;
        break;
    case I2C_START_RECV:
        s->state = READING;
        break;
    case I2C_FINISH:
    case I2C_NACK:
        s->state = IDEAL;
        break;
    }

    return 0;
}

static Property si57x_properties[] = {
    DEFINE_PROP_UINT16("temperature-stability", Si57xState, temp_stab,
                       TEMP_STAB_50PPM),
    DEFINE_PROP_END_OF_LIST(),
};

static void si57x_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->event = si57x_event;
    k->recv = si57x_rx;
    k->send = si57x_tx;
    device_class_set_props(dc, si57x_properties);
    dc->reset = si57x_reset;
}

static const TypeInfo si57x_info = {
    .name = TYPE_SI57X,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(Si57xState),
    .class_init = si57x_class_init,
};

static void si57x_register_type(void)
{
    type_register_static(&si57x_info);
}

type_init(si57x_register_type)
