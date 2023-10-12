#ifndef MDIO_SLAVE_H
#define MDIO_SLAVE_H

#include "hw/qdev-core.h"

#define TYPE_MDIO_SLAVE "mdio-slave"
#define MDIO_SLAVE(obj) \
    OBJECT_CHECK(MDIOSlave, (obj), TYPE_MDIO_SLAVE)
#define MDIO_SLAVE_CLASS(klass) \
    OBJECT_CLASS_CHECK(MDIOSlaveClass, (klass), TYPE_MDIO_SLAVE)
#define MDIO_SLAVE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(MDIOSlaveClass, (obj), TYPE_MDIO_SLAVE)

typedef enum MDIOFrameStart {
    MDIO_ST_CLAUSE_22 = 0x1,
    MDIO_ST_CLAUSE_45 = 0x0
} MDIOFrameStart;

typedef enum MDIOFrameOp {
    MDIO_OP_ADDR = 0x0,
    MDIO_OP_WRITE = 0x1,
    MDIO_OP_READ = 0x2,
    MDIO_OP_READ_POST_INCR = 0x3,
} MDIOFrameOp;

typedef struct MDIOFrame {
    MDIOFrameStart st;
    MDIOFrameOp op;
    uint8_t addr0; /* clause 22: PHY address, clause 45: port address */
    uint8_t addr1; /* clause 22: reg address, clause 45: device address */
    uint16_t data;

    /*
     * Returned by the PHY that handled the frame. As of today, the data stream
     * does not go through the PHY. The MAC directly interfaces with the QEMU
     * net API to send/receive Ethernet frames. As a result it needs a way to
     * know the PHY link status to correctly take into account the PHY
     * configuration (link status, loopback, ...). This could go away if the
     * PHY was effectively placed on the data path between the MAC and the rest
     * of the world.
     */
    struct {
        bool present; /* A PHY succesfully handled the frame */
        bool local_loopback;
        bool remote_loopback;
    } phy_status;
} MDIOFrame;

typedef struct MDIOSlave {
    DeviceState qdev;

    uint8_t addr;
} MDIOSlave;

typedef struct MDIOSlaveClass {
    DeviceClass parent_class;

    void (*transfer)(MDIOSlave *s, MDIOFrame *frame);
} MDIOSlaveClass;

#define TYPE_MDIO_BUS "mdio-bus"
#define MDIO_BUS(obj) OBJECT_CHECK(MDIOBus, (obj), TYPE_MDIO_BUS)

typedef struct MDIOBus {
    BusState qbus;

    uint8_t cur_addr;
    MDIOSlave *cur_slave;
} MDIOBus;

struct MDIOBus *mdio_init_bus(DeviceState *parent, const char *name);
void mdio_set_slave_addr(MDIOSlave *s, uint8_t addr);
int mdio_send(struct MDIOBus *s, uint8_t addr, uint8_t reg, uint16_t data);
uint16_t mdio_recv(struct MDIOBus *s, uint8_t addr, uint8_t reg);

void mdio_transfer(MDIOBus *s, MDIOFrame *frame);

static inline bool mdio_frame_is_read(const MDIOFrame *f)
{
    return f->op >= MDIO_OP_READ;
}

static inline void mdio_frame_invalid_dst(MDIOFrame *f)
{
    f->data = 0xffff;
    f->phy_status.present = false;
}

#endif
