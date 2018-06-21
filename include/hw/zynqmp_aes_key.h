/*
 * Interface to pass AES keys.
 *
 * Copyright (c) 2013 Xilinx Inc.
 *
 * Written by Edgar E. Iglesias <edgari@xilinx.com>
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
#ifndef ZYNQMP_AES_KEY_H
#define ZYNQMP_AES_KEY_H

#include "qemu-common.h"
#include "qom/object.h"

#define TYPE_ZYNQMP_AES_KEY_SINK "zynqmp-aes-key-sink"

#define ZYNQMP_AES_KEY_SINK_CLASS(klass) \
     OBJECT_CLASS_CHECK(ZynqMPAESKeySinkClass, (klass), \
                        TYPE_ZYNQMP_AES_KEY_SINK)

#define ZYNQMP_AES_KEY_SINK_GET_CLASS(obj) \
    OBJECT_GET_CLASS(ZynqMPAESKeySinkClass, (obj), TYPE_ZYNQMP_AES_KEY_SINK)

#define ZYNQMP_AES_KEY_SINK(obj) \
     INTERFACE_CHECK(ZynqMPAESKeySink, (obj), TYPE_ZYNQMP_AES_KEY_SINK)

typedef struct ZynqMPAESKeySink {
    /*< private >*/
    Object Parent;
} ZynqMPAESKeySink;

typedef struct ZynqMPAESKeySinkClass {
    /*< private >*/
    InterfaceClass parent_class;

    /*< public >*/
    /**
     * update - Update the key value whenever the key has changed
     *          at the source.
     *
     * @obj: Sink to push key update to
     * @key: Key material
     * @len: Length in bytes of key
     */
    void (*update)(ZynqMPAESKeySink *obj, uint8_t *key, size_t len);
} ZynqMPAESKeySinkClass;

void zynqmp_aes_key_update(ZynqMPAESKeySink *sink, uint8_t *key, size_t len);
#endif
