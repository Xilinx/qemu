/*
 * Workaround for MAP_FIXED_NOREPLACE
 *
 * Copyright (c) 2024, Advanced Micro Devices, Inc.
 * Developed by Fred Konrad <fkonrad@amd.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <sys/mman.h>
#include <errno.h>

#ifndef MAP_FIXED_NOREPLACE
#include "mmap-fixed.h"

void *mmap_fixed_noreplace(void *addr, size_t len, int prot, int flags,
                                  int fd, off_t offset)
{
    void *retaddr;

    if (!(flags & MAP_FIXED_NOREPLACE)) {
        /* General case, use the regular mmap.  */
        return mmap(addr, len, prot, flags, fd, offset);
    }

    /* Since MAP_FIXED_NOREPLACE is not implemented, try to emulate it.  */
    flags = flags & ~(MAP_FIXED_NOREPLACE | MAP_FIXED);
    retaddr = mmap(addr, len, prot, flags, fd, offset);
    if ((retaddr == addr) || (retaddr == MAP_FAILED)) {
        /*
         * Either the map worked and we get the good address so it can be
         * returned, or it failed and would have failed the same with
         * MAP_FIXED*, in which case return MAP_FAILED.
         */
        return retaddr;
    } else {
        /*
         * Page has been mapped but not at the requested address.. unmap it and
         * return EEXIST.
         */
        munmap(retaddr, len);
        errno = EEXIST;
        return MAP_FAILED;
    }
}

#endif
