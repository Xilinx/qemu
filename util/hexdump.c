<<<<<<< HEAD
/*
 * Helper to hexdump a buffer
 *
 * Copyright (c) 2013 Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 * Copyright (c) 2013 Xilinx, Inc
 *
 * Factored out from iov.c:
 * Copyright IBM, Corp. 2007, 2008
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * Author(s):
 *  Anthony Liguori <aliguori@us.ibm.com>
 *  Amit Shah <amit.shah@redhat.com>
 *  Michael Tokarev <mjt@tls.msk.ru>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

=======
>>>>>>> 5880b0ec1458210f323f3f086cdb7c900521fc21
#include "qemu-common.h"
#include "qemu/hexdump.h"

void hexdump(const char *buf, FILE *fp, const char *prefix, size_t size)
{
    unsigned int b;

    for (b = 0; b < size; b++) {
        if ((b % 16) == 0) {
            fprintf(fp, "%s: %04x:", prefix, b);
        }
        if ((b % 4) == 0) {
            fprintf(fp, " ");
        }
        fprintf(fp, " %02x", (unsigned char)buf[b]);
        if ((b % 16) == 15) {
            fprintf(fp, "\n");
        }
    }
    if ((b % 16) != 0) {
        fprintf(fp, "\n");
    }
}
