/*
 * lsm6dso I3C interface
 *
 * Copyright (c) 2024, Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/i3c/i3c.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qemu/bswap.h"
#include "hw/qdev-properties.h"

#define TYPE_LSM6DSO "lsm6dso"
OBJECT_DECLARE_SIMPLE_TYPE(LSM6DSOState, LSM6DSO)

#define LSM6DSO_FUNC_CFG_ACCESS               0x01
#define LSM6DSO_PIN_CTRL                      0x02
#define LSM6DSO_FIFO_CTRL1                    0x07
#define LSM6DSO_FIFO_CTRL2                    0x08
#define LSM6DSO_FIFO_CTRL3                    0x09
#define LSM6DSO_FIFO_CTRL4                    0x0A
#define LSM6DSO_COUNTER_BDR_REG1              0x0B
#define LSM6DSO_COUNTER_BDR_REG2              0x0C
#define LSM6DSO_INT1_CTRL                     0x0D
#define LSM6DSO_INT2_CTRL                     0x0E
#define LSM6DSO_WHO_AM_I                      0x0F
#define LSM6DSO_CTRL1_XL                      0x10
#define LSM6DSO_CTRL2_G                       0x11
#define LSM6DSO_CTRL3_C                       0x12
 #define LSM6DSO_CTRL3_C_BOOT       7
 #define LSM6DSO_CTRL3_C_BDU        6
 #define LSM6DSO_CTRL3_C_H_LACTIVE  5
 #define LSM6DSO_CTRL3_C_PP_OD      4
 #define LSM6DSO_CTRL3_C_SIM        3
 #define LSM6DSO_CTRL3_C_IF_INC     2
 #define LSM6DSO_CTRL3_C_SW_RESET   0
#define LSM6DSO_CTRL4_C                       0x13
#define LSM6DSO_CTRL5_C                       0x14
 #define LSM6DSO_CTRL5_C_ROUNDING0  5
#define LSM6DSO_CTRL6_C                       0x15
#define LSM6DSO_CTRL7_G                       0x16
#define LSM6DSO_CTRL8_XL                      0x17
#define LSM6DSO_CTRL9_XL                      0x18
#define LSM6DSO_CTRL10_C                      0x19
#define LSM6DSO_ALL_INT_SRC                   0x1A
#define LSM6DSO_WAKE_UP_SRC                   0x1B
#define LSM6DSO_TAP_SRC                       0x1C
#define LSM6DSO_D6D_SRC                       0x1D
#define LSM6DSO_STATUS_REG                    0x1E
#define LSM6DSO_OUT_TEMP_L                    0x20
#define LSM6DSO_OUT_TEMP_H                    0x21
#define LSM6DSO_OUTX_L_G                      0x22
#define LSM6DSO_OUTX_H_G                      0x23
#define LSM6DSO_OUTY_L_G                      0x24
#define LSM6DSO_OUTY_H_G                      0x25
#define LSM6DSO_OUTZ_L_G                      0x26
#define LSM6DSO_OUTZ_H_G                      0x27
#define LSM6DSO_OUTX_L_A                      0x28
#define LSM6DSO_OUTX_H_A                      0x29
#define LSM6DSO_OUTY_L_A                      0x2A
#define LSM6DSO_OUTY_H_A                      0x2B
#define LSM6DSO_OUTZ_L_A                      0x2C
#define LSM6DSO_OUTZ_H_A                      0x2D
#define LSM6DSO_EMB_FUNC_STATUS_MAINPAGE      0x35
#define LSM6DSO_FSM_STATUS_A_MAINPAGE         0x36
#define LSM6DSO_FSM_STATUS_B_MAINPAGE         0x37
#define LSM6DSO_STATUS_MASTER_MAINPAGE        0x39
#define LSM6DSO_FIFO_STATUS1                  0x3A
#define LSM6DSO_FIFO_STATUS2                  0x3B
#define LSM6DSO_TIMESTAMP0                    0x40
#define LSM6DSO_TIMESTAMP1                    0x41
#define LSM6DSO_TIMESTAMP2                    0x42
#define LSM6DSO_TIMESTAMP3                    0x43
#define LSM6DSO_TAP_CFG0                      0x56
#define LSM6DSO_TAP_CFG1                      0x57
#define LSM6DSO_TAP_CFG2                      0x58
#define LSM6DSO_TAP_THS_6D                    0x59
#define LSM6DSO_INT_DUR2                      0x5A
#define LSM6DSO_WAKE_UP_THS                   0x5B
#define LSM6DSO_WAKE_UP_DUR                   0x5C
#define LSM6DSO_FREE_FALL                     0x5D
#define LSM6DSO_MD1_CFG                       0x5E
#define LSM6DSO_MD2_CFG                       0x5F
#define LSM6DSO_I3C_BUS_AVB                   0x62
#define LSM6DSO_INTERNAL_FREQ_FINE            0x63
#define LSM6DSO_INT_OIS                       0x6F
#define LSM6DSO_CTRL1_OIS                     0x70
#define LSM6DSO_CTRL2_OIS                     0x71
#define LSM6DSO_CTRL3_OIS                     0x72
#define LSM6DSO_X_OFS_USR                     0x73
#define LSM6DSO_Y_OFS_USR                     0x74
#define LSM6DSO_Z_OFS_USR                     0x75
#define LSM6DSO_FIFO_DATA_OUT_TAG             0x78
#define LSM6DSO_FIFO_DATA_OUT_X_L             0x79
#define LSM6DSO_FIFO_DATA_OUT_X_H             0x7A
#define LSM6DSO_FIFO_DATA_OUT_Y_L             0x7B
#define LSM6DSO_FIFO_DATA_OUT_Y_H             0x7C
#define LSM6DSO_FIFO_DATA_OUT_Z_L             0x7D
#define LSM6DSO_FIFO_DATA_OUT_Z_H             0x7E

#define LSM6DSO_R_MAX  (LSM6DSO_FIFO_DATA_OUT_Z_H + 1)

typedef struct LSM6DSOState {
    I3CTarget parent_obj;

    /*
     * ccc config
     */
    struct {
        uint16_t mwl;
        uint16_t mrl;
        uint16_t status;
        uint16_t mxds;
        uint8_t ctrl;
    } cfg;
    uint16_t sub_addr;
    uint16_t temperature;
    uint8_t fifo_tag;
    uint8_t regs[LSM6DSO_R_MAX];
} LSM6DSOState;

