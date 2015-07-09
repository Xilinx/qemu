/*
 * xilinx_dp.h
 *
 *  Copyright (C) 2015 : GreenSocs Ltd
 *      http://www.greensocs.com/ , email: info@greensocs.com
 *
 *  Developed by :
 *  Frederic Konrad   <fred.konrad@greensocs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "hw/dma/xilinx_dpdma.h"

#ifndef XILINX_DP_H
#define XILINX_DP_H

typedef struct XilinxDPState XilinxDPState;

#define TYPE_XILINX_DP "xlnx.v-dp"
#define XILINX_DP(obj) OBJECT_CHECK(XilinxDPState, (obj), TYPE_XILINX_DP)

#endif /* !XILINX_DP_H */
