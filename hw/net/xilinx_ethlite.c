/*
 * QEMU model of the Xilinx Ethernet Lite MAC.
 *
 * Copyright (c) 2009 Edgar E. Iglesias.
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
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "cpu.h" /* FIXME should not use tswap* */
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "net/net.h"

#define D(x)
#define R_TX_BUF0     0
#define R_TX_LEN0     (0x07f4 / 4)
#define R_TX_GIE0     (0x07f8 / 4)
#define R_TX_CTRL0    (0x07fc / 4)
#define R_TX_BUF1     (0x0800 / 4)
#define R_TX_LEN1     (0x0ff4 / 4)
#define R_TX_CTRL1    (0x0ffc / 4)

#define R_RX_BUF0     (0x1000 / 4)
#define R_RX_CTRL0    (0x17fc / 4)
#define R_RX_BUF1     (0x1800 / 4)
#define R_RX_CTRL1    (0x1ffc / 4)
#define R_MAX         (0x2000 / 4)

#define GIE_GIE    0x80000000

#define CTRL_I     0x8
#define CTRL_P     0x2
#define CTRL_S     0x1

#define TYPE_XILINX_ETHLITE "xlnx.xps-ethernetlite"
#define XILINX_ETHLITE(obj) \
    OBJECT_CHECK(struct xlx_ethlite, (obj), TYPE_XILINX_ETHLITE)

#define R_MDIOADDR (0x07E4 / 4)  /* MDIO Address Register */
#define R_MDIOWR (0x07E8 / 4)    /* MDIO Write Data Register */
#define R_MDIORD (0x07EC / 4)    /* MDIO Read Data Register */
#define R_MDIOCTRL (0x07F0 / 4)  /* MDIO Control Register */

/* MDIO Address Register Bit Masks */
#define R_MDIOADDR_REGADR_MASK  0x0000001F  /* Register Address */
#define R_MDIOADDR_PHYADR_MASK  0x000003E0  /* PHY Address */
#define R_MDIOADDR_PHYADR_SHIFT 5
#define R_MDIOADDR_OP_MASK      0x00000400    /* RD/WR Operation */

/* MDIO Write Data Register Bit Masks */
#define R_MDIOWR_WRDATA_MASK    0x0000FFFF /* Data to be Written */

/* MDIO Read Data Register Bit Masks */
#define R_MDIORD_RDDATA_MASK    0x0000FFFF /* Data to be Read */

/* MDIO Control Register Bit Masks */
#define R_MDIOCTRL_MDIOSTS_MASK 0x00000001   /* MDIO Status Mask */
#define R_MDIOCTRL_MDIOEN_MASK  0x00000008   /* MDIO Enable */

/* Advertisement control register. */
#define ADVERTISE_10HALF        0x0020  /* Try for 10mbps half-duplex  */
#define ADVERTISE_10FULL        0x0040  /* Try for 10mbps full-duplex  */
#define ADVERTISE_100HALF       0x0080  /* Try for 100mbps half-duplex */
#define ADVERTISE_100FULL       0x0100  /* Try for 100mbps full-duplex */

#define DPHY(x)

struct PHY {
    uint32_t regs[32];

    int link;

    unsigned int (*read)(struct PHY *phy, unsigned int req);
    void (*write)(struct PHY *phy, unsigned int req,
                  unsigned int data);
};

