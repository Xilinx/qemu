/*
 * Xilinx Versal SERBS
 *
 * SPDX-FileCopyrightText: 2023 AMD
 * SPDX-FileContributor: Author: Sai Pavan Boddu <sai.pavan.boddu@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef XLNX_VERSAL_SERBS_H
#define XLNX_VERSAL_SERBS_H

#include "qom/object.h"
#define TYPE_XLNX_SERBS_IF "xlnx-serbs-if"

typedef struct xlnx_serbs_if_class xlnx_serbs_if_class;
DECLARE_CLASS_CHECKERS(xlnx_serbs_if_class, XLNX_SERBS_IF, TYPE_XLNX_SERBS_IF)

#define XLNX_SERBS_IF(obj) \
    INTERFACE_CHECK(xlnx_serbs_if, (obj), TYPE_XLNX_SERBS_IF)

typedef struct xlnx_serbs_if {
    Object Parent;
} xlnx_serbs_if;

typedef struct xlnx_serbs_if_class {
    InterfaceClass parent;

    void (*timer_config)(xlnx_serbs_if *sif, int id, int timems, bool enable);
    void (*timeout_set)(xlnx_serbs_if *sif, int id, bool level);
} xlnx_serbs_if_class;

void xlnx_serbs_if_timer_config(xlnx_serbs_if *sif, int id, int timems,
                                bool enable);
void xlnx_serbs_if_timeout_set(xlnx_serbs_if *sif, int id, bool level);
#endif
