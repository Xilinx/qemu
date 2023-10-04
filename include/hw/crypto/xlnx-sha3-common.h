/*
 * Common base model for AMD / Xilinx SHA3 IPs.
 *
 * Copyright (c) 2023, Advanced Micro Devices, Inc.
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

#ifndef XLNX_SHA3_COMMON_H
#define XLNX_SHA3_COMMON_H

#include "crypto/keccak_sponge.h"
#include "hw/stream.h"

#define TYPE_XLNX_SHA3_COMMON "xlnx-sha3-common"
OBJECT_DECLARE_TYPE(XlnxSha3Common, XlnxSha3CommonClass, XLNX_SHA3_COMMON)

#define XLNX_SHA3_COMMON_MAX_BLOCK_SIZE (144)

enum XlnxSha3CommonAlg {
    SHA_MODE_UNIMPLEMENTED,
    SHA_MODE_256,
    SHA_MODE_384,
    SHA_MODE_512,
    SHA_MODE_SHAKE256,
};
typedef enum XlnxSha3CommonAlg XlnxSha3CommonAlg;

struct XlnxSha3Common {
    SysBusDevice parent_obj;

    uint32_t state;
    StreamCanPushNotifyFn notify;
    void *notify_opaque;

    /*
     * Data waiting for a complete block to be received before going in the
     * keccak sponge.
     */
    uint8_t data[XLNX_SHA3_COMMON_MAX_BLOCK_SIZE];
    /* Current data_ptr withing data[] above.  */
    uint8_t data_ptr;
    /* Keccak state.  */
    keccak_sponge_t sponge;
    /* Hash algorithm selected when starting.  */
    XlnxSha3CommonAlg alg;
};
typedef struct XlnxSha3Common XlnxSha3Common;

/**
 * struct XlnxSha3CommonClass:
 * @parent_class: the base class.
 * @is_autopadding_enabled: shall returns true if autopadding is enabled,
 *                          false otherwise depending on the register state.
 * @end_of_packet_notifier: shall update the registers when an end of packet
 *                          occurs from the DMA.
 * @write_digest: shall update the registers with the new digest.
 * @get_algorithm: shall return the current algorithm described by the
 *                 registers.
 *
 * Note that all those callback needs to be implemented by the child device.
 */
struct XlnxSha3CommonClass {
    SysBusDeviceClass parent_class;

    bool (*is_autopadding_enabled)(XlnxSha3Common *s);
    void (*end_of_packet_notifier)(XlnxSha3Common *s);
    void (*write_digest)(XlnxSha3Common *s, uint32_t *digest, size_t size);
    XlnxSha3CommonAlg (*get_algorithm)(XlnxSha3Common *s);
};
typedef struct XlnxSha3CommonClass XlnxSha3CommonClass;

/**
 * xlnx_sha3_common_set_algorithm:
 * @s: The common sha3 block to address.
 * @alg: The algorithm to use.
 *
 * This function will tell the @s device to use the @alg algorithm at the next
 * start.
 */
void xlnx_sha3_common_set_algorithm(XlnxSha3Common *s,
                                    XlnxSha3CommonAlg alg);
/**
 * xlnx_sha3_common_start:
 * @s: The device to start.
 *
 * This put the sha3 model in start mode.  It will wait for data on its stream
 * interface and hash them with the current algorithm set with
 * @xlnx_sha3_common_set_algorithm.
 */
void xlnx_sha3_common_start(XlnxSha3Common *s);

/**
 * xlnx_sha3_common_reset:
 * @s: The device to reset.
 * @reseting: The status of the reset line.
 *
 * Tell the @s device to reset its state when @reseting is @true, or to leave
 * reset mode when @reseting is @false.
 */
void xlnx_sha3_common_reset(XlnxSha3Common *s, int reseting);

/**
 * xlnx_sha3_common_update_digest:
 * @s: The device to update its digest.
 *
 * This tells the @s device to update the digest from the current state of the
 * keccak / sha3 sponge.  It will call the class write_digest_func in order to
 * push it in the registers.
 */
void xlnx_sha3_common_update_digest(XlnxSha3Common *s);

/**
 * xlnx_sha3_common_next_xof:
 * @s: The device to ask the next xof.
 *
 * This tells the @s device to compute the next bits of the SHAKE256 hash.  It
 * will call the class write_digest_func in order to push it in the registers.
 */
void xlnx_sha3_common_next_xof(XlnxSha3Common *s);

#endif /* XLNX_SHA3_COMMON_H */