static unsigned int tdk_read(struct PHY *phy, unsigned int req)
{
    int regnum;
    unsigned r = 0;

    regnum = req & 0x1f;

    switch (regnum) {
    case 1:
        if (!phy->link) {
            break;
        }
        /* MR1.  */
        /* Speeds and modes.  */
        r |= (1 << 13) | (1 << 14);
        r |= (1 << 11) | (1 << 12);
        r |= (1 << 5); /* Autoneg complete.  */
        r |= (1 << 3); /* Autoneg able.  */
        r |= (1 << 2); /* link.  */
        r |= (1 << 1); /* link.  */
        break;
    case 5:
        /* Link partner ability.
           We are kind; always agree with whatever best mode
           the guest advertises.  */
        r = 1 << 14; /* Success.  */
        /* Copy advertised modes.  */
        r |= phy->regs[4] & (15 << 5);
        /* Autoneg support.  */
        r |= 1;
        break;
    case 17:
        /* Marvel PHY on many xilinx boards.  */
        r = 0x4c00; /* 100Mb  */
        break;
    case 18:
        {
            /* Diagnostics reg.  */
            int duplex = 0;
            int speed_100 = 0;
            if (!phy->link) {
                break;
            }
            /* Are we advertising 100 half or 100 duplex ? */
            speed_100 = !!(phy->regs[4] & ADVERTISE_100HALF);
            speed_100 |= !!(phy->regs[4] & ADVERTISE_100FULL);
            /* Are we advertising 10 duplex or 100 duplex ? */
            duplex = !!(phy->regs[4] & ADVERTISE_100FULL);
            duplex |= !!(phy->regs[4] & ADVERTISE_10FULL);
            r = (speed_100 << 10) | (duplex << 11);
        }
        break;

    default:
        r = phy->regs[regnum];
        break;
    }
    DPHY(qemu_log("\n%s %x = reg[%d]\n", __func__, r, regnum));
    return r;
}

static void
tdk_write(struct PHY *phy, unsigned int req, unsigned int data)
{
    int regnum;

    regnum = req & 0x1f;
    DPHY(qemu_log("%s reg[%d] = %x\n", __func__, regnum, data));
    switch (regnum) {
    default:
        phy->regs[regnum] = data;
        break;
    }

    /* Unconditionally clear regs[BMCR][BMCR_RESET] */
    phy->regs[0] &= ~0x8000;
}

static void
tdk_init(struct PHY *phy)
{
    phy->regs[0] = 0x3100;
    /* PHY Id.  */
    phy->regs[2] = 0x0141;
    phy->regs[3] = 0x0cc2;
    /* Autonegotiation advertisement reg.  */
    phy->regs[4] = 0x01E1;
    phy->link = 1;

    phy->read = tdk_read;
    phy->write = tdk_write;
}

struct MDIOBus {
    /* bus.  */
    int mdc;
    int mdio;

    /* decoder.  */
    enum {
        PREAMBLE,
        SOF,
        OPC,
        ADDR,
        REQ,
        TURNAROUND,
        DATA
    } state;
    unsigned int drive;

    unsigned int cnt;
    unsigned int addr;
    unsigned int opc;
    unsigned int req;
    unsigned int data;

    struct PHY *devs[32];
};

static void
mdio_attach(struct MDIOBus *bus, struct PHY *phy, unsigned int addr)
{
    bus->devs[addr & 0x1f] = phy;
}

#ifdef USE_THIS_DEAD_CODE
static void
mdio_detach(struct MDIOBus *bus, struct PHY *phy, unsigned int addr)
{
    bus->devs[addr & 0x1f] = NULL;
}
#endif

static uint16_t mdio_read_req(struct MDIOBus *bus, unsigned int addr,
                  unsigned int reg)
{
    struct PHY *phy;
    uint16_t data;

    phy = bus->devs[addr];
    if (phy && phy->read) {
        data = phy->read(phy, reg);
    } else {
        data = 0xffff;
    }
    DPHY(qemu_log("%s addr=%d reg=%d data=%x\n", __func__, addr, reg, data));
    return data;
}

static void mdio_write_req(struct MDIOBus *bus, unsigned int addr,
               unsigned int reg, uint16_t data)
{
    struct PHY *phy;

    DPHY(qemu_log("%s addr=%d reg=%d data=%x\n", __func__, addr, reg, data));
    phy = bus->devs[addr];
    if (phy && phy->write) {
        phy->write(phy, reg, data);
    }
}

struct TEMAC  {
    struct MDIOBus mdio_bus;
    struct PHY phy;

    void *parent;
};

struct xlx_ethlite
{
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    NICState *nic;
    NICConf conf;

