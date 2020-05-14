/*
 * QEMU model of ZynqMP CSU SHA-3 block
 *
 * Copyright (c) 2013 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 * Uses borrowed LGPL code from nettle.
 * This code is licensed under the GNU LGPL.
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

/* The sha3 state is a 5x5 matrix of 64-bit words. In the notation of
   Keccak description, S[x,y] is element x + 5*y, so if x is
   interpreted as the row index and y the column index, it is stored
   in column-major order. */
#define SHA3_STATE_LENGTH 25

/* The "width" is 1600 bits or 200 octets */
struct sha3_state {
  uint64_t a[SHA3_STATE_LENGTH];
};

struct sha3_384_ctx {
    struct sha3_state state;
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

/* Borrowed from nettle. Once distros get an OpenSSL version
 * that has SHA-3 support, this should be removed.
 *
 * Implements the core SHA-3 parts but excludes padding to
 * match common hardware implementations.
 */

#define SHA3_ROUNDS 24

#define ROTL64(n, x) (((x)<<(n)) | ((x)>>(64-(n))))

static void sha3_permute(struct sha3_state *state)
{
    static const uint64_t rc[SHA3_ROUNDS] = {
        0x0000000000000001ULL, 0X0000000000008082ULL,
        0X800000000000808AULL, 0X8000000080008000ULL,
        0X000000000000808BULL, 0X0000000080000001ULL,
        0X8000000080008081ULL, 0X8000000000008009ULL,
        0X000000000000008AULL, 0X0000000000000088ULL,
        0X0000000080008009ULL, 0X000000008000000AULL,
        0X000000008000808BULL, 0X800000000000008BULL,
        0X8000000000008089ULL, 0X8000000000008003ULL,
        0X8000000000008002ULL, 0X8000000000000080ULL,
        0X000000000000800AULL, 0X800000008000000AULL,
        0X8000000080008081ULL, 0X8000000000008080ULL,
        0X0000000080000001ULL, 0X8000000080008008ULL,
    };

  /* Original permutation:

       0,10,20, 5,15,
      16, 1,11,21, 6,
       7,17, 2,12,22,
      23, 8,18, 3,13,
      14,24, 9,19, 4

     Rotation counts:

       0,  1, 62, 28, 27,
      36, 44,  6, 55, 20,
       3, 10, 43, 25, 39,
      41, 45, 15, 21,  8,
      18,  2, 61, 56, 14,
  */

  /* In-place implementation. Permutation done as a long sequence of
     25 moves "following" the permutation.

      T <--  1
      1 <--  6
      6 <--  9
      9 <-- 22
     22 <-- 14
     14 <-- 20
     20 <--  2
      2 <-- 12
     12 <-- 13
     13 <-- 19
     19 <-- 23
     23 <-- 15
     15 <--  4
      4 <-- 24
     24 <-- 21
     21 <--  8
      8 <-- 16
     16 <--  5
      5 <--  3
      3 <-- 18
     18 <-- 17
     17 <-- 11
     11 <--  7
      7 <-- 10
     10 <--  T

  */
    uint64_t C[5], D[5], T, X;
    unsigned i, y;

#define A (state->a)

    C[0] = A[0] ^ A[5+0] ^ A[10+0] ^ A[15+0] ^ A[20+0];
    C[1] = A[1] ^ A[5+1] ^ A[10+1] ^ A[15+1] ^ A[20+1];
    C[2] = A[2] ^ A[5+2] ^ A[10+2] ^ A[15+2] ^ A[20+2];
    C[3] = A[3] ^ A[5+3] ^ A[10+3] ^ A[15+3] ^ A[20+3];
    C[4] = A[4] ^ A[5+4] ^ A[10+4] ^ A[15+4] ^ A[20+4];

    for (i = 0; i < SHA3_ROUNDS; i++) {
        D[0] = C[4] ^ ROTL64(1, C[1]);
        D[1] = C[0] ^ ROTL64(1, C[2]);
        D[2] = C[1] ^ ROTL64(1, C[3]);
        D[3] = C[2] ^ ROTL64(1, C[4]);
        D[4] = C[3] ^ ROTL64(1, C[0]);

        A[0] ^= D[0];
        X = A[ 1] ^ D[1];     T = ROTL64(1, X);
        X = A[ 6] ^ D[1]; A[ 1] = ROTL64(44, X);
        X = A[ 9] ^ D[4]; A[ 6] = ROTL64(20, X);
        X = A[22] ^ D[2]; A[ 9] = ROTL64(61, X);
        X = A[14] ^ D[4]; A[22] = ROTL64(39, X);
        X = A[20] ^ D[0]; A[14] = ROTL64(18, X);
        X = A[ 2] ^ D[2]; A[20] = ROTL64(62, X);
        X = A[12] ^ D[2]; A[ 2] = ROTL64(43, X);
        X = A[13] ^ D[3]; A[12] = ROTL64(25, X);
        X = A[19] ^ D[4]; A[13] = ROTL64( 8, X);
        X = A[23] ^ D[3]; A[19] = ROTL64(56, X);
        X = A[15] ^ D[0]; A[23] = ROTL64(41, X);
        X = A[ 4] ^ D[4]; A[15] = ROTL64(27, X);
        X = A[24] ^ D[4]; A[ 4] = ROTL64(14, X);
        X = A[21] ^ D[1]; A[24] = ROTL64( 2, X);
        X = A[ 8] ^ D[3]; A[21] = ROTL64(55, X); /* row 4 done */
        X = A[16] ^ D[1]; A[ 8] = ROTL64(45, X);
        X = A[ 5] ^ D[0]; A[16] = ROTL64(36, X);
        X = A[ 3] ^ D[3]; A[ 5] = ROTL64(28, X);
        X = A[18] ^ D[3]; A[ 3] = ROTL64(21, X); /* row 0 done */
        X = A[17] ^ D[2]; A[18] = ROTL64(15, X);
        X = A[11] ^ D[1]; A[17] = ROTL64(10, X); /* row 3 done */
        X = A[ 7] ^ D[2]; A[11] = ROTL64( 6, X); /* row 1 done */
        X = A[10] ^ D[0]; A[ 7] = ROTL64( 3, X);
        A[10] = T;                                /* row 2 done */

        D[0] = ~A[1] & A[2];
        D[1] = ~A[2] & A[3];
        D[2] = ~A[3] & A[4];
        D[3] = ~A[4] & A[0];
        D[4] = ~A[0] & A[1];

        A[0] ^= D[0] ^ rc[i]; C[0] = A[0];
        A[1] ^= D[1]; C[1] = A[1];
        A[2] ^= D[2]; C[2] = A[2];
        A[3] ^= D[3]; C[3] = A[3];
        A[4] ^= D[4]; C[4] = A[4];

        for (y = 5; y < 25; y += 5) {
            D[0] = ~A[y+1] & A[y+2];
            D[1] = ~A[y+2] & A[y+3];
            D[2] = ~A[y+3] & A[y+4];
            D[3] = ~A[y+4] & A[y+0];
            D[4] = ~A[y+0] & A[y+1];

            A[y+0] ^= D[0]; C[0] ^= A[y+0];
            A[y+1] ^= D[1]; C[1] ^= A[y+1];
            A[y+2] ^= D[2]; C[2] ^= A[y+2];
            A[y+3] ^= D[3]; C[3] ^= A[y+3];
            A[y+4] ^= D[4]; C[4] ^= A[y+4];
        }
    }
#undef A
}

static void memxor(uint8_t *a, const uint8_t *b, unsigned int len)
{
    int i;

    for (i = 0; i < len; i++) {
        *a = *a ^ *b;
         a++;
         b++;
    }
}

static void
sha3_absorb(struct sha3_state *state, unsigned length, const uint8_t *data)
{
    assert((length & 7) == 0);
#ifdef HOST_WORDS_BIGENDIAN
    {
        uint64_t *p;
        for (p = state->a; length > 0; p++, length -= 8, data += 8) {
            uint64_t v64;
            memcpy(&v64, data, sizeof v64);
            *p ^= cpu_to_le64(v64);
        }
    }
#else /* !WORDS_BIGENDIAN */
    memxor((uint8_t *) state->a, data, length);
#endif

    sha3_permute(state);
}

static unsigned sha3_update(struct sha3_state *state,
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
            sha3_absorb(state, block_size, block);
        }
    }
    for (; length >= block_size; length -= block_size, data += block_size) {
        sha3_absorb(state, block_size, data);
    }

    memcpy(block, data, length);
    return length;
}