static int lsm6dso_event(I3CTarget *t, enum I3CEvent event)
{
   switch (event) {
   case I3C_START_SEND:
   case I3C_START_RECV:
   case I3C_STOP:
   case I3C_NACK:
   case I3C_CCC_WR:
   case I3C_CCC_RD:
       break;
   }
   return 0;
}

static int lsm6dso_send(I3CTarget *t, const uint8_t *data, uint32_t num_to_send,
                uint32_t *num_sent)
{
    LSM6DSOState *s = LSM6DSO(t);
    int i;

    g_assert(num_to_send);
    s->sub_addr = data[0];
    if (num_to_send > 1) {
        for (i = 1; i < num_to_send; i++) {
            s->regs[s->sub_addr] = data[i];
            if (s->regs[LSM6DSO_CTRL3_C] & (1 << LSM6DSO_CTRL3_C_IF_INC)) {
                s->sub_addr++;
            }
        }
    }
    return 0;
}

static uint32_t lsm6dso_recv(I3CTarget *t, uint8_t *data, uint32_t num_to_read)
{
    LSM6DSOState *s = LSM6DSO(t);
    int i;
    uint32_t ret = 0;

    for (i = 0; i < num_to_read; i++) {
        switch (s->sub_addr) {
        case LSM6DSO_FIFO_DATA_OUT_TAG:
            s->fifo_tag += s->fifo_tag == 3 ? -2 : 1;
            data[i] = s->fifo_tag << 3;
            switch (s->fifo_tag) {
            case 1:
                s->sub_addr = LSM6DSO_OUTX_L_G;
                break;
            case 2:
                s->sub_addr = LSM6DSO_OUTX_L_A;
                break;
            case 3:
                s->sub_addr = LSM6DSO_OUT_TEMP_L;
                break;
            };
            ret++;
            break;
        case LSM6DSO_FIFO_DATA_OUT_X_L:
        case LSM6DSO_FIFO_DATA_OUT_X_H:
        case LSM6DSO_FIFO_DATA_OUT_Y_L:
        case LSM6DSO_FIFO_DATA_OUT_Y_H:
        case LSM6DSO_FIFO_DATA_OUT_Z_L:
        case LSM6DSO_FIFO_DATA_OUT_Z_H:
            switch (s->fifo_tag) {
            case 1:
                if (i < 2) {
                    data[i] = s->regs[s->sub_addr++];
                }
                break;
            case 2:
            case 3:
               data[i] = s->regs[s->sub_addr++];
               break;
            };
            ret++;
            break;
        default:
            data[i] = s->regs[s->sub_addr];
            ret++;
            break;
        };
        if (s->regs[LSM6DSO_CTRL3_C] & (1 << LSM6DSO_CTRL3_C_IF_INC)) {
            s->sub_addr++;
        }
    }
    return ret;
}

static void lsm6dso_reset(DeviceState *dev)
{
    LSM6DSOState *s = LSM6DSO(dev);

    s->regs[LSM6DSO_WHO_AM_I] = 0x6c;
    s->regs[LSM6DSO_OUTX_L_G] = 0x2C;
    s->regs[LSM6DSO_OUTX_H_G] = 0xA4;
    s->regs[LSM6DSO_OUTY_L_G] = 0x2C;
    s->regs[LSM6DSO_OUTY_H_G] = 0xA4;
    s->regs[LSM6DSO_OUTZ_L_G] = 0x2C;
    s->regs[LSM6DSO_OUTZ_H_G] = 0xA4;

    s->regs[LSM6DSO_OUTX_L_A] = 0x40;
    s->regs[LSM6DSO_OUTX_H_A] = 0x09;
    s->regs[LSM6DSO_OUTY_L_A] = 0x40;
    s->regs[LSM6DSO_OUTY_H_A] = 0x09;
    s->regs[LSM6DSO_OUTZ_L_A] = 0x40;
    s->regs[LSM6DSO_OUTZ_H_A] = 0x09;

    s->regs[LSM6DSO_CTRL3_C] |= 1 << LSM6DSO_CTRL3_C_IF_INC;
}

