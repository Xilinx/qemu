/*
 * QEMU Ethernet extended MDIO bus & PHY models (IEEE 802.3 clause 45)
 *
 * Copyright (c) 2023, Advanced Micro Device, Inc.
 *    Luc Michel <luc.michel@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "hw/mdio/mdio_slave.h"
#include "hw/mdio/ext_phy.h"
#include "qemu/log.h"
#include "hw/registerfields.h"

enum ExtPhyMMD {
    MMD_PMA_PMD = 1,
    MMD_PMA_WIS = 2,
    MMD_PMA_PCS = 3,
    MMD_PMA_PHY_XS = 4,
    MMD_PMA_DTE_XS = 5,
    MMD_PMA_TC = 6,
    MMD_PMA_AN = 7,
    MMD_SEP_PMA1 = 8,
    MMD_SEP_PMA2 = 9,
    MMD_SEP_PMA3 = 10,
    MMD_SEP_PMA4 = 11,
    MMD_OFDM_PMA_PMD = 12,
    MMD_POWER_UNIT = 13,
    MMD_CLAUSE22_EXT = 29,
    MMD_VENDOR1 = 30,
    MMD_VENDOR2 = 31,
};

typedef struct MMDAccessFn {
    void (*reset)(ExtPhy *);
    void (*read)(ExtPhy *, MDIOFrame *);
    void (*write)(ExtPhy *, const MDIOFrame *);
} MMDAccessFn;

/* Registers common to all MMDs */
REG8(DEVS_IN_PKG0, 0x5)
    FIELD(DEVS_IN_PKG0, CLAUSE22, 0, 1)
    FIELD(DEVS_IN_PKG0, PMD_PMA, 1, 1)
    FIELD(DEVS_IN_PKG0, WIS, 2, 1)
    FIELD(DEVS_IN_PKG0, PCS, 3, 1)
    FIELD(DEVS_IN_PKG0, PHY_XS, 4, 1)
    FIELD(DEVS_IN_PKG0, DTE_XS, 5, 1)
    FIELD(DEVS_IN_PKG0, TC, 6, 1)
    FIELD(DEVS_IN_PKG0, AN, 7, 1)
    FIELD(DEVS_IN_PKG0, SEP_PMA1, 8, 1)
    FIELD(DEVS_IN_PKG0, SEP_PMA2, 9, 1)
    FIELD(DEVS_IN_PKG0, SEP_PMA3, 10, 1)
    FIELD(DEVS_IN_PKG0, SEP_PMA4, 11, 1)
    FIELD(DEVS_IN_PKG0, OFDM, 12, 1)
    FIELD(DEVS_IN_PKG0, POWER_UNIT, 13, 1)

REG8(DEVS_IN_PKG1, 0x6)
    FIELD(DEVS_IN_PKG1, CLAUSE22_EXT, 13, 1)
    FIELD(DEVS_IN_PKG1, VENDOR_1, 14, 1)
    FIELD(DEVS_IN_PKG1, VENDOR_2, 15, 1)


/* PMA/PMD registers */
REG8(PMA_PMD_CTRL1, 0x0)
    FIELD(PMA_PMD_CTRL1, LOCAL_LOOPBACK, 0, 1)
    FIELD(PMA_PMD_CTRL1, REMOTE_LOOPBACK, 1, 1)
    FIELD(PMA_PMD_CTRL1, SPEED_SEL, 2, 4)
    FIELD(PMA_PMD_CTRL1, SPEED_SEL_LSB, 6, 1)
    FIELD(PMA_PMD_CTRL1, LOW_POWER, 11, 1)
    FIELD(PMA_PMD_CTRL1, SPEED_SEL_MSB, 13, 1)
    FIELD(PMA_PMD_CTRL1, RESET, 15, 1)
