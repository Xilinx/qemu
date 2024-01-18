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

#ifndef MMAP_FIXED_H
#define MMAP_FIXED_H

#ifndef MAP_SHARED_VALIDATE
#define MAP_SHARED_VALIDATE 0x03
#endif

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000

void *mmap_fixed_noreplace(void *addr, size_t len, int prot, int flags,
                           int fd, off_t offset);

#else /* MAP_FIXED_NOREPLACE */
#define mmap_fixed_noreplace mmap
#endif /* MAP_FIXED_NOREPLACE */

#endif /* MMAP_FIXED_H */
