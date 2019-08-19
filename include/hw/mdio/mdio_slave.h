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

typedef struct MDIOSlave {
    DeviceState qdev;

    uint8_t addr;
} MDIOSlave;

typedef struct MDIOSlaveClass {
    DeviceClass parent_class;

    /* Master to Slave */
    int (*send)(MDIOSlave *s, uint8_t reg, uint8_t data);
    /*slave to master */
    int (*recv)(MDIOSlave *s, uint8_t reg);
} MDIOSlaveClass;

#define TYPE_MDIO_BUS "mdio-bus"
#define MDIO_BUS(obj) OBJECT_CHECK(struct MDIOBus, (obj), TYPE_MDIO_BUS)

struct MDIOBus {
    BusState qbus;

    uint8_t cur_addr;
    MDIOSlave *cur_slave;
};

struct MDIOBus *mdio_init_bus(DeviceState *parent, const char *name);
void mdio_set_slave_addr(MDIOSlave *s, uint8_t addr);
int mdio_send(struct MDIOBus *s, uint8_t addr, uint8_t reg, uint8_t data);
int mdio_recv(struct MDIOBus *s, uint8_t addr, uint8_t reg);

#endif
