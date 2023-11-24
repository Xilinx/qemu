/*
 * QEMU model of the PMXC Key Transfer.
 *
 * Copyright (c) 2023 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef XLNX_PMXC_KT_H
#define XLNX_PMXC_KT_H

#include "qom/object.h"
#include "qemu/osdep.h"

#define TYPE_PMXC_KEY_TRANSFER "xlnx-pmxc-kt"
typedef struct pmxcKTClass pmxcKTClass;
DECLARE_CLASS_CHECKERS(pmxcKTClass, PMXC_KT, TYPE_PMXC_KEY_TRANSFER)

#define PMXC_KT(obj) \
        INTERFACE_CHECK(pmxcKT, (obj), TYPE_PMXC_KEY_TRANSFER)

typedef struct pmxcKT {
     Object Parent;
} pmxcKT;

typedef struct pmxcKTClass {
    InterfaceClass parent;

    /* asu aes */
    void (*asu_ready)(pmxcKT *kt, bool ready);
    /* pmxc aes */
    void (*done)(pmxcKT *kt, bool done);
    void (*send_key)(pmxcKT *kt, uint8_t n, uint8_t *key, size_t len);
} pmxcKTClass;

void pmxc_kt_asu_ready(pmxcKT *kt, bool ready);
void pmxc_kt_done(pmxcKT *kt, bool done);
void pmxc_kt_send_key(pmxcKT *kt, uint8_t n, uint8_t *key, size_t len);
#endif