static int lsm6dso_handle_ccc_read(I3CTarget *t, uint8_t *data,
                                   uint32_t num_to_read,
                                   uint32_t *num_read)
{
    LSM6DSOState *s = LSM6DSO(t);

    switch (t->curr_ccc) {
    case I3C_CCCD_GETXTIME:
        break;
    case I3C_CCCD_GETMWL:
        data[0] = s->cfg.mwl >> 8;
        data[1] = s->cfg.mwl & 0xF;
        *num_read = 2;
        break;
    case I3C_CCCD_GETMRL:
        data[0] = s->cfg.mrl >> 8;
        data[1] = s->cfg.mrl & 0xF;
        *num_read = 2;
        break;
    case I3C_CCCD_GETSTATUS:
        data[0] = s->cfg.status >> 8;
        data[1] = s->cfg.status & 0xF;
        *num_read = 2;
        break;
    case I3C_CCCD_GETMXDS:
        break;
    default:
        break;
    };
    return 0;
}

static int lsm6dso_handle_ccc_write(I3CTarget *t, const uint8_t *data,
                            uint32_t num_to_send, uint32_t *num_sent)
{
    LSM6DSOState *s = LSM6DSO(t);

    switch (t->curr_ccc) {
    case I3C_CCC_ENEC:
        t->ccc_byte_offset++;
        *num_sent = 1;
        /*
         * fall through
         */
    case I3C_CCCD_ENEC:
        if (t->ccc_byte_offset == 1) {
            s->cfg.ctrl |= data[*num_sent];
            *num_sent += 1;
            t->ccc_byte_offset++;
        }
        break;
    case I3C_CCC_DISEC:
        t->ccc_byte_offset++;
        *num_sent = 1;
       /*
        * fall through
        */
    case I3C_CCCD_DISEC:
        s->cfg.ctrl &= ~((data[*num_sent] & 0xF));
        *num_sent += 1;
        t->ccc_byte_offset++;
        break;
    case I3C_CCC_ENTAS0:
    case I3C_CCCD_ENTAS0:
    case I3C_CCC_ENTAS1:
    case I3C_CCCD_ENTAS1:
    case I3C_CCC_ENTAS2:
    case I3C_CCCD_ENTAS2:
    case I3C_CCC_ENTAS3:
    case I3C_CCCD_ENTAS3:
        *num_sent = num_to_send;
        break;
    case I3C_CCCD_SETXTIME:
        break;
    case I3C_CCC_SETMRL:
        t->ccc_byte_offset++;
        *num_sent = 1;
        /*
         * Fall through
         */
    case I3C_CCCD_SETMRL:
        /*
         * 0: mrl msb
         * 1: mrl lsb
         */
        s->cfg.mrl = 0;
        s->cfg.mrl = data[*num_sent] << 8;
        s->cfg.mrl |= data[*num_sent + 1];
        *num_sent += 2;
        break;
    case I3C_CCC_SETMWL:
        t->ccc_byte_offset++;
        *num_sent = 1;
        /*
         * Fall through
         */
    case I3C_CCCD_SETMWL:
        s->cfg.mwl = 0;
        s->cfg.mwl = data[*num_sent] << 8;
        s->cfg.mwl |= data[*num_sent + 1];
        *num_sent += 2;
        break;
    default:
        break;
    };
    return 0;
}


static void lsm6dso_initfn(Object *obj)
{
    I3CTarget *t = I3C_TARGET(obj);

    qdev_prop_set_uint64(DEVICE(t), "pid", 0x0B106C000802);
    qdev_prop_set_uint8(DEVICE(t), "bcr", 0x7);
    qdev_prop_set_uint8(DEVICE(t), "dcr", 0x44);
}

static void lsm6dso_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I3CTargetClass *k = I3C_TARGET_CLASS(klass);

    dc->reset = lsm6dso_reset;
    k->event = lsm6dso_event;
    k->recv = lsm6dso_recv;
    k->send = lsm6dso_send;
    k->handle_ccc_read = lsm6dso_handle_ccc_read;
    k->handle_ccc_write = lsm6dso_handle_ccc_write;
}

static const TypeInfo lsm6dso_info = {
    .name = TYPE_LSM6DSO,
    .parent = TYPE_I3C_TARGET,
    .instance_size = sizeof(LSM6DSOState),
    .instance_init = lsm6dso_initfn,
    .class_init = lsm6dso_class_init,
};

static void lsm6dso_register_types(void)
{
    type_register_static(&lsm6dso_info);
}

type_init(lsm6dso_register_types)