#define PMA_PMD_CTRL1_WRITE_MASK \
        (R_PMA_PMD_CTRL1_LOCAL_LOOPBACK_MASK \
         | R_PMA_PMD_CTRL1_REMOTE_LOOPBACK_MASK \
         | R_PMA_PMD_CTRL1_SPEED_SEL_MASK \
         | R_PMA_PMD_CTRL1_SPEED_SEL_LSB_MASK \
         | R_PMA_PMD_CTRL1_LOW_POWER_MASK \
         | R_PMA_PMD_CTRL1_SPEED_SEL_MSB_MASK)
#define CTRL1_SPEED_10G 0

REG8(PMA_PMD_STATUS1, 0x1)
    FIELD(PMA_PMD_STATUS1, LOW_POWER_ABILITY, 1, 1)
    FIELD(PMA_PMD_STATUS1, LINK_STA, 2, 1)
    FIELD(PMA_PMD_STATUS1, FAULT, 7, 1)
    FIELD(PMA_PMD_STATUS1, PEASA, 8, 1)
    FIELD(PMA_PMD_STATUS1, PIASA, 9, 1)

REG8(PMA_PMD_DEVID0, 0x2)
REG8(PMA_PMD_DEVID1, 0x3)

REG8(PMA_PMD_CTRL2, 0x7)
    FIELD(PMA_PMD_CTRL2, TYPE, 0, 7)
    FIELD(PMA_PMD_CTRL2, PEASE, 8, 1)
    FIELD(PMA_PMD_CTRL2, PIASE, 9, 1)
#define PMA_PMD_CTRL2_WRITE_MASK \
        (R_PMA_PMD_CTRL2_TYPE_MASK \
         | R_PMA_PMD_CTRL2_PEASE_MASK \
         | R_PMA_PMD_CTRL2_PIASE_MASK)
#define CTRL2_TYPE_10GBASE_T 0x9

REG8(PMA_PMD_STATUS2, 0x8)
    FIELD(PMA_PMD_STATUS2, LOCAL_LOOPBACK_ABILITY, 0, 1)
    FIELD(PMA_PMD_STATUS2, 10GBASE_EW, 1, 1)
    FIELD(PMA_PMD_STATUS2, 10GBASE_LW, 2, 1)
    FIELD(PMA_PMD_STATUS2, 10GBASE_SW, 3, 1)
    FIELD(PMA_PMD_STATUS2, 10GBASE_LX4, 4, 1)
    FIELD(PMA_PMD_STATUS2, 10GBASE_ER, 5, 1)
    FIELD(PMA_PMD_STATUS2, 10GBASE_LR, 6, 1)
    FIELD(PMA_PMD_STATUS2, 10GBASE_SR, 7, 1)
    FIELD(PMA_PMD_STATUS2, XMIT_DIS_ABILITY, 8, 1)
    FIELD(PMA_PMD_STATUS2, EXT_ABILITY, 9, 1)
    FIELD(PMA_PMD_STATUS2, RECV_FAULT, 10, 1)
    FIELD(PMA_PMD_STATUS2, XMIT_FAULT, 11, 1)
    FIELD(PMA_PMD_STATUS2, RECV_FAULT_ABILITY, 12, 1)
    FIELD(PMA_PMD_STATUS2, XMIT_FAULT_ABILITY, 13, 1)
    FIELD(PMA_PMD_STATUS2, DEV_PRESENT, 14, 2)
#define STATUS2_DEV_PRESENT 0x2

