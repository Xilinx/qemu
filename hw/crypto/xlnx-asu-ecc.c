/*
 * Caliptra ECC ECDSA block
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 * Author: Luc Michel <luc.michel@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "crypto/ecdsa.h"
#include "hw/irq.h"
#include "hw/registerfields.h"
#include "hw/crypto/xlnx-asu-ecc.h"
#include "trace.h"

REG32(CTRL, 0x10)
    FIELD(CTRL, SUPPRESSSCP, 9, 1)
    FIELD(CTRL, SUPPRESSSCP2, 8, 1)
    FIELD(CTRL, RESET, 7, 1)
    FIELD(CTRL, CURVE, 5, 1)
    FIELD(CTRL, OPCODE, 1, 3)
    FIELD(CTRL, START, 0, 1)

enum AsuEccOpcode {
    ASU_ECC_OP_SIG_VERIF = 0,
    ASU_ECC_OP_PUB_KEY_VALID = 1,
    ASU_ECC_OP_PUB_KEY_GEN = 2,
    ASU_ECC_OP_SIG_GEN = 3,
};

REG32(STATUS, 0x14)
    FIELD(STATUS, BUSY, 7, 1)
    FIELD(STATUS, SCPENABLED, 5, 1)
    FIELD(STATUS, TERMINATION_CODE, 0, 4)

enum AsuEccTerminationCode {
    ASU_ECC_SUCCESS = 0,
    ASU_ECC_BAD_OPCODE = 1,
    ASU_ECC_R_ZERO = 2,
    ASU_ECC_S_ZERO = 3,
    ASU_ECC_R_GT_N_1 = 4,
    ASU_ECC_S_GT_N_1 = 5,
    ASU_ECC_SIG_MISMATCH = 6,
    ASU_ECC_Q_NOT_ON_CURVE = 7,
    ASU_ECC_QX_ZERO = 8,
    ASU_ECC_QY_ZERO = 9,
    ASU_ECC_QX_GT_N_1 = 10,
    ASU_ECC_QY_GT_N_1 = 11,
    ASU_ECC_Q_BAD_ORDER = 12,
};

REG32(RESET, 0x40)
    FIELD(RESET, RESET, 0, 1)

REG32(ISR, 0x48)
    FIELD(ISR, DONE, 0, 1)

REG32(IMR, 0x4c)
REG32(IER, 0x50)
REG32(IDR, 0x54)
REG32(ITR, 0x58)

REG32(CFG, 0x5c)
    FIELD(CFG, WR_ENDIANNESS, 0, 1)
    FIELD(CFG, RD_ENDIANNESS, 1, 1)

REG32(MEM, 0x200)
REG32(MEM_LAST_RW, 0x2ec)
REG32(MEM_FIRST_WO, 0x2f0)
REG32(MEM_LAST, 0x34c)

/* Signature generation offsets */
/* Inputs */
#define MEM_SIG_GEN_D_OFFSET 0x0   /* priv key */
#define MEM_SIG_GEN_K_OFFSET 0x30  /* random number */
#define MEM_SIG_GEN_Z_OFFSET 0x60  /* hash to sign */
/* Outputs */
#define MEM_SIG_GEN_R_OFFSET 0x0   /* signature */
#define MEM_SIG_GEN_S_OFFSET 0x30

/* Signature verification offsets */
/* Inputs */
#define MEM_SIG_VERIF_R_OFFSET 0x0  /* signature to check */
#define MEM_SIG_VERIF_S_OFFSET 0x30
#define MEM_SIG_VERIF_Z_OFFSET 0x60 /* hash to check */
#define MEM_SIG_VERIF_X_OFFSET 0x90 /* pub key */
#define MEM_SIG_VERIF_Y_OFFSET 0xc0

/* Public key validation */
/* Inputs */
#define MEM_PUB_KEY_VALID_X_OFFSET 0x90 /* pub key to check */
#define MEM_PUB_KEY_VALID_Y_OFFSET 0xc0

