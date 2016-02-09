/*
 * QEMU NAND flash image creator.
 *
 * QEMU NAND flash backing files have a non-trivial data layout due to OOB data.
 * This program takes an input image and lays it out in the in-band sections and
 * sets the OOB to 0xFF spam.
 *
 * Copyright (C) 2013 - 2014 Xilinx, Inc.  All rights reserved.
 * Written by Peter Crosthwaite <peter.crosthwaite@xilinx.com>
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

#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#define ECC_CODEWORD_SIZE 512

#ifdef DEBUG
    #define DPRINT(...) do { fprintf(stdout, __VA_ARGS__); } while (0)
#else
    #define DPRINT(...) do { } while (0)
#endif

uint32_t ecc_size;
uint32_t ecc_pos;
uint32_t ecc_subpage_offset;

static void ecc_digest(uint8_t *data , uint8_t * oob, uint32_t bytes_read,
                       uint32_t page_size);

/* Command line positional inputs
 * 1. page size
 * 2. oob size
 * 3. num of pages per block
 * 5. num of blocks per lun
 * 4. ecc size
ex: for 32gb micron nand inputs are as below.
./qemu-nand-creator 16384 1216 256 1048 672 < BOOT.BIN

*/

int main(int argc, char *argv[])
{
    uint32_t page_size;
    uint32_t oob_size;
    uint32_t num_pages_per_block;
    uint32_t num_pages = 0;
    uint32_t block = 0;
    uint32_t num_of_blocks = 0;
    uint32_t bb_pat = 0;
    uint8_t *oob_pbuf;
    uint8_t *buf;
    uint8_t *ecc_data;
    int i, j;
    bool Image_done = false;
    bool page_empty = false;
    bool Image_wip = true;
    int nand_flash;

    const char *exe_name = argv[0];
    argc--;
    argv++;

    if (argc != 5) {
        fprintf(stderr, "Usage: %s <page size> <oob size> " \
                "<num of pages per block> <num_blocks> "\
                "<ecc size>\n", exe_name);
        return 1;
    }

    page_size = strtol(argv[0], NULL, 0);
    argc--;
    if (errno) {
        perror(argv[0]);
        exit(1);
    }

    oob_size = strtol(argv[1], NULL, 0);

    if (argc) {
        num_pages_per_block = strtol(argv[2], NULL, 0);
    }
    argc--;
    num_of_blocks = strtol(argv[3], NULL, 0);
    ecc_size = strtol(argv[4], NULL, 0);
    ecc_data = (uint8_t *)malloc(ecc_size);

    uint8_t *oob_buf = (uint8_t *)malloc(oob_size);
    memset(oob_buf, 0xFF, oob_size);

    nand_flash = open("./qemu_nand.bin", O_CREAT | O_WRONLY, S_IRWXU);

    /* bad block marker oob data */
    uint8_t oob_buf_bad[oob_size];
    memset(oob_buf_bad, 0xFF, oob_size);
    oob_buf_bad[0] = 0x00;

#ifdef CREATE_BB
    /* create random bad block pattern with first block as good */
    bb_pat = rand();

    /* First block is always good */
    bb_pat &= ~(1 << 0);
#endif

    /* Create Page Refernce buffer */
    buf = (uint8_t *)malloc(page_size);

    fprintf(stderr, "Creating Nand Flash Image:\n");
    while (true) {
        /* check if block is bad for first 32 blocks */
        bool block_is_bad = (block < 32) ? ((bb_pat >> block) & 0x1) : 0;

        if (block_is_bad) {
            /* Block is bad, make an empty page */
            page_empty = true;
            DPRINT("Bad Block %d\n", block);
        } else if (!Image_done) {
            /* Image Write in progress.. */
            page_empty = false;
            /* Clear oob & ecc area */
            memset(oob_buf, 0xFF, oob_size);
            memset(ecc_data, 0xFF, ecc_size);
            ecc_pos = 0;
            ecc_subpage_offset = 0;

            for (i = 0; i < page_size; ++i) {
                /* Read Input file, 1 page at a time */
                ssize_t bytes_read = read(STDIN_FILENO, &buf[i], page_size - i);
                DPRINT("Block %d page %d bytes_read %d\n", block,
                       num_pages, bytes_read);

                /* Calcualte Ecc digest based on read bytes */
                if (ecc_size && bytes_read) {
                    ecc_digest(&buf[i], ecc_data, bytes_read, page_size);
                    /* Copy ecc_data to OOB */
                    memcpy(&oob_buf[oob_size - ecc_size], ecc_data, ecc_size);
                    /* ECC debug Logging */
                    DPRINT("\tECC pos %d\nECC Digest:\n", ecc_pos);
                    for (j = oob_size - ecc_size; j < oob_size; j++) {
                        DPRINT("%d:%x ", j, oob_buf[j]);
                        if (!(j % 5)) {
                            DPRINT("\n");
                        }
                    }
                    DPRINT("\n");
                }

                switch (bytes_read) {
                case 0:
                    Image_done = true;
                    memset(&buf[i], 0xFF, page_size - i);
                    break;
                case -1:
                    perror("stdin");
                    return 1;
                default:
                    if (bytes_read < page_size) {
                        Image_done = true;
                        /* Clear Rest of the page and update ECC */
                        memset(&buf[bytes_read], 0xFF, page_size - bytes_read);
                        ecc_digest(&buf[bytes_read], ecc_data,
                                   page_size - bytes_read, page_size);
                        /* Refresh ECC data */
                        memcpy(&oob_buf[oob_size - ecc_size], ecc_data,
                               ecc_size);
                        i = page_size;
                    } else {
                        i += bytes_read;
                    }
                    break;
                }
            }
        } else {
            /* Empty Page */
            page_empty = true;
        }

        for (i = 0; i < page_size; ++i) {
            ssize_t bytes_written;
            if (page_empty == false) {
                bytes_written = write(nand_flash, &buf[i], page_size - i);
            } else {
                lseek64(nand_flash, page_size - i, SEEK_CUR);
                bytes_written = page_size;
            }

            switch (bytes_written) {
            case -1:
                perror("stdin");
                return 1;
            default:
                i += bytes_written;
            }
        }

        /* write bad block markers in spare area */
        oob_pbuf = (num_pages <= 1) && block_is_bad ? &oob_buf_bad[0]
                                                    : &oob_buf[0];
        for (i = 0; i < oob_size; ++i) {
            ssize_t bytes_written = write(nand_flash, &oob_pbuf[i],
                                          oob_size - i);
            switch (bytes_written) {
            case -1:
                perror("stdin");
                return 1;
            default:
                i += bytes_written;
            }
        }

        if (Image_wip == Image_done) {
            /* Image is written. So one final time clean the oob,
             * so it can be written every time
             */
            memset(oob_buf, 0xFF, oob_size);
            Image_wip = false;
        }
        num_pages++;
        /* check block boundary */
        if (num_pages_per_block && !(num_pages % num_pages_per_block)) {
            block++;
            fprintf(stderr, "\r. . .");
            if (block > num_of_blocks) {
                goto done;
            }
            num_pages = 0;
        }

    }

done:
    fprintf(stderr, "Done!\n");
    sync();
    close(nand_flash);
    return 0;
}

static void ecc_digest(uint8_t *data , uint8_t * oob,
                       uint32_t bytes_read, uint32_t page_size)
{
    int ecc_bytes_per_subpage =  ecc_size /
                                    (page_size / ECC_CODEWORD_SIZE);
    uint32_t head = 0;
    while (head < bytes_read) {
        oob[ecc_pos++] ^= ~data[head];
        if (!(ecc_pos % ecc_bytes_per_subpage)) {
            ecc_pos -= ecc_bytes_per_subpage;
        }

        ecc_subpage_offset++;
        if (ecc_subpage_offset == ECC_CODEWORD_SIZE) {
            ecc_subpage_offset = 0;
            do {
                ecc_pos++;
            } while (ecc_pos % ecc_bytes_per_subpage);
        }
        head++;
    }
}
