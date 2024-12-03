/*
 * PMXC Key Transfer interface.
 *
 * Copyright (c) 2023 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef XLNX_PMXC_KEY_XFER_IF_H
#define XLNX_PMXC_KEY_XFER_IF_H

#include "qom/object.h"

#define TYPE_PMXC_KEY_XFER_IF "xlnx-pmxc-key-xfer-if"
typedef struct PmxcKeyXferIfClass PmxcKeyXferIfClass;
DECLARE_CLASS_CHECKERS(PmxcKeyXferIfClass, PMXC_KEY_XFER_IF,
                       TYPE_PMXC_KEY_XFER_IF)

#define PMXC_KEY_XFER_IF(obj) \
        INTERFACE_CHECK(PmxcKeyXferIf, (obj), TYPE_PMXC_KEY_XFER_IF)

typedef struct PmxcKeyXferIf PmxcKeyXferIf;

typedef struct PmxcKeyXferIfClass {
    InterfaceClass parent;

    /* asu aes */
    void (*asu_ready)(PmxcKeyXferIf *kt, bool ready);
    /* pmxc aes */
    void (*done)(PmxcKeyXferIf *kt, bool done);
    void (*send_key)(PmxcKeyXferIf *kt, uint8_t n, uint8_t *key, size_t len);
} PmxcKeyXferIfClass;

void pmxc_kt_asu_ready(PmxcKeyXferIf *kt, bool ready);
void pmxc_kt_done(PmxcKeyXferIf *kt, bool done);
void pmxc_kt_send_key(PmxcKeyXferIf *kt, uint8_t n, uint8_t *key, size_t len);
#endif