/* Public key generation (computation from private key) */
/* Inputs */
#define MEM_PUB_KEY_GEN_D_OFFSET 0x30 /* priv key */
/* Outputs */
#define MEM_PUB_KEY_GEN_X_OFFSET 0x90 /* pub key */
#define MEM_PUB_KEY_GEN_Y_OFFSET 0xc0


/*
 * About error reporting: the hardware does not expose an appropriate error
 * code for all error conditions. Let's use a common error code in those cases.
 */
static const enum AsuEccTerminationCode DEFAULT_ERROR = ASU_ECC_BAD_OPCODE;

static void update_irq(XilinxAsuEccState *s)
{
    bool sta;

    sta = !!(s->isr & ~s->imr);
    qemu_set_irq(s->irq, sta);
}

static void raise_irq(XilinxAsuEccState *s, uint32_t mask)
{
    mask &= R_ISR_DONE_MASK;
    s->isr |= mask;
    update_irq(s);
}

static void clear_irq(XilinxAsuEccState *s, uint32_t mask)
{
    mask &= R_ISR_DONE_MASK;
    s->isr &= ~mask;
    update_irq(s);
}

static void enable_irq(XilinxAsuEccState *s, uint32_t mask)
{
    mask &= R_ISR_DONE_MASK;
    s->imr |= mask;
    update_irq(s);
}

static void disable_irq(XilinxAsuEccState *s, uint32_t mask)
{
    mask &= R_ISR_DONE_MASK;
    s->imr &= ~mask;
    update_irq(s);
}

static inline QCryptoEcdsaCurve get_curve(XilinxAsuEccState *s)
{
    return (s->ctrl & R_CTRL_CURVE_MASK)
        ? QCRYPTO_ECDSA_NIST_P384
        : QCRYPTO_ECDSA_NIST_P256;
}

static inline size_t get_curve_data_len(XilinxAsuEccState *s)
{
    return qcrypto_ecdsa_get_curve_data_size(get_curve(s));
}

static inline uint8_t *get_mem_ptr(XilinxAsuEccState *s, size_t offset,
                                   size_t len)
{
    uint8_t *p = (uint8_t *) s->mem;

    g_assert((offset + len) <= sizeof(s->mem));

    return p + offset;
}

static inline void set_status_term_code(XilinxAsuEccState *s,
                                        enum AsuEccTerminationCode code)
{
    s->status = FIELD_DP32(s->status, STATUS, TERMINATION_CODE, code);
}

static inline bool buffer_is_zero(const uint8_t *buf, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        if (buf[i]) {
            return false;
        }
    }

    return true;
}