REG8(PMA_PMD_EXT_ABILITY, 0xb)
    FIELD(PMA_PMD_EXT_ABILITY, 10GBASE_CX4, 0, 1)
    FIELD(PMA_PMD_EXT_ABILITY, 10GBASE_LRM, 1, 1)
    FIELD(PMA_PMD_EXT_ABILITY, 10GBASE_T, 2, 1)
    FIELD(PMA_PMD_EXT_ABILITY, 10GBASE_KX4, 3, 1)
    FIELD(PMA_PMD_EXT_ABILITY, 10GBASE_KR, 4, 1)
    FIELD(PMA_PMD_EXT_ABILITY, 1000BASE_T, 5, 1)
    FIELD(PMA_PMD_EXT_ABILITY, 1000BASE_KX, 6, 1)
    FIELD(PMA_PMD_EXT_ABILITY, 100BASE_TX, 7, 1)
    FIELD(PMA_PMD_EXT_ABILITY, 10BASE_T, 8, 1)
    FIELD(PMA_PMD_EXT_ABILITY, P2MP, 9, 1)
    FIELD(PMA_PMD_EXT_ABILITY, 40G_100G, 10, 1)
    FIELD(PMA_PMD_EXT_ABILITY, BASE_T1, 11, 1)
    FIELD(PMA_PMD_EXT_ABILITY, 25G, 12, 1)
    FIELD(PMA_PMD_EXT_ABILITY, 200G_400G, 13, 1)
    FIELD(PMA_PMD_EXT_ABILITY, 2DOT5_5G, 14, 1)
    FIELD(PMA_PMD_EXT_ABILITY, BASE_H, 15, 1)

REG8(PMA_PMD_2DOT5_5G_EXT_ABILITY, 0x15)
    FIELD(PMA_PMD_2DOT5_5G_EXT_ABILITY, 2DOT5GBASE_T, 0, 1)
    FIELD(PMA_PMD_2DOT5_5G_EXT_ABILITY, 5GBASE_T, 1, 1)

static inline bool has_cap(ExtPhy *s, ExtPhyCapability cap)
{
    return EXT_PHY_GET_CLASS(s)->part->cap & cap;
}

static void pma_pmd_reset(ExtPhy *s)
{
    /* Reset in 10G mode */
    s->pma_pmd.ctrl1 = FIELD_DP16(0, PMA_PMD_CTRL1, SPEED_SEL, CTRL1_SPEED_10G);
    s->pma_pmd.ctrl1 = FIELD_DP16(s->pma_pmd.ctrl1,
                                  PMA_PMD_CTRL1, SPEED_SEL_LSB, 1);
    s->pma_pmd.ctrl1 = FIELD_DP16(s->pma_pmd.ctrl1,
                                  PMA_PMD_CTRL1, SPEED_SEL_MSB, 1);

    s->pma_pmd.ctrl2 = FIELD_DP16(0, PMA_PMD_CTRL2, TYPE, CTRL2_TYPE_10GBASE_T);
}

