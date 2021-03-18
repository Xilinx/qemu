/*
 * QEMU model of ZynqMP CSU SHA-3 block
 *
 * Copyright (c) 2013 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

#include "hw/hw.h"
#include "hw/stream.h"
#include "qemu/bitops.h"
#include "sysemu/dma.h"
#include "hw/register.h"

#include "crypto/keccak_sponge.h"

#ifndef ZYNQMP_CSU_SHA3_ERR_DEBUG
#define ZYNQMP_CSU_SHA3_ERR_DEBUG 0
#endif

#define TYPE_ZYNQMP_CSU_SHA3 "zynqmp.csu-sha3"

#define ZYNQMP_CSU_SHA3(obj) \
     OBJECT_CHECK(ZynqMPCSUSHA3, (obj), TYPE_ZYNQMP_CSU_SHA3)

REG32(SHA_START, 0x00)
    FIELD(SHA_START, START_MSG, 0, 1)
REG32(SHA_RESET, 0x04)
    FIELD(SHA_RESET, RESET, 0, 1)
REG32(SHA_DONE, 0x08)
    FIELD(SHA_DONE, SHA_DONE, 0, 1)
REG32(SHA_DIGEST_0, 0x10)
REG32(SHA_DIGEST_1, 0x14)
REG32(SHA_DIGEST_2, 0x18)
REG32(SHA_DIGEST_3, 0x1c)
REG32(SHA_DIGEST_4, 0x20)
REG32(SHA_DIGEST_5, 0x24)
REG32(SHA_DIGEST_6, 0x28)
REG32(SHA_DIGEST_7, 0x2c)
REG32(SHA_DIGEST_8, 0x30)
REG32(SHA_DIGEST_9, 0x34)
REG32(SHA_DIGEST_10, 0x38)
REG32(SHA_DIGEST_11, 0x3c)

#define R_MAX                      (R_SHA_DIGEST_11 + 1)

static const RegisterAccessInfo sha3_regs_info[] = {
    {   .name = "SHA_START",  .addr = A_SHA_START,
    },{ .name = "SHA_RESET",  .addr = A_SHA_RESET,
        .ro = R_SHA_RESET_RESET_MASK,
    },{ .name = "SHA_DONE",  .addr = A_SHA_DONE,
        .ro = 0x1,
    },{ .name = "SHA_DIGEST_0",  .addr = A_SHA_DIGEST_0,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_1",  .addr = A_SHA_DIGEST_1,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_2",  .addr = A_SHA_DIGEST_2,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_3",  .addr = A_SHA_DIGEST_3,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_4",  .addr = A_SHA_DIGEST_4,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_5",  .addr = A_SHA_DIGEST_5,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_6",  .addr = A_SHA_DIGEST_6,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_7",  .addr = A_SHA_DIGEST_7,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_8",  .addr = A_SHA_DIGEST_8,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_9",  .addr = A_SHA_DIGEST_9,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_10",  .addr = A_SHA_DIGEST_10,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_11",  .addr = A_SHA_DIGEST_11,
        .ro = 0xffffffff,
    }
};

enum State {
    IDLE = 0,
    RESETING,
    RUNNING,
};

#define SHA3_384_DIGEST_SIZE 48
#define SHA3_384_DATA_SIZE 104

struct sha3_384_ctx {
    keccak_sponge_t state;
    uint32_t index;
    uint8_t block[SHA3_384_DATA_SIZE];
};

typedef struct ZynqMPCSUSHA3 {
    SysBusDevice busdev;
    MemoryRegion iomem;

    struct sha3_384_ctx ctx;

    uint32_t state;
    uint32_t data_count;
    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];

    StreamCanPushNotifyFn notify;
    void *notify_opaque;
    /* debug only */
    const char *prefix;
} ZynqMPCSUSHA3;

#define SHA3_BLOCK_SIZE 104

static unsigned sha3_update(keccak_sponge_t *state,
                            unsigned block_size, uint8_t *block,
                            unsigned pos,
                            unsigned length, const uint8_t *data)
{
    if (pos > 0) {
        unsigned left = block_size - pos;
        if (length < left) {
            memcpy(block + pos, data, length);
            return pos + length;
        } else {
            memcpy(block + pos, data, left);
            data += left;
            length -= left;
            keccak_absorb(state, block_size, block);
        }
    }
    for (; length >= block_size; length -= block_size, data += block_size) {
        keccak_absorb(state, block_size, data);
    }

    memcpy(block, data, length);
    return length;
}

static void sha3_384_init(struct sha3_384_ctx *ctx)
{
    keccak_init(&ctx->state);
}

static void sha3_384_update(struct sha3_384_ctx *ctx,
                            unsigned length,
                            const uint8_t *data)
{
    ctx->index = sha3_update(&ctx->state, SHA3_384_DATA_SIZE, ctx->block,
                             ctx->index, length, data);
}

static void sha3_384_digest_no_pad(struct sha3_384_ctx *ctx,
                                   unsigned length,
                                   uint8_t *digest)
{
    keccak_squeeze(&ctx->state, length, digest);
    sha3_384_init(ctx);
}

