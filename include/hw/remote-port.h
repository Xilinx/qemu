/*
 * QEMU remote port.
 *
 * Copyright (c) 2013 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 * This code is licensed under the GNU GPL.
 */
#ifndef REMOTE_PORT_H__
#define REMOTE_PORT_H__

#include <stdbool.h>

bool rp_time_warp_enable(bool en);
bool rp_try_lock(uint64_t addr);
void rp_unlock(uint64_t addr);

#endif
