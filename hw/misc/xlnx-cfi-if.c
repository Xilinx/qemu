/*
 * Xilinx CFI interface
 *
 * Copyright (c) 2022 Xilinx Inc.
 *
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
#include "qemu/osdep.h"
#include "hw/misc/xlnx-cfi-if.h"

void xlnx_cfi_transfer_packet(XlnxCfiIf *cfi_if, XlnxCfiPacket *pkt)
{
    XlnxCfiIfClass *xcic = XLNX_CFI_IF_GET_CLASS(cfi_if);

    if (xcic->cfi_transfer_packet) {
        xcic->cfi_transfer_packet(cfi_if, pkt);
    }
}

static const TypeInfo xlnx_cfi_if_info = {
    .name          = TYPE_XLNX_CFI_IF,
    .parent        = TYPE_INTERFACE,
    .class_size = sizeof(XlnxCfiIfClass),
};

static void xlnx_cfi_if_register_types(void)
{
    type_register_static(&xlnx_cfi_if_info);
}

type_init(xlnx_cfi_if_register_types)

