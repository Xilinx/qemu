/*
 * xilinx_dpdma.h
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

#ifndef XILINX_DPDMA_H
#define XILINX_DPDMA_H

#include "hw/sysbus.h"
#include "ui/console.h"
#include "sysemu/dma.h"

typedef struct XilinxDPDMAState XilinxDPDMAState;

#define TYPE_XILINX_DPDMA "xlnx.dpdma"
#define XILINX_DPDMA(obj) OBJECT_CHECK(XilinxDPDMAState, (obj),                \
                                       TYPE_XILINX_DPDMA)

/*
 * \func xilinx_dpdma_start_operation.
 * \brief Start the operation on the specified channel. The DPDMA get the
 *        current descriptor and retrieve data to the buffer specified by
 *        dpdma_set_host_data_location.
 * \arg s The DPDMA instance.
 * \arg channel The channel to start.
 * \return false if the channel is disabled or if an error occured, true
 *         otherwise.
 */
bool xilinx_dpdma_start_operation(XilinxDPDMAState *s, uint8_t channel);

/*
 * \func xilinx_dpdma_set_host_data_location.
 * \brief Set the location in the host memory where to store the data out from
 *        the dma channel.
 * \arg s The DPDMA instance.
 * \arg channel The channel associated to the pointer.
 * \arg p The buffer where to store the data.
 */
/* XXX: add a maximum size arg and send an interrupt in case of overflow. */
void xilinx_dpdma_set_host_data_location(XilinxDPDMAState *s, uint8_t channel,
                                         void *p);

#endif /* !XILINX_DPDMA_H */
