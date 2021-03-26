/*
 * QEMU remote port ATS
 *
 * Copyright (c) 2021 Xilinx Inc
 * Written by Francisco Iglesias <francisco.iglesias@xilinx.com>
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
#ifndef REMOTE_PORT_ATS_H
#define REMOTE_PORT_ATS_H

#include "hw/remote-port.h"

#define TYPE_REMOTE_PORT_ATS "remote-port-ats"
#define REMOTE_PORT_ATS(obj) \
        OBJECT_CHECK(RemotePortATS, (obj), TYPE_REMOTE_PORT_ATS)

typedef struct {
    /* private */
    SysBusDevice parent;

    /* public */
    struct RemotePort *rp;
    struct rp_peer_state *peer;
    MemoryRegion *mr;
    AddressSpace as;
    RemotePortDynPkt rsp;
    GArray *iommu_notifiers;
    uint32_t rp_dev;
    GArray *cache; /* Translation cache */
} RemotePortATS;

#define TYPE_REMOTE_PORT_ATS_CACHE "remote-port-ats-cache"

#define REMOTE_PORT_ATS_CACHE_CLASS(klass) \
     OBJECT_CLASS_CHECK(RemotePortATSCacheClass, (klass), \
                        TYPE_REMOTE_PORT_ATS_CACHE)
#define REMOTE_PORT_ATS_CACHE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(RemotePortATSCacheClass, (obj), TYPE_REMOTE_PORT_ATS_CACHE)
#define REMOTE_PORT_ATS_CACHE(obj) \
     INTERFACE_CHECK(RemotePortATSCache, (obj), TYPE_REMOTE_PORT_ATS_CACHE)

typedef struct RemotePortATSCache {
    Object Parent;
} RemotePortATSCache;

typedef struct RemotePortATSCacheClass {
    InterfaceClass parent;

    IOMMUTLBEntry *(*lookup_translation)(RemotePortATSCache *cache,
                                         hwaddr translated_addr,
                                         hwaddr len);
} RemotePortATSCacheClass;

IOMMUTLBEntry *rp_ats_cache_lookup_translation(RemotePortATSCache *cache,
                                               hwaddr translated_addr,
                                               hwaddr len);
#endif