static void do_op_sign(XilinxAsuEccState *s)
{
    g_autoptr(QCryptoEcdsa) ecdsa;
    QCryptoEcdsaCurve curve;
    QCryptoEcdsaStatus ret;
    size_t len;
    uint8_t *p, *sig_r, *sig_s;

    curve = get_curve(s);
    len = get_curve_data_len(s);

    ecdsa = qcrypto_ecdsa_new(curve);


    p = get_mem_ptr(s, MEM_SIG_GEN_D_OFFSET, len);
    ret = qcrypto_ecdsa_set_priv_key(ecdsa, p, len, NULL);
    if (ret != QCRYPTO_ECDSA_OK) {
        set_status_term_code(s, DEFAULT_ERROR);
        return;
    }

    p = get_mem_ptr(s, MEM_SIG_GEN_K_OFFSET, len);
    ret = qcrypto_ecdsa_set_random(ecdsa, p, len, NULL);
    if (ret != QCRYPTO_ECDSA_OK) {
        set_status_term_code(s, DEFAULT_ERROR);
        return;
    }

    p = get_mem_ptr(s, MEM_SIG_GEN_Z_OFFSET, len);
    ret = qcrypto_ecdsa_set_hash(ecdsa, p, len, NULL);
    if (ret != QCRYPTO_ECDSA_OK) {
        /*
         * This one is not supposed to fail. There is no constraint on the hash
         * value.
         */
        set_status_term_code(s, DEFAULT_ERROR);
        return;
    }

    ret = qcrypto_ecdsa_sign(ecdsa, NULL);
    if (ret != QCRYPTO_ECDSA_OK) {
        /*
         * We can't know for sure whether R or S was 0. Arbitrarily choose R.
         * In any case it means that K is inappropriate.
         */
        set_status_term_code(s, ASU_ECC_R_ZERO);
        return;

    }

    sig_r = get_mem_ptr(s, MEM_SIG_GEN_R_OFFSET, len);
    sig_s = get_mem_ptr(s, MEM_SIG_GEN_S_OFFSET, len);

    ret = qcrypto_ecdsa_get_sig(ecdsa, sig_r, len, sig_s, len, NULL);
    if (ret != QCRYPTO_ECDSA_OK) {
        /*
         * This one is not supposed to fail. If the signature operation
         * succeeded, R and S should be available.
         */
        set_status_term_code(s, DEFAULT_ERROR);
        return;
    }

    set_status_term_code(s, ASU_ECC_SUCCESS);
}

static void do_op_sign_verif(XilinxAsuEccState *s)
{
    g_autoptr(QCryptoEcdsa) ecdsa;
    QCryptoEcdsaCurve curve;
    size_t len;
    uint8_t *p, *sig_r, *sig_s, *pub_x, *pub_y;
    QCryptoEcdsaStatus ret;

    curve = get_curve(s);
    len = get_curve_data_len(s);

    ecdsa = qcrypto_ecdsa_new(curve);

    p = get_mem_ptr(s, MEM_SIG_VERIF_Z_OFFSET, len);
    ret = qcrypto_ecdsa_set_hash(ecdsa, p, len, NULL);
    if (ret != QCRYPTO_ECDSA_OK) {
        /*
         * This one is not supposed to fail. There is no constraint on the hash
         * value.
         */
        set_status_term_code(s, DEFAULT_ERROR);
        return;
    }

    sig_r = get_mem_ptr(s, MEM_SIG_VERIF_R_OFFSET, len);

    if (buffer_is_zero(sig_r, len)) {
        set_status_term_code(s, ASU_ECC_R_ZERO);
        return;
    }

    sig_s = get_mem_ptr(s, MEM_SIG_VERIF_S_OFFSET, len);

    if (buffer_is_zero(sig_s, len)) {
        set_status_term_code(s, ASU_ECC_S_ZERO);
        return;
    }

    ret = qcrypto_ecdsa_set_sig(ecdsa, sig_r, len, sig_s, len, NULL);

    if (ret == QCRYPTO_ECDSA_SIG_R_OUT_OF_RANGE) {
        set_status_term_code(s, ASU_ECC_R_GT_N_1);
        return;
    }

    if (ret == QCRYPTO_ECDSA_SIG_S_OUT_OF_RANGE) {
        set_status_term_code(s, ASU_ECC_S_GT_N_1);
        return;
    }

    pub_x = get_mem_ptr(s, MEM_SIG_VERIF_X_OFFSET, len);

    if (buffer_is_zero(pub_x, len)) {
        set_status_term_code(s, ASU_ECC_QX_ZERO);
        return;
    }

    pub_y = get_mem_ptr(s, MEM_SIG_VERIF_Y_OFFSET, len);

    if (buffer_is_zero(pub_y, len)) {
        set_status_term_code(s, ASU_ECC_QY_ZERO);
        return;
    }

    switch (qcrypto_ecdsa_set_pub_key(ecdsa, pub_x, len, pub_y, len, NULL)) {
    case QCRYPTO_ECDSA_OK:
        break;

    case QCRYPTO_ECDSA_PUB_KEY_X_OUT_OF_RANGE:
        set_status_term_code(s, ASU_ECC_QX_GT_N_1);
        return;

    case QCRYPTO_ECDSA_PUB_KEY_Y_OUT_OF_RANGE:
        set_status_term_code(s, ASU_ECC_QY_GT_N_1);
        return;

    case QCRYPTO_ECDSA_PUB_KEY_NOT_ON_CURVE:
        set_status_term_code(s, ASU_ECC_Q_NOT_ON_CURVE);
        return;

    case QCRYPTO_ECDSA_UNKNOWN_ERROR:
        set_status_term_code(s, DEFAULT_ERROR);
        return;

    default:
        g_assert_not_reached();
    }

    switch (qcrypto_ecdsa_verify(ecdsa, NULL)) {
    case QCRYPTO_ECDSA_OK:
        set_status_term_code(s, ASU_ECC_SUCCESS);
        break;

    case QCRYPTO_ECDSA_SIG_MISMATCH:
        set_status_term_code(s, ASU_ECC_SIG_MISMATCH);
        break;

    case QCRYPTO_ECDSA_UNKNOWN_ERROR:
        set_status_term_code(s, DEFAULT_ERROR);
        break;

    default:
        g_assert_not_reached();
    }
}

