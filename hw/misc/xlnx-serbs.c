/*
 * Xilinx Versal SERBS
 *
 * SPDX-FileCopyrightText: 2023 AMD
 * SPDX-FileContributor: Author: Sai Pavan Boddu <sai.pavan.boddu@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/xlnx-serbs.h"

void xlnx_serbs_if_timer_config(xlnx_serbs_if *sif, int id,
                                int timems, bool enable)
{
    xlnx_serbs_if_class *Klass = XLNX_SERBS_IF_GET_CLASS(sif);

    if (Klass->timer_config) {
        Klass->timer_config(sif, id, timems, enable);
    }
}

void xlnx_serbs_if_timeout_set(xlnx_serbs_if *sif, int id, bool level)
{
    xlnx_serbs_if_class *Klass = XLNX_SERBS_IF_GET_CLASS(sif);

    if (Klass->timeout_set) {
        Klass->timeout_set(sif, id, level);
    }
}

static const TypeInfo serbs_if_info = {
    .name   = TYPE_XLNX_SERBS_IF,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(xlnx_serbs_if_class),
};

static void serbs_if_types(void)
{
    type_register_static(&serbs_if_info);
}

type_init(serbs_if_types);