static void pma_pmd_read(ExtPhy *s, MDIOFrame *frame)
{
    switch (s->latched_addr[MMD_PMA_PMD]) {
    case A_PMA_PMD_CTRL1:
        frame->data = s->pma_pmd.ctrl1;
        break;

    case A_PMA_PMD_STATUS1:
        frame->data =
            R_PMA_PMD_STATUS1_LOW_POWER_ABILITY_MASK
            | R_PMA_PMD_STATUS1_LINK_STA_MASK
            ;
        break;

    case A_PMA_PMD_DEVID0:
        frame->data = EXT_PHY_GET_CLASS(s)->part->phy_id1;
        break;

    case A_PMA_PMD_DEVID1:
        frame->data = EXT_PHY_GET_CLASS(s)->part->phy_id2;
        break;

    case A_PMA_PMD_CTRL2:
        frame->data = s->pma_pmd.ctrl2;
        break;

    case A_PMA_PMD_STATUS2:
        frame->data = R_PMA_PMD_STATUS2_LOCAL_LOOPBACK_ABILITY_MASK
            | R_PMA_PMD_STATUS2_XMIT_DIS_ABILITY_MASK
            | R_PMA_PMD_STATUS2_RECV_FAULT_ABILITY_MASK
            | R_PMA_PMD_STATUS2_XMIT_FAULT_ABILITY_MASK
            | R_PMA_PMD_STATUS2_EXT_ABILITY_MASK
            ;

        if (has_cap(s, EXT_PHY_CAP_10G)) {
            frame->data |=
                R_PMA_PMD_STATUS2_10GBASE_EW_MASK
                | R_PMA_PMD_STATUS2_10GBASE_LW_MASK
                | R_PMA_PMD_STATUS2_10GBASE_SW_MASK
                | R_PMA_PMD_STATUS2_10GBASE_LX4_MASK
                | R_PMA_PMD_STATUS2_10GBASE_ER_MASK
                | R_PMA_PMD_STATUS2_10GBASE_LR_MASK
                | R_PMA_PMD_STATUS2_10GBASE_SR_MASK;
        }

        frame->data |= FIELD_DP16(frame->data, PMA_PMD_STATUS2, DEV_PRESENT,
                                 STATUS2_DEV_PRESENT);
        break;

    case A_PMA_PMD_EXT_ABILITY:
        frame->data = R_PMA_PMD_EXT_ABILITY_2DOT5_5G_MASK;

        if (has_cap(s, EXT_PHY_CAP_10G)) {
            frame->data |=
                R_PMA_PMD_EXT_ABILITY_10GBASE_CX4_MASK
                | R_PMA_PMD_EXT_ABILITY_10GBASE_LRM_MASK
                | R_PMA_PMD_EXT_ABILITY_10GBASE_T_MASK
                | R_PMA_PMD_EXT_ABILITY_10GBASE_KX4_MASK
                | R_PMA_PMD_EXT_ABILITY_10GBASE_KR_MASK
                ;
        }

        if (has_cap(s, EXT_PHY_CAP_1G)) {
            frame->data |=
                R_PMA_PMD_EXT_ABILITY_1000BASE_T_MASK
                | R_PMA_PMD_EXT_ABILITY_1000BASE_KX_MASK
                ;
        }

        if (has_cap(s, EXT_PHY_CAP_100M)) {
            frame->data |= R_PMA_PMD_EXT_ABILITY_100BASE_TX_MASK;
        }

        if (has_cap(s, EXT_PHY_CAP_10M)) {
            frame->data |= R_PMA_PMD_EXT_ABILITY_10BASE_T_MASK;
        }

        break;

    case A_PMA_PMD_2DOT5_5G_EXT_ABILITY:
        frame->data = 0;

        if (has_cap(s, EXT_PHY_CAP_2DOT5G)) {
            frame->data |= R_PMA_PMD_2DOT5_5G_EXT_ABILITY_2DOT5GBASE_T_MASK;
        }

        if (has_cap(s, EXT_PHY_CAP_5G)) {
            frame->data |= R_PMA_PMD_2DOT5_5G_EXT_ABILITY_5GBASE_T_MASK;
        }
        break;

    default:
        frame->data = 0;
    }
}

static void pma_pmd_write(ExtPhy *s, const MDIOFrame *frame)
{
    switch (s->latched_addr[MMD_PMA_PMD]) {
    case A_PMA_PMD_CTRL1:
        s->pma_pmd.ctrl1 = frame->data & PMA_PMD_CTRL1_WRITE_MASK;

        if (frame->data & R_PMA_PMD_CTRL1_RESET_MASK) {
            /*
             * clause 45 stats that a PMA/PMD reset may reset other MMDs. Let's
             * reset the whole PHY.
             */
            device_cold_reset(DEVICE(s));
        }
        break;

    case A_PMA_PMD_CTRL2:
        s->pma_pmd.ctrl2 = frame->data & PMA_PMD_CTRL2_WRITE_MASK;
        break;

    default:
        break;
    }
}

static const MMDAccessFn MMD_ACCESS_FN[EXT_PHY_NUM_MMD] = {
    [MMD_PMA_PMD] = {
        .reset = pma_pmd_reset,
        .read = pma_pmd_read,
        .write = pma_pmd_write,
    },
};

static void mmd_access(ExtPhy *s, MDIOFrame *frame, size_t mmd, uint16_t addr)
{
    const MMDAccessFn *fn;

    fn = &MMD_ACCESS_FN[mmd];

    switch (frame->op) {
    case MDIO_OP_READ:
    case MDIO_OP_READ_POST_INCR:
        fn->read(s, frame);
        break;

    case MDIO_OP_WRITE:
        fn->write(s, frame);
        break;

    default:
        g_assert_not_reached();
    }
}