static void write_ctrl(XilinxAsuEccState *s, uint32_t val)
{
    bool reset, start;
    enum AsuEccOpcode opcode;


    s->ctrl = val & (R_CTRL_SUPPRESSSCP_MASK
                     | R_CTRL_SUPPRESSSCP2_MASK
                     | R_CTRL_CURVE_MASK);

    reset = FIELD_EX32(val, CTRL, RESET);
    start = FIELD_EX32(val, CTRL, START);
    opcode = FIELD_EX32(val, CTRL, OPCODE);

    if (reset) {
        qemu_log_mask(LOG_UNIMP, "xilinx-asu-ecc: unimplemented reset "
                      "field in CTRL register\n");
    }

    if (!start) {
        return;
    }

    switch (opcode) {
    case ASU_ECC_OP_SIG_VERIF:
        do_op_sign_verif(s);
        break;

    case ASU_ECC_OP_SIG_GEN:
        do_op_sign(s);
        break;

    default:
        set_status_term_code(s, ASU_ECC_BAD_OPCODE);
        return;
    }

    raise_irq(s, R_ISR_DONE_MASK);
}

static inline uint32_t read_status(const XilinxAsuEccState *s)
{
    uint32_t ret;
    bool scp_enabled;

    scp_enabled = !FIELD_EX32(s->ctrl, CTRL, SUPPRESSSCP)
        && !FIELD_EX32(s->ctrl, CTRL, SUPPRESSSCP2);

    ret = FIELD_DP32(s->status, STATUS, SCPENABLED, scp_enabled);

    return ret;
}

static inline void do_reset(XilinxAsuEccState *s)
{
    s->ctrl = 0;
    s->status = 0;
    s->reset = true;
    s->isr = 0;
    s->imr = R_ISR_DONE_MASK;
    s->cfg = 0;
    memset(s->mem, 0, sizeof(s->mem));
}

