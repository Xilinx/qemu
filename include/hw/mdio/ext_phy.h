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

#ifndef HW_MDIO_EXT_PHY_H
#define HW_MDIO_EXT_PHY_H

#define TYPE_EXT_PHY "ext-phy"

#define EXT_PHY(obj) OBJECT_CHECK(ExtPhy, (obj), TYPE_EXT_PHY)
#define EXT_PHY_CLASS(klass) OBJECT_CLASS_CHECK(ExtPhyClass, (klass),\
                                                TYPE_EXT_PHY)
#define EXT_PHY_GET_CLASS(obj) \
     OBJECT_GET_CLASS(ExtPhyClass, (obj), TYPE_EXT_PHY)

#define EXT_PHY_NUM_MMD 32

typedef enum ExtPhyCapability {
    EXT_PHY_CAP_2DOT5G = 1 << 0,
    EXT_PHY_CAP_5G = 1 << 1,
    EXT_PHY_CAP_10G = 1 << 2,
    EXT_PHY_CAP_1G = 1 << 3,
    EXT_PHY_CAP_100M = 1 << 4,
    EXT_PHY_CAP_10M = 1 << 5,
} ExtPhyCapability;

typedef struct ExtPhyPartInfo {
    const char *partname;

    uint16_t phy_id1;
    uint16_t phy_id2;
    uint64_t cap;

} ExtPhyPartInfo;

#define EXTPHYINFO(_part_name, _id1, _id2, _cap) \
    .partname = (_part_name),                    \
    .phy_id1 = (_id1),                           \
    .phy_id2 = (_id2),                           \
    .cap = (_cap)

static const ExtPhyPartInfo devices[] = {
    { EXTPHYINFO("phy-clause45-generic", 0x0000, 0x0000, EXT_PHY_CAP_10G) },
};


typedef struct ExtPhy {
    MDIOSlave parent_object;

    uint16_t latched_addr[EXT_PHY_NUM_MMD];

    struct {
        uint16_t ctrl1;
        uint16_t ctrl2;
    } pma_pmd;
} ExtPhy;

typedef struct ExtPhyClass {
    MDIOSlaveClass parent_class;

    ExtPhyPartInfo *part;
} ExtPhyClass;

#endif
