/*
 * Copyright (c) 2017 Xilinx Inc.
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

#ifndef SSS__H
#define SSS__H

#include "hw/sysbus.h"
#include "hw/stream.h"

#define TYPE_SSS_BASE "sss-base"
#define TYPE_SSS_STREAM "sss-stream"

#define SSS_BASE(obj) \
     OBJECT_CHECK(SSSBase, (obj), TYPE_SSS_BASE)

#define SSS_STREAM(obj) \
     OBJECT_CHECK(SSSStream, (obj), TYPE_SSS_STREAM)

#define NOT_REMOTE(s) \
    (s->num_remotes)

typedef struct SSSBase SSSBase;
typedef struct SSSStream SSSStream;

struct SSSStream {
    DeviceState parent_obj;

    SSSBase *sss;
};

struct SSSBase {
    SysBusDevice busdev;

    StreamSlave **tx_devs;
    SSSStream *rx_devs;

    uint32_t (*get_sss_regfield)(SSSBase *, int);
    StreamCanPushNotifyFn *notifys;
    void **notify_opaques;

    const uint32_t *sss_population;
    const int *r_sss_shifts;
    const uint8_t *r_sss_encodings;
    uint8_t num_remotes;
};

void sss_notify_all(SSSBase *s);

#endif
