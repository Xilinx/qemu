/*
 * QEMU Ethernet MDIO bus & PHY models
 *
 * Copyright (c) 2008 Edgar E. Iglesias (edgari@xilinx.com),
 * Copyright (c) 2008 Grant Likely (grant.likely@secretlab.ca),
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

#include "qemu/osdep.h"
#include "hw/mdio/mdio_slave.h"
#include "hw/mdio/eth_phy.h"
#include "qemu/log.h"

#ifndef ETH_PHY_DEBUG
#define ETH_PHY_DEBUG 0
#endif

#define DPRINT(fmt, args...) \
    do { \
        if (ETH_PHY_DEBUG) { \
            qemu_log("%s: " fmt, __func__, ## args); \
        } \
    } while (0)

static void eth_phy_reset(DeviceState *dev)
{
    EthPhy *s = ETHPHY(dev);

    /* If AutoNegotiation is supported */
    if (s->part->autoneg) {
        /* Show as AutoNeg capable & show as its completed autoneg */
        s->regs[PHY_STATUS] |= M(PHY_STAT_AUTONEG_CAP) |
                               M(PHY_STAT_AUTONEG_COMP) |
                               M(PHY_STAT_EXT_CAP);

        s->regs[PHY_CTRL] |= M(PHY_CTRLREG_AUTONEG_EN);
        /* Supports IEEE 802.3 std and 10BaseT and 10BaseTX Full and Half
         *  duplex
         */
        s->regs[PHY_AUTONEG_ADV] |= 0x01E1;
        s->regs[PHY_LP_ABILITY]  |= 0xCDE1;

        s->regs[PHY_1000T_CTRL] |= 0x0300;
        s->regs[PHY_1000T_STATUS] |= 0x7C00;

        /* Support all modes in gmii mode*/
        if (s->part->gmii) {
            s->regs[PHY_EXT_STATUS] |=  M(PHY_EXT_STAT_1000BT_HD) |
                                        M(PHY_EXT_STAT_1000BT_FD) |
                                        M(PHY_EXT_STAT_1000BX_HD) |
                                        M(PHY_EXT_STAT_1000BX_FD);
            s->regs[PHY_STATUS] |= M(PHY_STAT_100BX_FD) |
                                   M(PHY_STAT_100BX_HD) |
                                   M(PHY_STAT_100B_T2_FD) |
                                   M(PHY_STAT_100B_T2_HD) |
                                   M(PHY_STAT_10MBPS_HD) |
                                   M(PHY_STAT_10MBPS_FD);
            /* Show 1000Mb/s as default */
            s->regs[PHY_CTRL] |= M(PHY_CTRLREG_SPEED_SEL_MSB);

            /* Supports Extended status */
            s->regs[PHY_STATUS] |= M(PHY_STAT_EXT_STAT_CAP);
            s->regs[PHY_SPEC_STATUS] |= 0xBC00;
        } else {
            s->regs[PHY_STATUS] |= M(PHY_STAT_100BX_FD) |
                                   M(PHY_STAT_100BX_HD) |
                                   M(PHY_STAT_100B_T2_FD) |
                                   M(PHY_STAT_100B_T2_HD) |
                                   M(PHY_STAT_10MBPS_HD) |
                                   M(PHY_STAT_10MBPS_FD);
            /* Show 100Mb/s as default */
            s->regs[PHY_CTRL] |= M(PHY_CTRLREG_SPEED_SEL_LSB);
            s->regs[PHY_SPEC_STATUS] |= 0x7C00;
        }

    }
    s->link = true;
    s->regs[PHY_STATUS] |= M(PHY_STAT_LINK_STAT);
}

static int eth_phy_read(MDIOSlave *slave, uint8_t req)
{
    EthPhy *phy = ETHPHY(slave);
    int regnum;
    unsigned r = 0;

    regnum = req & 0x1f;

    switch (regnum) {
    case PHY_STATUS:
        if (!phy->link) {
            break;
        }
        r = phy->regs[PHY_STATUS];
        break;
    default:
        r = phy->regs[regnum];
        break;
    }
    DPRINT("%s %x = reg[%d]\r\n", __func__, r, regnum);
    return r;
}

static int eth_phy_write(MDIOSlave *slave, uint8_t req, uint8_t data)
{
    EthPhy *phy = ETHPHY(slave);
    int regnum = req & 0x1f;
    uint16_t mask = phy->regs_readonly_mask[regnum];

    DPRINT("%s reg[%d] = %x; mask=%x\n", __func__, regnum, data, mask);
    switch (regnum) {
    case PHY_CTRL:
        /* Update Registers on Fall through */
        if (data & PHY_CTRL_RST) {
            eth_phy_reset(DEVICE(phy));
        }
    default:
        phy->regs[regnum] = (phy->regs[regnum] & mask) | (data & ~mask);
        break;
    }
    return 0;
}

static void eth_phy_init(Object *Obj)
{
    EthPhy *s = ETHPHY(Obj);
    EthPhyClass *k = ETHPHY_GET_CLASS(Obj);

    s->part = k->part;
    /* PHY Id. */
    s->regs[PHY_ID1] = s->part->phy_id1;
    s->regs[PHY_ID2] = s->part->phy_id2;

    s->regs_readonly_mask = default_readonly_mask;
}

static void eth_phy_class_init(ObjectClass *klass, void *data)
{
    MDIOSlaveClass *sc = MDIO_SLAVE_CLASS(klass);

    sc->send = eth_phy_write;
    sc->recv = eth_phy_read;
}

static void phy_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    EthPhyClass *k = ETHPHY_CLASS(klass);

    k->part = data;
    dc->reset = eth_phy_reset;
}

static const TypeInfo eth_phy_info = {
    .name = TYPE_ETH_PHY,
    .parent = TYPE_MDIO_SLAVE,
    .instance_size = sizeof(EthPhy),
    .class_size = sizeof(EthPhyClass),
    .instance_init = eth_phy_init,
    .class_init = eth_phy_class_init,
    /* This cannot be directly initiated as it requires a MDIO slave */
    .abstract = true,
};

static void eth_phy_register_types(void)
{
    int i;

    type_register_static(&eth_phy_info);

    for (i = 0; i < ARRAY_SIZE(devices); i++) {
        TypeInfo ti = {
            .name       = devices[i].partname,
            .parent     = TYPE_ETH_PHY,
            .class_init = phy_class_init,
            .class_data = (void *)&devices[i],
        };
        type_register(&ti);
    }
}

type_init(eth_phy_register_types)
