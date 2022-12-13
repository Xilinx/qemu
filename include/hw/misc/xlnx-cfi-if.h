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
#ifndef XLNX_CFI_IF_H
#define XLNX_CFI_IF_H 1

#include "qemu/help-texts.h"
#include "hw/hw.h"
#include "qom/object.h"

#define TYPE_XLNX_CFI_IF "xlnx-cfi-if"
typedef struct XlnxCfiIfClass XlnxCfiIfClass;
DECLARE_CLASS_CHECKERS(XlnxCfiIfClass, XLNX_CFI_IF, TYPE_XLNX_CFI_IF)

#define XLNX_CFI_IF(obj) \
     INTERFACE_CHECK(XlnxCfiIf, (obj), TYPE_XLNX_CFI_IF)

typedef enum {
    PACKET_TYPE_CFU = 0x52,
    PACKET_TYPE_CFRAME = 0xA1,
} xlnx_cfi_packet_type;

typedef enum {
    CFRAME_FAR = 1,
    CFRAME_SFR = 2,
    CFRAME_FDRI = 4,
    CFRAME_CMD = 6,
} xlnx_cfi_reg_addr;

typedef struct XlnxCfiPacket {
    uint8_t reg_addr;
    uint32_t data[4];
} XlnxCfiPacket;

typedef struct XlnxCfiIf {
    Object Parent;
} XlnxCfiIf;

typedef struct XlnxCfiIfClass {
    InterfaceClass parent;

    void (*cfi_transfer_packet)(XlnxCfiIf *cfi_if, XlnxCfiPacket *pkt);
} XlnxCfiIfClass;

void xlnx_cfi_transfer_packet(XlnxCfiIf *cfi_if, XlnxCfiPacket *pkt);

#endif /* XLNX_CFI_IF_H */
