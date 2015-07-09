/*
 * dpcd.h
 *
 *  Copyright (C)2015 : GreenSocs Ltd
 *      http://www.greensocs.com/ , email: info@greensocs.com
 *
 *  Developed by :
 *  Frederic Konrad   <fred.konrad@greensocs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option)any later version.
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

#ifndef DPCD_H
#define DPCD_H

typedef struct DPCDState DPCDState;

#define TYPE_DPCD "dpcd"
#define DPCD(obj) OBJECT_CHECK(DPCDState, (obj), TYPE_DPCD)

/* DCPD Revision. */
#define DPCD_REV_1_0 0x10
#define DPCD_REV_1_1 0x11

/* DCPD Max Link Rate. */
#define DPCD_1_62GBPS 0x06
#define DPCD_2_7GBPS 0x0A

/* DCPD Max down spread. */
#define DPCD_UP_TO_0_5 0x01
#define DPCD_NO_AUX_HANDSHAKE_LINK_TRAINING 0x40

/* DCPD Downstream port type. */
#define DPCD_DISPLAY_PORT 0x00
#define DPCD_ANALOG 0x02
#define DPCD_DVI_HDMI 0x04
#define DPCD_OTHER 0x06

/* DPCD Format conversion. */
#define DPCD_FORMAT_CONVERSION 0x08

/* Main link channel coding. */
#define DPCD_ANSI_8B_10B 0x01

/* Down stream port count. */
#define DPCD_OUI_SUPPORTED 0x80

/* Receiver port capability. */
#define DPCD_EDID_PRESENT 0x02
#define DPCD_ASSOCIATED_TO_PRECEDING_PORT 0x04

/* Down stream port capability. */
#define DPCD_CAP_DISPLAY_PORT 0x000
#define DPCD_CAP_ANALOG_VGA 0x001
#define DPCD_CAP_DVI 0x002
#define DPCD_CAP_HDMI 0x003
#define DPCD_CAP_OTHER 0x100

#endif /* !DPCD_H */
