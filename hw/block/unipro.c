/*
 * UFS controller
 * Based on JESD223
 *
 * SPDX-FileCopyrightText: 2023, Advanced Micro Devices, Inc
 * SPDX-FileContributor: Author: Sai Pavan Boddu <sai.pavan.boddu@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/block/ufshc-if.h"
#include "unipro.h"
#include "trace.h"
#include "hw/irq.h"

#define TYPE_UNIPRO_MPHY "unipro-mphy"
#define UNIPRO_MPHY(obj) \
        OBJECT_CHECK(UniproMphy, (obj), TYPE_UNIPRO_MPHY)

#define ATTR_WRITE(regs, attr, val) \
    regs[(attr & 0xFFF)] = val

typedef struct UniproMphy {
    DeviceState parent;

    MemoryRegion iomem;
    ufshcIF *ufshc;
    qemu_irq dev_rst;
    /*
     * Attributes
     * L1   - M-Tx & M-Rx
     * L1.5 - Phy Adaptor Common &
     *        M-Phy Specific
     * L2   - Data Link layer
     * L3   - Network Layer
     * L4   - Transport Layer
     */
    uint8_t L1[0x100];
    uint8_t L1_5[0x5E0];
    uint8_t L2[0x70];
    uint8_t L3[0x30];
    uint8_t L4[0x30];
    uint8_t dme[0x100];
} UniproMphy;


static CfgResultCode pa_reg_access(UniproMphy *s,  dmeCmd cmd, uint16_t MIBattr,
                                    uint16_t GenSel, uint32_t *data)
{
    uint16_t offset = 0xFFF & MIBattr;
    CfgResultCode ret = DME_FAILURE;
    bool get = cmd == DME_GET || cmd == DME_PEER_GET;

    if (offset > 0x5E0) {
        return DME_INVALID_MIB_ATTRIBUTE;
    }

    switch (MIBattr) {
    case PA_REMOTEVERINFO:
    case PA_LOCALVERINFO:
        if (get) {
            *data = cpu_to_le32(*(uint16_t *) &s->L1_5[offset]);
            ret= DME_SUCCESS;
        } else {
            ret = DME_READ_ONLY_MIB_ATTRIBUTE;
        }
        break;
    case PA_PWRMODE:
        if (!get) {
            switch ((uint8_t) *data & 0xF) {
            case FAST_MODE:
            case SLOW_MODE:
            case FASTAUTO_MODE:
            case SLOWAUTO_MODE:
                ufshci_pwr_mode_status(s->ufshc, PWR_LOCAL);
                s->L1_5[offset] = (uint8_t) *data;
                ret = DME_SUCCESS;
                break;
           default:
                ufshci_pwr_mode_status(s->ufshc, PWR_ERROR_CAP);
                ret = DME_INVALID_MIB_ATTRIBUTE_VALUE;
                break;
           };
           break;
        }
        /*
         * fall through for reads
         */
    default:
        if (get) {
            *data = cpu_to_le32(s->L1_5[offset]);
        } else {
            s->L1_5[offset] = (uint8_t) *data;
        }
        ret = DME_SUCCESS;
        break;
    };
    return ret;
}