static uint64_t xilinx_asu_ecc_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    XilinxAsuEccState *s = XILINX_ASU_ECC(opaque);
    uint32_t ret;

    switch (addr) {
    case A_STATUS:
        ret = read_status(s);
        break;

    case A_RESET:
        ret = FIELD_DP32(0, RESET, RESET, s->reset);
        break;

    case A_ISR:
        ret = s->isr;
        break;

    case A_IMR:
        ret = s->imr;
        break;

    case A_CFG:
        ret = s->cfg;
        break;

    case A_MEM ... A_MEM_LAST_RW:
        if (FIELD_EX32(s->cfg, CFG, RD_ENDIANNESS)) {
            ret = cpu_to_le32(s->mem[(addr - A_MEM) / sizeof(uint32_t)]);
        } else {
            ret = cpu_to_be32(s->mem[(addr - A_MEM) / sizeof(uint32_t)]);
        }
        break;

    /* wo */
    case A_CTRL:
    case A_IER:
    case A_IDR:
    case A_ITR:
    case A_MEM_FIRST_WO ... A_MEM_LAST:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "xilinx-asu-ecc: read to write only register at"
                      " offset %" HWADDR_PRIx, addr);
        ret = 0;
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "xilinx-asu-ecc: read to unimplemented register at"
                      " offset %" HWADDR_PRIx, addr);
        ret = 0;
        break;
    }

    trace_xilinx_asu_ecc_read(addr, ret, size);
    return ret;
}

static void xilinx_asu_ecc_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned int size)
{
    XilinxAsuEccState *s = XILINX_ASU_ECC(opaque);

    if (s->reset && addr != A_RESET) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "xilinx-asu-ecc: write to register at offset %"
                      HWADDR_PRIx " while in reset", addr);
        return;
    }

    switch (addr) {
    case A_CTRL:
        write_ctrl(s, val);
        break;

    case A_RESET:
        if (FIELD_EX32(val, RESET, RESET)) {
            do_reset(s);
            update_irq(s);
        } else {
            s->reset = false;
        }
        break;

    case A_ISR:
        clear_irq(s, val);
        break;

    case A_IER:
        enable_irq(s, val);
        break;

    case A_IDR:
        disable_irq(s, val);
        break;

    case A_ITR:
        raise_irq(s, val);
        break;

    case A_CFG:
        s->cfg = val & 0x3;
        break;

    /* ro */
    case A_STATUS:
    case A_IMR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "xilinx-asu-ecc: write to read only register at offset %"
                      HWADDR_PRIx, addr);
        break;

    case A_MEM...A_MEM_LAST:
        if (FIELD_EX32(s->cfg, CFG, WR_ENDIANNESS)) {
            s->mem[(addr - A_MEM) / sizeof(uint32_t)] = le32_to_cpu(val);
        } else {
            s->mem[(addr - A_MEM) / sizeof(uint32_t)] = be32_to_cpu(val);
        }
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "xilinx-asu-ecc: write to unimplemented register at"
                      " offset %" HWADDR_PRIx, addr);
        break;
    }

    trace_xilinx_asu_ecc_write(addr, val, size);
}

static const MemoryRegionOps xilinx_asu_ecc_ops = {
    .read = xilinx_asu_ecc_read,
    .write = xilinx_asu_ecc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void xilinx_asu_ecc_reset_enter(Object *obj, ResetType type)
{
    XilinxAsuEccState *s = XILINX_ASU_ECC(obj);

    do_reset(s);
}

static void xilinx_asu_ecc_reset_hold(Object *obj)
{
    XilinxAsuEccState *s = XILINX_ASU_ECC(obj);

    update_irq(s);
}

static void xilinx_asu_ecc_realize(DeviceState *dev, Error **errp)
{
    XilinxAsuEccState *s = XILINX_ASU_ECC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &xilinx_asu_ecc_ops,
                          s, TYPE_XILINX_ASU_ECC, XILINX_ASU_ECC_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void xilinx_asu_ecc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = xilinx_asu_ecc_realize;
    rc->phases.enter = xilinx_asu_ecc_reset_enter;
    rc->phases.hold = xilinx_asu_ecc_reset_hold;
}

static const TypeInfo xilinx_asu_ecc_info = {
    .name = TYPE_XILINX_ASU_ECC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XilinxAsuEccState),
    .class_init = xilinx_asu_ecc_class_init,
};

static void xilinx_asu_ecc_register_types(void)
{
    type_register_static(&xilinx_asu_ecc_info);
}

type_init(xilinx_asu_ecc_register_types)