/* Access to a register common to all MMDs (devices in package reg) */
static void common_access(ExtPhy *s, MDIOFrame *frame, uint16_t addr)
{
    if (!mdio_frame_is_read(frame)) {
        /* those registers are RO */
        return;
    }

    switch (addr) {
    case A_DEVS_IN_PKG0:
        frame->data = R_DEVS_IN_PKG0_PMD_PMA_MASK;
        break;

    case A_DEVS_IN_PKG1:
        frame->data = 0;
        break;

    default:
        g_assert_not_reached();
    }
}

static inline void set_frame_phy_status(ExtPhy *s, MDIOFrame *frame)
{
    frame->phy_status.present = true;
    frame->phy_status.local_loopback = FIELD_EX16(s->pma_pmd.ctrl1,
                                                  PMA_PMD_CTRL1,
                                                  LOCAL_LOOPBACK);
    frame->phy_status.remote_loopback = FIELD_EX16(s->pma_pmd.ctrl1,
                                                   PMA_PMD_CTRL1,
                                                   REMOTE_LOOPBACK);
}

static void ext_phy_mdio_transfer(MDIOSlave *slave, MDIOFrame *frame)
{
    ExtPhy *s = EXT_PHY(slave);
    size_t mmd;
    uint16_t addr;

    if (frame->st == MDIO_ST_CLAUSE_22) {
        mdio_frame_invalid_dst(frame);
        return;
    }

    if (frame->addr1 >= EXT_PHY_NUM_MMD) {
        mdio_frame_invalid_dst(frame);
        return;
    }

    /* clause 45: addr1 is the device (MMD) address */
    mmd = frame->addr1;

    if (!MMD_ACCESS_FN[mmd].read) {
        /* MMD not implemented */
        mdio_frame_invalid_dst(frame);
        return;
    }

    if (frame->op == MDIO_OP_ADDR) {
        s->latched_addr[mmd] = frame->data;
        return;
    }

    addr = s->latched_addr[mmd];

    if (addr == A_DEVS_IN_PKG0 ||
        addr == A_DEVS_IN_PKG1) {
        common_access(s, frame, addr);
    } else {
        mmd_access(s, frame, mmd, addr);
    }

    if (frame->op == MDIO_OP_READ_POST_INCR && addr < UINT16_MAX) {
        s->latched_addr[mmd]++;
    }

    set_frame_phy_status(s, frame);
}

static void ext_phy_reset(DeviceState *dev)
{
    ExtPhy *s = EXT_PHY(dev);
    size_t i;

    for (i = 0; i < EXT_PHY_NUM_MMD; i++) {
        s->latched_addr[i] = 0;

        if (MMD_ACCESS_FN[i].reset) {
            MMD_ACCESS_FN[i].reset(s);
        }
    }
}

static void ext_phy_class_init(ObjectClass *klass, void *data)
{
    MDIOSlaveClass *sc = MDIO_SLAVE_CLASS(klass);

    sc->transfer = ext_phy_mdio_transfer;
}

static void phy_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ExtPhyClass *k = EXT_PHY_CLASS(klass);

    k->part = data;
    dc->reset = ext_phy_reset;
}

static const TypeInfo ext_phy_info = {
    .name = TYPE_EXT_PHY,
    .parent = TYPE_MDIO_SLAVE,
    .instance_size = sizeof(ExtPhy),
    .class_size = sizeof(ExtPhyClass),
    .class_init = ext_phy_class_init,
    /* This cannot be directly initiated as it requires a MDIO slave */
    .abstract = true,
};

static void ext_phy_register_types(void)
{
    int i;

    type_register_static(&ext_phy_info);

    for (i = 0; i < ARRAY_SIZE(devices); i++) {
        const TypeInfo ti = {
            .name       = devices[i].partname,
            .parent     = TYPE_EXT_PHY,
            .class_init = phy_class_init,
            .class_data = (void *)&devices[i],
        };
        type_register(&ti);
    }
}

type_init(ext_phy_register_types)