static void sha3_384_init(struct sha3_384_ctx *ctx)
{
    memset(&ctx->state, 0, offsetof(struct sha3_384_ctx, block));
}

static void sha3_384_update(struct sha3_384_ctx *ctx,
                            unsigned length,
                            const uint8_t *data)
{
    ctx->index = sha3_update(&ctx->state, SHA3_384_DATA_SIZE, ctx->block,
                             ctx->index, length, data);
}

static void write_le64(unsigned length, uint8_t *dst,
                       uint64_t *src)
{
    unsigned i;
    unsigned words;
    unsigned leftover;

    words = length / 8;
    leftover = length % 8;

    for (i = 0; i < words; i++, dst += 8) {
        uint64_t v64 = cpu_to_le64(src[i]);
        memcpy(dst, &v64, sizeof v64);
    }

    if (leftover) {
        uint64_t word;
        word = src[i];

        do {
            *dst++ = word & 0xff;
            word >>= 8;
        } while (--leftover);
    }
}

static void sha3_384_digest_no_pad(struct sha3_384_ctx *ctx,
                                   unsigned length,
                                   uint8_t *digest)
{
    write_le64(length, digest, ctx->state.a);
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
        VMSTATE_UINT64_ARRAY(ctx.state.a, ZynqMPCSUSHA3, SHA3_STATE_LENGTH),

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