    uint32_t c_tx_pingpong;
    uint32_t c_rx_pingpong;
    unsigned int txbuf;
    unsigned int rxbuf;

uint32_t c_phyaddr;
    struct TEMAC TEMAC;

    uint32_t regs[R_MAX];
};

static inline void eth_pulse_irq(struct xlx_ethlite *s)
{
    /* Only the first gie reg is active. */
    if (s->regs[R_TX_GIE0] & GIE_GIE) {
        qemu_irq_pulse(s->irq);
    }
}

static uint64_t
eth_read(void *opaque, hwaddr addr, unsigned int size)
{
    struct xlx_ethlite *s = opaque;
    uint32_t r = 0;

    addr >>= 2;

    switch (addr)
    {
        case R_TX_GIE0:
        case R_TX_LEN0:
        case R_TX_LEN1:
        case R_TX_CTRL1:
        case R_TX_CTRL0:
        case R_RX_CTRL1:
        case R_RX_CTRL0:
            r = s->regs[addr];
            break;
        case R_MDIOCTRL:
            r = s->regs[addr] & (~R_MDIOCTRL_MDIOSTS_MASK); /* Always ready.  */
            break;

        default:
            r = tswap32(s->regs[addr]);
            break;
    }
    D(qemu_log("%s " TARGET_FMT_plx "=%x\n", __func__, addr * 4, r));
    return r;
}

static void
eth_write(void *opaque, hwaddr addr,
          uint64_t val64, unsigned int size)
{
    struct xlx_ethlite *s = opaque;
    unsigned int base = 0;
    uint32_t value = val64;

    addr >>= 2;
    switch (addr) 
    {
        case R_TX_CTRL0:
        case R_TX_CTRL1:
            if (addr == R_TX_CTRL1)
                base = 0x800 / 4;

            D(qemu_log("%s addr=" TARGET_FMT_plx " val=%x\n",
                       __func__, addr * 4, value));
            if ((value & (CTRL_P | CTRL_S)) == CTRL_S) {
                qemu_send_packet(qemu_get_queue(s->nic),
                                 (void *) &s->regs[base],
                                 s->regs[base + R_TX_LEN0]);
                D(qemu_log("eth_tx %d\n", s->regs[base + R_TX_LEN0]));
                if (s->regs[base + R_TX_CTRL0] & CTRL_I)
                    eth_pulse_irq(s);
            } else if ((value & (CTRL_P | CTRL_S)) == (CTRL_P | CTRL_S)) {
                memcpy(&s->conf.macaddr.a[0], &s->regs[base], 6);
                if (s->regs[base + R_TX_CTRL0] & CTRL_I)
                    eth_pulse_irq(s);
            }

            /* We are fast and get ready pretty much immediately so
               we actually never flip the S nor P bits to one.  */
            s->regs[addr] = value & ~(CTRL_P | CTRL_S);
            break;

        /* Keep these native.  */
        case R_RX_CTRL0:
        case R_RX_CTRL1:
            if (!(value & CTRL_S)) {
                qemu_flush_queued_packets(qemu_get_queue(s->nic));
            }
            /* fall through */
        case R_TX_LEN0:
        case R_TX_LEN1:
        case R_TX_GIE0:
            D(qemu_log("%s addr=" TARGET_FMT_plx " val=%x\n",
                       __func__, addr * 4, value));
            s->regs[addr] = value;
            break;
        case R_MDIOCTRL:
            if (((unsigned int)value & R_MDIOCTRL_MDIOSTS_MASK) != 0) {
                struct TEMAC *t = &s->TEMAC;
                unsigned int op = s->regs[R_MDIOADDR] & R_MDIOADDR_OP_MASK;
                unsigned int phyaddr = (s->regs[R_MDIOADDR] &
                    R_MDIOADDR_PHYADR_MASK) >> R_MDIOADDR_PHYADR_SHIFT;
                unsigned int regaddr = s->regs[R_MDIOADDR] &
                    R_MDIOADDR_REGADR_MASK;
                if (op) {
                    /* read PHY registers */
                    s->regs[R_MDIORD] = mdio_read_req(
                        &t->mdio_bus, phyaddr, regaddr);
                } else {
                    /* write PHY registers */
                    mdio_write_req(&t->mdio_bus, phyaddr, regaddr,
                        s->regs[R_MDIOWR]);
                }
            }
            s->regs[addr] = value;

        default:
            s->regs[addr] = tswap32(value);
            break;
    }
}

