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

#include "qemu/osdep.h"
#include "hw/zynqmp_aes_key.h"

void zynqmp_aes_key_update(ZynqMPAESKeySink *sink,
                            uint8_t *key, size_t len)
{
    ZynqMPAESKeySinkClass *k = ZYNQMP_AES_KEY_SINK_GET_CLASS(sink);

    k->update(sink, key, len);
}

static const TypeInfo zynqmp_aes_key_info = {
    .name          = TYPE_ZYNQMP_AES_KEY_SINK,
    .parent        = TYPE_INTERFACE,
    .class_size    = sizeof(ZynqMPAESKeySinkClass),
};

static void zynqmp_aes_key_register_types(void)
{
    type_register_static(&zynqmp_aes_key_info);
}

type_init(zynqmp_aes_key_register_types)
