/*
 * QEMU Ethernet MDIO bus & PHY models
 *
 * Copyright (c) 2008 Edgar E. Iglesias (edgari@xilinx.com),
 *                          Grant Likely (grant.likely@secretlab.ca),
 * Copyright (c) 2016 Xilinx Inc.
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
 *
 */

#ifndef ETH_PHY_H
#define ETH_PHY_H

#include "qemu-common.h"

/* PHY MII Register/Bit Definitions */
/* PHY Registers defined by IEEE */
#define PHY_CTRL         0x00 /* Control Register */
#define PHY_STATUS       0x01 /* Status Regiser */
#define PHY_ID1          0x02 /* Phy Id Reg (word 1) */
#define PHY_ID2          0x03 /* Phy Id Reg (word 2) */
#define PHY_AUTONEG_ADV  0x04 /* Autoneg Advertisement */
#define PHY_LP_ABILITY   0x05 /* Link Partner Ability (Base Page) */
#define PHY_AUTONEG_EXP  0x06 /* Autoneg Expansion Reg */
#define PHY_NEXT_PAGE_TX 0x07 /* Next Page TX */
#define PHY_LP_NEXT_PAGE 0x08 /* Link Partner Next Page */
#define PHY_1000T_CTRL   0x09 /* 1000Base-T Control Reg */
#define PHY_1000T_STATUS 0x0A /* 1000Base-T Status Reg */
#define PHY_EXT_STATUS   0x0F /* Extended Status Reg */
#define PHY_SPEC_CTRL    0x10 /* PHY Specific control reg */
#define PHY_SPEC_STATUS  0x11 /* PHY Specific status reg */
#define NUM_PHY_REGS     0x1F  /* 5 bit address bus (0-0x1F) */

/*Control Register bitfeild offsets*/
#define PHY_CTRL_REG_UNIDIR_EN      5
#define PHY_CTRLREG_SPEED_SEL_MSB   6
#define PHY_CTRLREG_COLLISION_TEST  7
#define PHY_CTRLREG_DUPLEX_MODE     8
#define PHY_CTRLREG_RST_AUTONEG     9
#define PHY_CTRLREG_ISOLATE         10
#define PHY_CTRLREG_POWER_DWN       11
#define PHY_CTRLREG_AUTONEG_EN      12
#define PHY_CTRLREG_SPEED_SEL_LSB   13
#define PHY_CTRLREG_LOOPBACK        14
#define PHY_CTRLREG_RESET           15

/*Status Register bitfeild offsets */
#define PHY_STAT_EXT_CAP        0
#define PHY_STAT_JAB_DETECT     1
#define PHY_STAT_LINK_STAT      2
#define PHY_STAT_AUTONEG_CAP    3
#define PHY_STAT_REMOTE_FAL     4
#define PHY_STAT_AUTONEG_COMP   5
#define PHY_STAT_PREM_SUPPRESS  6
#define PHY_STAT_UNIDIR_CAP     7
#define PHY_STAT_EXT_STAT_CAP   8
#define PHY_STAT_100B_T2_HD     9
#define PHY_STAT_100B_T2_FD     10
#define PHY_STAT_10MBPS_HD      11
#define PHY_STAT_10MBPS_FD      12
#define PHY_STAT_100BX_HD       13
#define PHY_STAT_100BX_FD       14
#define PHY_STAT_T4             15

/* EXT Status Register bitfeild offsets*/
#define PHY_EXT_STAT_1000BT_HD  12
#define PHY_EXT_STAT_1000BT_FD  13
#define PHY_EXT_STAT_1000BX_HD  14
#define PHY_EXT_STAT_1000BX_FD  15

/* Prepare a bit mask */
#define M(X) (uint16_t) (1 << X)

#define PHY_CTRL_RST            0x8000 /* PHY reset command */
#define PHY_CTRL_ANEG_RST       0x0200 /* Autonegotiation reset command */

/* PHY Advertisement control and remote capability registers (same bitfields) */
#define PHY_ADVERTISE_10HALF    0x0020  /* Try for 10mbps half-duplex  */
#define PHY_ADVERTISE_10FULL    0x0040  /* Try for 10mbps full-duplex  */
#define PHY_ADVERTISE_100HALF   0x0080  /* Try for 100mbps half-duplex */
#define PHY_ADVERTISE_100FULL   0x0100  /* Try for 100mbps full-duplex */

#define TYPE_ETH_PHY "eth-phy"

#define ETHPHY(obj) OBJECT_CHECK(EthPhy, (obj), TYPE_ETH_PHY)
#define ETHPHY_CLASS(klass) OBJECT_CLASS_CHECK(EthPhyClass, (klass),\
                                               TYPE_ETH_PHY)
#define ETHPHY_GET_CLASS(obj) \
     OBJECT_GET_CLASS(EthPhyClass, (obj), TYPE_ETH_PHY)

static const uint16_t default_readonly_mask[32] = {
    [PHY_CTRL] = PHY_CTRL_RST | PHY_CTRL_ANEG_RST,
    [PHY_ID1] = 0xffff,
    [PHY_ID2] = 0xffff,
    [PHY_LP_ABILITY] = 0xffff,
    [PHY_SPEC_STATUS] = 0xffff,
};

typedef struct PhyPartInfo {
    const char *partname;

    uint16_t phy_id1;
    uint16_t phy_id2;

    bool autoneg;
    bool gmii;
} PhyPartInfo;

#define PHYINFO(_part_name, _id1, _id2, _autoneg, _gmii) \
    .partname = (_part_name),\
    .phy_id1 = (_id1),\
    .phy_id2 = (_id2),\
    .autoneg = (_autoneg),\
    .gmii = (_gmii),\

static const PhyPartInfo devices[] = {
    { PHYINFO("88e1116r", 0x0141, 0x0e50, 1, 1) },
    { PHYINFO("88e1116",  0x0141, 0x0e50, 1, 1) },
    { PHYINFO("dp83867",  0x2000, 0xa231, 1, 1) },
    { PHYINFO("88e1118r", 0x0141, 0x0e10, 1, 1) },
    { PHYINFO("88e1510",  0x0141, 0x0dd0, 1, 1) },
};


typedef struct EthPhy {
    MDIOSlave parent_object;

    /* Only the basic registers, rest of them in Vendor */
    uint16_t regs[NUM_PHY_REGS];
    const uint16_t *regs_readonly_mask; /* 0=writable, 1=read-only */

    bool link;

    PhyPartInfo *part;
} EthPhy;

typedef struct EthPhyClass {
    MDIOSlaveClass parent_class;

    PhyPartInfo *part;
} EthPhyClass;

#endif