static const MemoryRegionOps eth_ops = {
    .read = eth_read,
    .write = eth_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static bool eth_can_rx(NetClientState *nc)
{
    struct xlx_ethlite *s = qemu_get_nic_opaque(nc);
    unsigned int rxbase = s->rxbuf * (0x800 / 4);

    return !(s->regs[rxbase + R_RX_CTRL0] & CTRL_S);
}

static ssize_t eth_rx(NetClientState *nc, const uint8_t *buf, size_t size)
{
    struct xlx_ethlite *s = qemu_get_nic_opaque(nc);
    unsigned int rxbase = s->rxbuf * (0x800 / 4);

    /* DA filter.  */
    if (!(buf[0] & 0x80) && memcmp(&s->conf.macaddr.a[0], buf, 6))
        return size;

    if (s->regs[rxbase + R_RX_CTRL0] & CTRL_S) {
        D(qemu_log("ethlite lost packet %x\n", s->regs[R_RX_CTRL0]));
        return -1;
    }

    D(qemu_log("%s %zd rxbase=%x\n", __func__, size, rxbase));
    if (size > (R_MAX - R_RX_BUF0 - rxbase) * 4) {
        D(qemu_log("ethlite packet is too big, size=%x\n", size));
        return -1;
    }
    memcpy(&s->regs[rxbase + R_RX_BUF0], buf, size);

    s->regs[rxbase + R_RX_CTRL0] |= CTRL_S;
    if (s->regs[R_RX_CTRL0] & CTRL_I) {
        eth_pulse_irq(s);
    }

    /* If c_rx_pingpong was set flip buffers.  */
    s->rxbuf ^= s->c_rx_pingpong;
    return size;
}

static void xilinx_ethlite_reset(DeviceState *dev)
{
    struct xlx_ethlite *s = XILINX_ETHLITE(dev);

    s->rxbuf = 0;
}

static NetClientInfo net_xilinx_ethlite_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = eth_can_rx,
    .receive = eth_rx,
};

static void xilinx_ethlite_realize(DeviceState *dev, Error **errp)
{
    struct xlx_ethlite *s = XILINX_ETHLITE(dev);

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_xilinx_ethlite_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);

    tdk_init(&s->TEMAC.phy);
    mdio_attach(&s->TEMAC.mdio_bus, &s->TEMAC.phy, s->c_phyaddr);
}

static void xilinx_ethlite_init(Object *obj)
{
    struct xlx_ethlite *s = XILINX_ETHLITE(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &eth_ops, s,
                          "xlnx.xps-ethernetlite", R_MAX * 4);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static Property xilinx_ethlite_properties[] = {
    DEFINE_PROP_UINT32("phyaddr", struct xlx_ethlite, c_phyaddr, 7),
    DEFINE_PROP_UINT32("tx-ping-pong", struct xlx_ethlite, c_tx_pingpong, 1),
    DEFINE_PROP_UINT32("rx-ping-pong", struct xlx_ethlite, c_rx_pingpong, 1),
    DEFINE_NIC_PROPERTIES(struct xlx_ethlite, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void xilinx_ethlite_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xilinx_ethlite_realize;
    dc->reset = xilinx_ethlite_reset;
    device_class_set_props(dc, xilinx_ethlite_properties);
}

static const TypeInfo xilinx_ethlite_info = {
    .name          = TYPE_XILINX_ETHLITE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct xlx_ethlite),
    .instance_init = xilinx_ethlite_init,
    .class_init    = xilinx_ethlite_class_init,
};

static void xilinx_ethlite_register_types(void)
{
    type_register_static(&xilinx_ethlite_info);
}

type_init(xilinx_ethlite_register_types)
