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
#include "hw/misc/xlnx-serbs.h"

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
    xlnx_serbs_if *serbsIf;

    /* public */
    uint32_t map_num;
    uint64_t map_offset;
    uint64_t map_size;
    uint32_t rp_dev;
    bool relative;
    uint32_t max_access_size;
    struct RemotePort *rp;
    struct rp_peer_state *peer;
    int rp_timeout;
    int serbs_id;
    bool rp_timeout_err;
};

MemTxResult rp_mm_access(RemotePort *rp, uint32_t rp_dev,
                         struct rp_peer_state *peer,
                         MemoryTransaction *tr,
                         bool relative, uint64_t offset);

MemTxResult rp_mm_access_with_def_attr(RemotePort *rp, uint32_t rp_dev,
                                       struct rp_peer_state *peer,
                                       MemoryTransaction *tr,
                                       bool relative, uint64_t offset,
                                       uint32_t def_attr);
#endif
