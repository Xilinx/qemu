/*
 * QEMU model of the PMXC Key Transfer.
 *
 * Copyright (c) 2023 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "hw/crypto/xlnx-pmxc-key-transfer.h"

void pmxc_kt_asu_ready(pmxcKT *kt, bool rdy)
{
    pmxcKTClass *k = PMXC_KT_GET_CLASS(kt);

    if (k->asu_ready) {
        k->asu_ready(kt, rdy);
    }
}

void pmxc_kt_done(pmxcKT *kt, bool done)
{
    pmxcKTClass *k = PMXC_KT_GET_CLASS(kt);

    if (k->done) {
        k->done(kt, done);
    }
}

void pmxc_kt_send_key(pmxcKT *kt, uint8_t n, uint8_t *key, size_t len)
{
    pmxcKTClass *k = PMXC_KT_GET_CLASS(kt);

    if (k->send_key) {
        k->send_key(kt, n, key, len);
    }
}

static const TypeInfo pmxc_kt_info = {
    .name          = TYPE_PMXC_KEY_TRANSFER,
    .parent        = TYPE_INTERFACE,
    .class_size    = sizeof(pmxcKTClass),
};

static void pmxc_kt_types(void)
{
    type_register_static(&pmxc_kt_info);
}

type_init(pmxc_kt_types)
