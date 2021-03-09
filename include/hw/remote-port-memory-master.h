/*
 * QEMU remote port memory master. Read and write transactions
 * recieved from QEMU are transmitted over remote-port.
 *
 * Copyright (c) 2020 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 * This code is licensed under the GNU GPL.
 */
#ifndef REMOTE_PORT_MEMORY_MASTER_H
#define REMOTE_PORT_MEMORY_MASTER_H

#include "hw/remote-port.h"

#define TYPE_REMOTE_PORT_MEMORY_MASTER "remote-port-memory-master"
#define REMOTE_PORT_MEMORY_MASTER(obj) \
        OBJECT_CHECK(RemotePortMemoryMaster, (obj), \
                     TYPE_REMOTE_PORT_MEMORY_MASTER)

typedef struct RemotePortMemoryMaster RemotePortMemoryMaster;

typedef struct RemotePortMap {
    void *parent;
    MemoryRegion iomem;
    uint32_t rp_dev;
    uint64_t offset;
} RemotePortMap;

struct RemotePortMemoryMaster {
    /* private */
    SysBusDevice parent;

    MemoryRegionOps *rp_ops;
    RemotePortMap *mmaps;

    /* public */
    uint32_t map_num;
    uint64_t map_offset;
    uint64_t map_size;
    uint32_t rp_dev;
    bool relative;
    uint32_t max_access_size;
    struct RemotePort *rp;
    struct rp_peer_state *peer;
};

MemTxResult rp_mm_access(RemotePort *rp, uint32_t rp_dev,
                         struct rp_peer_state *peer,
                         MemoryTransaction *tr,
                         bool relative, uint64_t offset);
#endif
