/*
 * Copyright (c) 2013 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 * Written by Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 *
 * This code is licensed under the GNU GPL.
 */
#ifndef REMOTE_PORT_GPIO_H
#define REMOTE_PORT_GPIO_H

#define TYPE_REMOTE_PORT_GPIO "remote-port-gpio"
#define REMOTE_PORT_GPIO(obj) \
        OBJECT_CHECK(RemotePortGPIO, (obj), TYPE_REMOTE_PORT_GPIO)

#define MAX_GPIOS 164

typedef struct RemotePortGPIO {
    /* private */
    SysBusDevice parent;
    /* public */

    int8_t cache[MAX_GPIOS];
    uint32_t num_gpios;
    qemu_irq *gpio_out;
    uint16_t cell_offset_irq_num;

    bool posted_updates;
    uint32_t rp_dev;
    struct RemotePort *rp;
    struct rp_peer_state *peer;
} RemotePortGPIO;
#endif