static void xlx_sha3_emit_digest(ZynqMPCSUSHA3 *s)
{
    /* Temporary state copy for reading out the digest.  */
    struct sha3_384_ctx ctx_ro;
    union {
        uint8_t u8[48];
        uint32_t u32[12];
    } digest;
    int i;

    /* Make a copy if the current state.  */
    memcpy(&ctx_ro, &s->ctx, sizeof s->ctx);
    sha3_384_digest_no_pad(&ctx_ro, 48, digest.u8);

    /* Store the digest in SHA_DIGEST_X. In reverse word order.  */
    for (i = 0; i < 12; i++) {
        s->regs[R_SHA_DIGEST_0 + i] = digest.u32[11 - i];
    }
}

static size_t xlx_sha3_stream_push(StreamSlave *obj, uint8_t *buf, size_t len,
                                   bool eop)
{
    ZynqMPCSUSHA3 *s = ZYNQMP_CSU_SHA3(obj);
    unsigned int excess_len;

    if (s->state != RUNNING) {
        hw_error("%s: Data in bad state %d\n", s->prefix, s->state);
        return 0;
    }

    excess_len = (s->data_count + len) % SHA3_BLOCK_SIZE;
    s->data_count = excess_len;
    if (excess_len >= len) {
        /* We dont reach a block boundary.  */
        sha3_384_update(&s->ctx, len, buf);
        return len;
    }

    sha3_384_update(&s->ctx, len - excess_len, buf);

    /* The SHA-3 block will continously compress it's state and emit the
       digest.  */
    xlx_sha3_emit_digest(s);

    if (excess_len) {
        sha3_384_update(&s->ctx, excess_len, buf + len - excess_len);
    }

    if (eop) {
        ARRAY_FIELD_DP32(s->regs, SHA_DONE, SHA_DONE, true);
    }
    return len;
}

static bool xlx_sha3_stream_can_push(StreamSlave *obj,
                                    StreamCanPushNotifyFn notify,
                                    void *notify_opaque)
{
    ZynqMPCSUSHA3 *s = ZYNQMP_CSU_SHA3(obj);
    return s->state == RUNNING;
}

static void xlx_sha3_reset(DeviceState *dev)
{
    ZynqMPCSUSHA3 *s = ZYNQMP_CSU_SHA3(dev);
    int i;

    for (i = 0; i < R_MAX; ++i) {
        register_reset(&s->regs_info[i]);
    }
    s->data_count = 0;
}

static void xlx_sha3_write(void *opaque, hwaddr addr, uint64_t value,
                      unsigned size)
{
    RegisterInfoArray *reg_array = opaque;
    ZynqMPCSUSHA3 *s = ZYNQMP_CSU_SHA3(reg_array->r[0]->opaque);

    register_write_memory(opaque, addr, value, size);

    addr >>= 2;

    switch (addr) {
    case R_SHA_START:
        if (value & R_SHA_START_START_MSG_MASK && s->state != RESETING) {
            sha3_384_init(&s->ctx);
            s->data_count = 0;
            s->state = RUNNING;
            ARRAY_FIELD_DP32(s->regs, SHA_DONE, SHA_DONE, false);

            /* Assume empty digest is available at init.  */
            xlx_sha3_emit_digest(s);
        }
        break;
    case R_SHA_RESET:
        if (value & R_SHA_RESET_RESET_MASK) {
            s->state = RESETING;
        } else {
            xlx_sha3_reset((void *) s);
            s->state = IDLE;
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps sha3_ops = {
    .read = register_read_memory,
    .write = xlx_sha3_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void sha3_init(Object *obj)
{
    ZynqMPCSUSHA3 *s = ZYNQMP_CSU_SHA3(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    s->prefix = object_get_canonical_path(obj);

    memory_region_init(&s->iomem, obj, TYPE_ZYNQMP_CSU_SHA3, R_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), sha3_regs_info,
                              ARRAY_SIZE(sha3_regs_info),
                              s->regs_info, s->regs,
                              &sha3_ops,
                              ZYNQMP_CSU_SHA3_ERR_DEBUG,
                              R_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                0x0,
                                &reg_array->mem);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_sha3 = {
    .name = "zynqmp_csu_sha3",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ctx.index, ZynqMPCSUSHA3),
        VMSTATE_UINT8_ARRAY(ctx.block, ZynqMPCSUSHA3, SHA3_384_DATA_SIZE),
        VMSTATE_UINT64_ARRAY(ctx.state.a, ZynqMPCSUSHA3,
                             ARRAY_SIZE(((struct sha3_384_ctx *)0)->state.a)),

        VMSTATE_UINT32(state, ZynqMPCSUSHA3),
        VMSTATE_UINT32(data_count, ZynqMPCSUSHA3),
        VMSTATE_UINT32_ARRAY(regs, ZynqMPCSUSHA3, R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void sha3_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    StreamSlaveClass *ssc = STREAM_SLAVE_CLASS(klass);

    dc->reset = xlx_sha3_reset;
    dc->vmsd = &vmstate_sha3;

    ssc->push = xlx_sha3_stream_push;
    ssc->can_push = xlx_sha3_stream_can_push;
}

static const TypeInfo sha3_info = {
    .name          = TYPE_ZYNQMP_CSU_SHA3,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ZynqMPCSUSHA3),
    .class_init    = sha3_class_init,
    .instance_init = sha3_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_STREAM_SLAVE },
        { }
    }
};

static void sha3_register_types(void)
{
    type_register_static(&sha3_info);
}

type_init(sha3_register_types)