static CfgResultCode unipro_dme_cmd(ufshcIF *ifs, dmeCmd cmd, uint16_t MIBattr,
                                    uint16_t GenSel, uint32_t *data)
{
    UniproMphy *s = UNIPRO_MPHY(ifs);
    uint8_t LayerID = (0x7000 & MIBattr) >> 12;
    uint16_t offset = 0xFFF & MIBattr;
    uint8_t *reg = LayerID == 0 ? s->L1:
                   LayerID == 1 ? s->L1_5:
                   LayerID == 2 ? s->L2:
                   LayerID == 3 ? s->L3:
                   LayerID == 4 ? s->L4 :
                   LayerID == 5 ? s->dme : NULL;
    CfgResultCode ret = DME_FAILURE;

    if (reg == NULL) {
        return DME_INVALID_MIB_ATTRIBUTE;
    }

    trace_unipro_dme_cmd((uint8_t)cmd, MIBattr, GenSel);

    switch (cmd) {
    case DME_GET:
    case DME_SET:
        switch (LayerID) {
        case 1:
            ret = pa_reg_access(s, cmd, MIBattr, GenSel, data);
            break;
        case 0:
        case 2 ... 5:
            if (offset > (LayerID == 0 ? sizeof(s->L1) :
                          LayerID == 1 ? sizeof(s->L1_5) :
                          LayerID == 2 ? sizeof(s->L2) :
                          LayerID == 3 ? sizeof(s->L3) :
                          LayerID == 4 ? sizeof(s->L4) :
                          LayerID == 5 ? sizeof(s->dme) : 0)) {
                /*
                 * Layer offset might be invalid.
                 * TODO: Need to report failure.
                 */
                trace_unipro_offset_invalid(offset, LayerID);
                return DME_SUCCESS;
            }
            if (cmd == DME_GET) {
                *data = cpu_to_le32(reg[offset]);
            } else {
                reg[offset] = (uint8_t) *data;
            }
            ret = DME_SUCCESS;
            break;
        };
        break;
    case DME_PEER_GET:
    case DME_PEER_SET:
        if (LayerID == 1) {
            ret = pa_reg_access(s, cmd, MIBattr, GenSel, data);
        }
        break;
    case DME_RESET:
        qemu_set_irq(s->dev_rst, 0);
        /*
         * Fall through
         */
    case DME_POWERON:
    case DME_POWEROFF:
    case DME_ENABLE:
    case DME_ENDPOINTRESET:
    case DME_LINKSTARTUP:
    case DME_HIBERNATE_ENTER:
    case DME_HIBERNATE_EXIT:
        ret = DME_SUCCESS;
        break;
    case DME_TEST_MODE:
        break;
    };

    return ret;
}

static void unipro_reset_enter(Object *obj, ResetType type)
{
    UniproMphy *s = UNIPRO_MPHY(obj);

    qemu_set_irq(s->dev_rst, 1);
}

static void uniproMphy_realize(DeviceState *dev, Error **errp)
{
    UniproMphy *s = UNIPRO_MPHY(dev);

    ATTR_WRITE(s->L1_5, PA_ACTIVETXDATALANES, 1);
    ATTR_WRITE(s->L1_5, PA_ACTIVERXDATALANES, 1);
    ATTR_WRITE(s->L1_5, PA_PHY_TYPE, 1);
    ATTR_WRITE(s->L1_5, PA_AVAILTXDATALANES, 1);
    ATTR_WRITE(s->L1_5, PA_AVAILRXDATALANES, 1);
    ATTR_WRITE(s->L1_5, PA_CONNECTEDRXDATALANES, 1);
    ATTR_WRITE(s->L1_5, PA_CONNECTEDTXDATALANES, 1);
    ATTR_WRITE(s->L1_5, PA_TXPWRSTATUS, 1);
    ATTR_WRITE(s->L1_5, PA_RXPWRSTATUS, 1);
    ATTR_WRITE(s->L1_5, PA_TXGEAR, 1);
    ATTR_WRITE(s->L1_5, PA_RXGEAR, 1);
    ATTR_WRITE(s->L1_5, PA_PWRMODE, 5);
    ATTR_WRITE(s->L1_5, PA_LOCALVERINFO, 0x5);
    ATTR_WRITE(s->L1_5, PA_REMOTEVERINFO, 0x5);
    ATTR_WRITE(s->L1_5, PA_MAXRXHSGEAR, 1);
    ATTR_WRITE(s->L1_5, PA_MAXRXPWMGEAR, 1);

    qdev_init_gpio_out(dev, &s->dev_rst, 1);
}

static void uniproMphy_init(Object *obj)
{
    UniproMphy *s = UNIPRO_MPHY(obj);

    object_property_add_link(obj, "ufshc", TYPE_UFSHC_IF,
                             (Object **)&s->ufshc,
                             qdev_prop_allow_set_link,
                             OBJ_PROP_LINK_STRONG);
}

static void uniproMphy_class_init(ObjectClass *klass, void *data)
{
    ufshcIFClass *uc = UFSHC_IF_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    uc->dme_cmd = unipro_dme_cmd;
    dc->realize = uniproMphy_realize;
    rc->phases.enter = unipro_reset_enter;
}

static const TypeInfo uniproMphy_info = {
    .name = TYPE_UNIPRO_MPHY,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(UniproMphy),
    .class_init = uniproMphy_class_init,
    .instance_init = uniproMphy_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_UFSHC_IF },
    },
};

static void uniproMphy_types(void)
{
    type_register_static(&uniproMphy_info);
}

type_init(uniproMphy_types)
