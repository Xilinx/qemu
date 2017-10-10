/*
 * ARM Generic/Distributed Interrupt Controller
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

/* This file contains implementation code for the RealView EB interrupt
 * controller, MPCore distributed interrupt controller and ARMv7-M
 * Nested Vectored Interrupt Controller.
 * It is compiled in two ways:
 *  (1) as a standalone file to produce a sysbus device which is a GIC
 *  that can be used on the realview board and as one of the builtin
 *  private peripherals for the ARM MP CPUs (11MPCore, A9, etc)
 *  (2) by being directly #included into armv7m_nvic.c to produce the
 *  armv7m_nvic device.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "gic_internal.h"
#include "qapi/error.h"
#include "qom/cpu.h"
#include "qemu/log.h"
#include "trace.h"
#include "sysemu/kvm.h"

#include "hw/fdt_generic_util.h"

#include "hw/guest/linux.h"

/* #define DEBUG_GIC */

#ifdef DEBUG_GIC
#define DEBUG_GIC_GATE 1
#else
#define DEBUG_GIC_GATE 0
#endif

#define IDLE_PRIORITY 0xff
#define DPRINTF(fmt, ...) do {                                          \
        if (DEBUG_GIC_GATE) {                                           \
            fprintf(stderr, "%s: " fmt, __func__, ## __VA_ARGS__);      \
        }                                                               \
    } while (0)

static const uint8_t gic_id_11mpcore[] = {
    0x00, 0x00, 0x00, 0x00, 0x90, 0x13, 0x04, 0x00, 0x0d, 0xf0, 0x05, 0xb1
};

static const uint8_t gic_id_gicv1[] = {
    0x04, 0x00, 0x00, 0x00, 0x90, 0xb3, 0x1b, 0x00, 0x0d, 0xf0, 0x05, 0xb1
};

static const uint8_t gic_id_gicv2[] = {
    0x04, 0x00, 0x00, 0x00, 0x90, 0xb4, 0x2b, 0x00, 0x0d, 0xf0, 0x05, 0xb1
};

#define NUM_CPU(s) ((s)->num_cpu)

#define GICH_LRN_STATE_INVALID   0
#define GICH_LRN_STATE_PENDING   1
#define GICH_LRN_STATE_ACTIVE    2

static inline void gic_dump_lrs(GICState *s, const char *prefix)
{
    unsigned int i;
    unsigned int lr;
    unsigned int apr;
    uint32_t lr_comb_state = 0;

    for (i = 0; i < s->num_cpu; i++) {
        qemu_log("%s: CPU%d HCR=%x ", prefix, i, s->gich.hcr[i]);
        for (lr = 0; lr < GICV_NR_LR; lr++) {
            int state = extract32(s->gich.lr[i][lr], 28, 2);
            lr_comb_state |= s->gich.lr[i][lr];
            qemu_log("LR[%d]=%x %c%c ", lr, s->gich.lr[i][lr],
                     state & GICH_LRN_STATE_PENDING ? 'P' : '.',
                     state & GICH_LRN_STATE_ACTIVE ? 'A' : '.');
        }
        for (apr = 0; apr < GIC_NR_APRS; apr++) {
            qemu_log("APR[%d]=%x ", apr, s->apr[apr][i]);
        }
        qemu_log("GICH.APR=%x\n", s->gich.apr[i]);
        if (extract32(lr_comb_state, 28, 2) == 0 && s->gich.apr[i]) {
            qemu_log("BAD! no active LR but GICH.APR!\n");
        }
    }
}

static inline int gic_get_current_cpu(GICState *s)
{
    if (s->num_cpu > 1) {
        return current_cpu->cpu_index % 4;
    }
    return 0;
}

static bool is_apr(GICState *s, unsigned int cpu, unsigned int prio)
{
    int regnum = prio / 32;
    int regbit = prio % 32;

    if (regnum >= ARRAY_SIZE(s->apr)) printf("prio=%d\n", prio);
    assert(regnum < ARRAY_SIZE(s->apr));
    return s->apr[regnum][cpu] & (1 << regbit);
}

static void set_apr(GICState *s, unsigned int cpu, unsigned int prio)
{
    int regnum = prio / 32;
    int regbit = prio % 32;

    if (regnum >= ARRAY_SIZE(s->apr)) printf("prio=%d\n", prio);
    assert(regnum < ARRAY_SIZE(s->apr));
    assert(!is_apr(s, cpu, prio));
    s->apr[regnum][cpu] |= 1 << regbit;
}

static void clear_apr(GICState *s, unsigned int cpu, unsigned int prio)
{
    int regnum = prio / 32;
    int regbit = prio % 32;

    if (regnum >= ARRAY_SIZE(s->apr)) printf("prio=%d\n", prio);
    assert(regnum < ARRAY_SIZE(s->apr));
    assert(is_apr(s, cpu, prio));
    if (s->apr[regnum][cpu] & ((1 << regbit) - 1)) {
        qemu_log("cpu=%d completed APR not lowest! prio=%d\n", cpu, prio);
        gic_dump_lrs(s, "BAD");
    } else {
        int i;
        i = regnum;
        while (--i > 0) {
		if (s->apr[i][cpu]) {
		    qemu_log("cpu=%d completed APR not lowest! %d\n", cpu, prio);
                    gic_dump_lrs(s, "BAD");
		}
	}
    }
    s->apr[regnum][cpu] &= ~(1 << regbit);
}

#define GICH_LRN_STATE_INVALID   0
#define GICH_LRN_STATE_PENDING   1
#define GICH_LRN_STATE_ACTIVE    2
static void gicv_update_cpu(GICState *s, int vcpu)
{
    int cpu = vcpu + GIC_N_REALCPU;
    unsigned int i;
    unsigned int best_prio = 0x100;
    unsigned int best_irq = 1023;
    unsigned int best_lrn = 0;
    unsigned int allstate = 0;
    int nr_valid = 0;
    bool level = 0;
    bool maint_irq = 0;

    if (!(s->gich.hcr[vcpu] & 1)) {
        goto done;
    }

    s->current_pending[cpu] = 1023;
    s->gich.pending_prio[vcpu] = 0x100;
    s->gich.misr[vcpu] = 0;
    s->gich.eisr[vcpu] = 0;
    s->gich.elrsr[vcpu] = 0;

    for (i = 0; i < ARRAY_SIZE(s->gich.lr[vcpu]); i++) {
        unsigned int state;
        unsigned int prio;
        unsigned int vid;
        unsigned int hw;
        unsigned int eoi;

        state = extract32(s->gich.lr[vcpu][i], 28, 2);
        vid = extract32(s->gich.lr[vcpu][i], 0, 10);
        prio = extract32(s->gich.lr[vcpu][i], 23, 6);
        hw = extract32(s->gich.lr[vcpu][i], 31, 1);
        eoi = extract32(s->gich.lr[vcpu][i], 19, 1);

        if (state == 0 && hw == 0 && eoi) {
            s->gich.eisr[vcpu] |= 1ULL << i;
        }

        if (state == 0 && (hw || !eoi)) {
            s->gich.elrsr[vcpu] |= 1ULL << i;
        }

        allstate |= state;

        if (state) {
            nr_valid++;
        }

        if (state != GICH_LRN_STATE_PENDING) {
            continue;
        }

#if 0
        if (s->gich.apr[vcpu] & (1 << (prio >> 3))) {
            continue;
        }
#endif
        if (prio < best_prio) {
            best_prio = prio;
            best_irq = vid;
            best_lrn = i;
        }
    }

    if (best_prio < s->priority_mask[cpu]) {
        /* resignal the irq.  */
        s->current_pending[cpu] = best_irq;
        s->gich.pending_lrn[vcpu] = best_lrn;
        s->gich.pending_prio[vcpu] = best_prio;
        if (best_prio < s->running_priority[cpu]) {
            level = 1;
        }
    }

    s->gich.misr[vcpu] |= s->gich.eisr[vcpu] != 0;
    s->gich.misr[vcpu] |= (nr_valid > 1 ? 0 : 1 << 1) & s->gich.hcr[vcpu];
    s->gich.misr[vcpu] |= ((allstate & 1) << 3) & s->gich.hcr[vcpu];

    level &= s->gich.hcr[vcpu] & 1;
    assert(!(level && !(s->gicc_ctrl[cpu].enable_grp[1])));

    maint_irq = s->gich.misr[vcpu] && (s->gich.hcr[vcpu] & 1);
done:
//    qemu_log("CPU%d virq=%d\n", cpu, level);
    qemu_set_irq(s->parent_irq[cpu], level);
    qemu_set_irq(s->maint[vcpu], maint_irq);
}

static void gicv_update(GICState *s)
{
    unsigned int i;

    for (i = 0; i < s->num_cpu; i++) {
        gicv_update_cpu(s, i);
    }
}

/* TODO: Many places that call this routine could be optimized.  */
/* Update interrupt status after enabled or pending bits have been changed.  */
void gic_update(GICState *s)
{
    int best_irq;
    int best_prio;
    int irq;
    int level;
    int cpu;
    int cm;

    for (cpu = 0; cpu < NUM_CPU(s); cpu++) {
        bool cpu_irq = false;
        bool cpu_fiq = false;
        bool next_grp0;

        cm = 1 << cpu;
        s->current_pending[cpu] = 1023;
        best_prio = 0x100;
        best_irq = 1023;
        for (irq = 0; irq < s->num_irq; irq++) {
            if (GIC_TEST_ENABLED(irq, cm) && gic_test_pending(s, irq, cm) &&
                (irq < GIC_INTERNAL || GIC_TARGET(irq) & cm)) {
                if (GIC_GET_PRIORITY(irq, cpu) < best_prio &&
                    !is_apr(s, cpu, GIC_GET_PRIORITY(irq, cpu))) {
                    best_prio = GIC_GET_PRIORITY(irq, cpu);
                    best_irq = irq;
                }
            }
        }
        level = 0;
        if (best_prio < s->priority_mask[cpu]) {
            s->current_pending[cpu] = best_irq;
            if (best_prio < s->running_priority[cpu]) {
                DPRINTF("Raised pending IRQ %d (cpu %d)\n", best_irq, cpu);
                level = 1;
            }
        }

        next_grp0 = GIC_GROUP(best_irq) == 0;
        if (level) {
            if (next_grp0 && s->gicc_ctrl[cpu].fiq_en) {
                if (s->gicc_ctrl[cpu].enable_grp[0]) {
                    cpu_fiq = true;
                }
            } else {
                if ((next_grp0 && s->gicc_ctrl[cpu].enable_grp[0])
                    || (!next_grp0 && s->gicc_ctrl[cpu].enable_grp[1])) {
                    cpu_irq = true;
                }
            }
        }
        qemu_set_irq(s->parent_fiq[cpu], cpu_fiq);
        qemu_set_irq(s->parent_irq[cpu], cpu_irq);
    }
    gicv_update(s);
}

void gic_set_pending_private(GICState *s, int cpu, int irq)
{
    int cm = 1 << cpu;

    if (gic_test_pending(s, irq, cm)) {
        return;
    }

    DPRINTF("Set %d pending cpu %d\n", irq, cpu);
    GIC_SET_PENDING(irq, cm);
    gic_update(s);
}

static void gic_set_irq_11mpcore(GICState *s, int irq, int level,
                                 int cm, int target)
{
    if (level) {
        GIC_SET_LEVEL(irq, cm);
        if (GIC_TEST_EDGE_TRIGGER(irq) || GIC_TEST_ENABLED(irq, cm)) {
            DPRINTF("Set %d pending mask %x\n", irq, target);
            GIC_SET_PENDING(irq, target);
        }
    } else {
        GIC_CLEAR_LEVEL(irq, cm);
    }
}

static void gic_set_irq_generic(GICState *s, int irq, int level,
                                int cm, int target)
{
    if (level) {
        GIC_SET_LEVEL(irq, cm);
        DPRINTF("Set %d pending mask %x\n", irq, target);
        if (GIC_TEST_EDGE_TRIGGER(irq)) {
            GIC_SET_PENDING(irq, target);
        }
    } else {
        GIC_CLEAR_LEVEL(irq, cm);
    }
}

/* Process a change in an external IRQ input.  */
static void gic_set_irq(void *opaque, int irq, int level)
{
    /* Meaning of the 'irq' parameter:
     *  [0..N-1] : external interrupts
     *  [N..N+31] : PPI (internal) interrupts for CPU 0
     *  [N+32..N+63] : PPI (internal interrupts for CPU 1
     *  ...
     */
    GICState *s = (GICState *)opaque;
    int cm, target;
    if (irq < (s->num_irq - GIC_INTERNAL)) {
        /* The first external input line is internal interrupt 32.  */
        cm = ALL_CPU_MASK;
        irq += GIC_INTERNAL;
        target = GIC_TARGET(irq);
    } else {
        int cpu;
        irq -= (s->num_irq - GIC_INTERNAL);
        cpu = irq / GIC_INTERNAL;
        irq %= GIC_INTERNAL;
        cm = 1 << cpu;
        target = cm;
    }

    assert(irq >= GIC_NR_SGIS);

    if (level == GIC_TEST_LEVEL(irq, cm)) {
        return;
    }

    if (s->revision == REV_11MPCORE) {
        gic_set_irq_11mpcore(s, irq, level, cm, target);
    } else {
        gic_set_irq_generic(s, irq, level, cm, target);
    }

    gic_update(s);
}

static void gic_set_irq_cb(void *opaque, int irq, int level)
{
    ARMGICClass *agc = ARM_GIC_GET_CLASS((Object *)opaque);

    agc->irq_handler(opaque, irq, level);
}

static void gic_set_running_irq(GICState *s, int cpu, int irq)
{
    s->running_irq[cpu] = irq;
    if (irq == 1023) {
        s->running_priority[cpu] = 0x100;
    } else {
        s->running_priority[cpu] = GIC_GET_PRIORITY(irq, cpu);
    }
    gic_update(s);
}

static uint32_t gic_acknowledge_virq(GICState *s, int cpu)
{
    uint32_t t, cpuid = 0;
    int vcpu = cpu - GIC_N_REALCPU;
    bool hw;

    if (s->gich.pending_prio[vcpu] == 0x100) {
        return 1023;
    }
    s->running_priority[cpu] = s->gich.pending_prio[vcpu];
    s->running_irq[cpu] = s->current_pending[cpu];
    /* Clear the pending bit.  */
    t = s->gich.lr[vcpu][s->gich.pending_lrn[vcpu]];
    s->gich.lr[vcpu][s->gich.pending_lrn[vcpu]] = deposit32(t, 28, 2, 2);

    hw = extract32(t, 31, 1);
    if (!hw) {
        cpuid = extract32(t, 10, 3);
    }

    gicv_update(s);

    s->gich.apr[vcpu] |= 1 << (s->running_priority[cpu] >> 3);
#if 0
    if (s->running_irq[cpu] == 27) {
    qemu_log("Vack CPU%d %d\n", cpu, s->running_irq[cpu]);
    gic_dump_lrs(s, "Vack");
    }
#endif
    return s->running_irq[cpu] | (cpuid << 10);
}

uint32_t gic_acknowledge_irq(GICState *s, int cpu, bool secure)
{
    int ret, irq, src;
    int cm = 1 << cpu;
    bool is_grp0;

    irq = s->current_pending[cpu];
    is_grp0 = GIC_GROUP(irq) == 0;

    if (irq == 1023
            || GIC_GET_PRIORITY(irq, cpu) >= s->running_priority[cpu]) {
        DPRINTF("ACK no pending IRQ\n");
        return 1023;
    }

    if ((is_grp0 && !s->gicc_ctrl[cpu].enable_grp[0])
        || (!is_grp0 && !s->gicc_ctrl[cpu].enable_grp[1])
        || (is_grp0 && !secure)) {
        return 1023;
    }

    if (!is_grp0 && secure && !s->gicc_ctrl[cpu].ack_ctl) {
        return 1022;
    }

    s->last_active[irq][cpu] = s->running_irq[cpu];

    if (s->revision == REV_11MPCORE) {
        /* Clear pending flags for both level and edge triggered interrupts.
         * Level triggered IRQs will be reasserted once they become inactive.
         */
        GIC_CLEAR_PENDING(irq, GIC_TEST_MODEL(irq) ? ALL_CPU_MASK : cm);
        ret = irq;
    } else {
        if (irq < GIC_NR_SGIS) {
            /* Lookup the source CPU for the SGI and clear this in the
             * sgi_pending map.  Return the src and clear the overall pending
             * state on this CPU if the SGI is not pending from any CPUs.
             */
            assert(s->sgi_pending[irq][cpu] != 0);
            src = ctz32(s->sgi_pending[irq][cpu]);
            s->sgi_pending[irq][cpu] &= ~(1 << src);
            if (s->sgi_pending[irq][cpu] == 0) {
                GIC_CLEAR_PENDING(irq, GIC_TEST_MODEL(irq) ? ALL_CPU_MASK : cm);
            }
            ret = irq | ((src & 0x7) << 10);
        } else {
            /* Clear pending state for both level and edge triggered
             * interrupts. (level triggered interrupts with an active line
             * remain pending, see gic_test_pending)
             */
            GIC_CLEAR_PENDING(irq, GIC_TEST_MODEL(irq) ? ALL_CPU_MASK : cm);
            ret = irq;
        }
    }

    gic_set_running_irq(s, cpu, irq);
    set_apr(s, cpu, s->running_priority[cpu]);
#if 0
    if (irq == 27) {
    qemu_log("ack CPU%d %d\n", cpu, irq);
//    gic_dump_lrs(s, "ack");
    }
    DPRINTF("ACK %d\n", irq);
#endif
    return ret;
}

void gic_set_priority(GICState *s, int cpu, int irq, uint8_t val)
{
    if (irq < GIC_INTERNAL) {
        s->priority1[irq][cpu] = val;
    } else {
        s->priority2[(irq) - GIC_INTERNAL] = val;
    }
}

static void gic_complete_irq_force(GICState *s, int cpu, int irq, bool force, bool sec)
{
    int update = 0;
    int cm = 1 << cpu;
    bool eoirmode = s->gicc_ctrl[cpu].eoirmode_ns;

    if (sec) {
        eoirmode = s->gicc_ctrl[cpu].eoirmode;
    }

    if (force) {
        eoirmode = false;
    }

    if (irq >= s->num_irq) {
        /* This handles two cases:
         * 1. If software writes the ID of a spurious interrupt [ie 1023]
         * to the GICC_EOIR, the GIC ignores that write.
         * 2. If software writes the number of a non-existent interrupt
         * this must be a subcase of "value written does not match the last
         * valid interrupt value read from the Interrupt Acknowledge
         * register" and so this is UNPREDICTABLE. We choose to ignore it.
         */
        return;
    }
    if (s->running_irq[cpu] == 1023) {
        int i;
        for (i = 0; i < GIC_NR_APRS; i++) {
            assert(s->apr[i][cpu] == 0);
        }
        return; /* No active IRQ.  */
    }

    if (eoirmode) {
        gic_update(s);
        return;
    }

    if (s->revision == REV_11MPCORE) {
        /* Mark level triggered interrupts as pending if they are still
           raised.  */
        if (!GIC_TEST_EDGE_TRIGGER(irq) && GIC_TEST_ENABLED(irq, cm)
            && GIC_TEST_LEVEL(irq, cm) && (GIC_TARGET(irq) & cm) != 0) {
            DPRINTF("Set %d pending mask %x\n", irq, cm);
            GIC_SET_PENDING(irq, cm);
            update = 1;
        }
    }

#if 0
    if (irq == 27) {
        qemu_log("HW DIR irq=%d\n", irq);
    }
#endif

    if (irq != s->running_irq[cpu]) {
        /* Complete an IRQ that is not currently running.  */
        int tmp = s->running_irq[cpu];
        if (irq == 27)
        qemu_log("compl not running irq=%d running=%d %x %x %x\n", irq, s->running_irq[cpu],
		s->apr[0][cpu], s->apr[1][cpu], s->apr[2][cpu]);
        while (s->last_active[tmp][cpu] != 1023) {
            if (s->last_active[tmp][cpu] == irq) {
                if (is_apr(s, cpu, GIC_GET_PRIORITY(s->last_active[tmp][cpu], cpu)))
	            clear_apr(s, cpu, GIC_GET_PRIORITY(s->last_active[tmp][cpu], cpu));
                s->last_active[tmp][cpu] = s->last_active[irq][cpu];
                break;
            }
            tmp = s->last_active[tmp][cpu];
        }
        if (update) {
            gic_update(s);
        }
    } else {
        /* Complete the current running IRQ.  */
        clear_apr(s, cpu, s->running_priority[cpu]);
        gic_set_running_irq(s, cpu, s->last_active[s->running_irq[cpu]][cpu]);
    }
    if (irq == 27 && s->running_irq[cpu] == 27) {
        qemu_log("BAD: DIR irq=%d running IRQ=%d\n", irq, s->running_irq[cpu]);
    }
}

void gic_complete_irq(GICState *s, int cpu, int irq, bool secure)
{
    gic_complete_irq_force(s, cpu, irq, false, secure);
#if 0
    if (irq == 27) {
    qemu_log("complete CPU%d %d\n", cpu, irq);
//    gic_dump_lrs(s, "complete");
    }
#endif
}

static void gic_complete_virq(GICState *s, int cpu, int irq)
{
    int vcpu = cpu - GIC_N_REALCPU;
    bool eoirmode = s->gicc_ctrl[cpu].eoirmode_ns;
    unsigned int i;
    unsigned int state;
    unsigned int vid;
    unsigned int pid;
    bool hw;
    bool eoi;

    for (i = 0; i < ARRAY_SIZE(s->gich.lr[vcpu]); i++) {
        state = extract32(s->gich.lr[vcpu][i], 28, 2);
        if (state == GICH_LRN_STATE_INVALID) {
            continue;
        }

        vid = extract32(s->gich.lr[vcpu][i], 0, 10);
        pid = extract32(s->gich.lr[vcpu][i], 10, 10);
        eoi = extract32(s->gich.lr[vcpu][i], 19, 1);
        hw = extract32(s->gich.lr[vcpu][i], 31, 1);
        if (vid == irq) {
            break;
        }
    }

    if (i == ARRAY_SIZE(s->gich.lr[vcpu])) {
        qemu_log("%s:%d BAD?\n", __func__, __LINE__);
        return;
    }

    if (!hw && eoi) {
        qemu_log("EOI! maint! %d\n", irq);
        s->gich.eisr[vcpu] |= 1ULL << irq;
    }

    if (eoirmode == 0) {
        /* Clear the active bit.  */
        s->gich.lr[vcpu][i] = deposit32(s->gich.lr[vcpu][i], 29, 1, 0);
        if (hw) {
            /* Deactive the physical IRQ.  */
            gic_complete_irq_force(s, vcpu, pid, true, false);
        }
    } else {
        qemu_log_mask(LOG_UNIMP, "gic: unimplemted CTLR.EOIRMODE = 1\n");
    }
    s->gich.apr[vcpu] &= ~(1 << (s->running_priority[cpu] >> 3));
    s->running_priority[cpu] = 0x100;
    s->running_irq[cpu] = 1023;
#if 0
    if (irq == 27) {
    qemu_log("Vcomplete CPU%d %d\n", cpu, irq);
    gic_dump_lrs(s, "Vcomplete");
    }
#endif
}

static uint32_t gic_dist_readb(void *opaque, hwaddr offset, bool secure)
{
    GICState *s = (GICState *)opaque;
    uint32_t res;
    int irq;
    int i;
    int cpu;
    int cm;
    int mask;

    cpu = gic_get_current_cpu(s);
    cm = 1 << cpu;
    if (offset < 0x100) {
        if (offset == 0) {
            if (secure) {
                return (s->enabled << 1) | s->enabled_grp0;
            } else {
                return s->enabled;
            }
        }
        if (offset == 4)
            return ((s->num_irq / 32) - 1) | ((NUM_CPU(s) - 1) << 5);
        if (offset < 0x08)
            return 0;
        if (offset >= 0x80) {
            if (secure && s->revision >= 2) {
                unsigned int irq = (offset - 0x80) * 8;
                res = 0;
                for (i = 0; i < 8; i++) {
                    res |= s->irq_state[irq + i].group << i;
                }
                return res;
            }
            /* Interrupt Security , RAZ/WI */
            return 0;
        }
        goto bad_reg;
    } else if (offset < 0x200) {
        /* Interrupt Set/Clear Enable.  */
        if (offset < 0x180)
            irq = (offset - 0x100) * 8;
        else
            irq = (offset - 0x180) * 8;
        irq += GIC_BASE_IRQ;
        if (irq >= s->num_irq)
            goto bad_reg;
        res = 0;
        for (i = 0; i < 8; i++) {
            if (GIC_TEST_ENABLED(irq + i, cm)) {
                res |= (1 << i);
            }
        }
    } else if (offset < 0x300) {
        /* Interrupt Set/Clear Pending.  */
        if (offset < 0x280)
            irq = (offset - 0x200) * 8;
        else
            irq = (offset - 0x280) * 8;
        irq += GIC_BASE_IRQ;
        if (irq >= s->num_irq)
            goto bad_reg;
        res = 0;
        mask = (irq < GIC_INTERNAL) ?  cm : ALL_CPU_MASK;
        for (i = 0; i < 8; i++) {
            if (gic_test_pending(s, irq + i, mask)) {
                res |= (1 << i);
            }
        }
    } else if (offset < 0x400) {
        /* Interrupt Active.  */
        irq = (offset - 0x300) * 8 + GIC_BASE_IRQ;
        if (irq >= s->num_irq)
            goto bad_reg;
        res = 0;
        mask = (irq < GIC_INTERNAL) ?  cm : ALL_CPU_MASK;
        for (i = 0; i < 8; i++) {
            if (GIC_TEST_ACTIVE(irq + i, mask)) {
                res |= (1 << i);
            }
        }
    } else if (offset < 0x800) {
        /* Interrupt Priority.  */
        irq = (offset - 0x400) + GIC_BASE_IRQ;
        if (irq >= s->num_irq)
            goto bad_reg;
        res = GIC_GET_PRIORITY(irq, cpu);
    } else if (offset < 0xc00) {
        /* Interrupt CPU Target.  */
        if (s->num_cpu == 1 && s->revision != REV_11MPCORE) {
            /* For uniprocessor GICs these RAZ/WI */
            res = 0;
        } else {
            irq = (offset - 0x800) + GIC_BASE_IRQ;
            if (irq >= s->num_irq) {
                goto bad_reg;
            }
            if (irq < GIC_INTERNAL) {
                res = cm;
            } else {
                res = GIC_TARGET(irq);
            }
        }
    } else if (offset < 0xf00) {
        /* Interrupt Configuration.  */
        irq = (offset - 0xc00) * 4 + GIC_BASE_IRQ;
        if (irq >= s->num_irq)
            goto bad_reg;
        res = 0;
        for (i = 0; i < 4; i++) {
            if (GIC_TEST_MODEL(irq + i))
                res |= (1 << (i * 2));
            if (GIC_TEST_EDGE_TRIGGER(irq + i))
                res |= (2 << (i * 2));
        }
    } else if (offset < 0xf10) {
        goto bad_reg;
    } else if (offset < 0xf30) {
        if (s->revision == REV_11MPCORE) {
            goto bad_reg;
        }

        if (offset < 0xf20) {
            /* GICD_CPENDSGIRn */
            irq = (offset - 0xf10);
        } else {
            irq = (offset - 0xf20);
            /* GICD_SPENDSGIRn */
        }

        res = s->sgi_pending[irq][cpu];
    } else if (offset < 0xfd0) {
        goto bad_reg;
    } else if (offset < 0x1000) {
        if (offset & 3) {
            res = 0;
        } else {
            switch (s->revision) {
            case REV_11MPCORE:
                res = gic_id_11mpcore[(offset - 0xfd0) >> 2];
                break;
            case 1:
                res = gic_id_gicv1[(offset - 0xfd0) >> 2];
                break;
            case 2:
                res = gic_id_gicv2[(offset - 0xfd0) >> 2];
                break;
            default:
                res = 0;
            }
        }
    } else {
        g_assert_not_reached();
    }
    return res;
bad_reg:
    qemu_log_mask(LOG_GUEST_ERROR,
                  "gic_dist_readb: Bad offset %x\n", (int)offset);
    return 0;
}

static uint32_t gic_dist_readw(void *opaque, hwaddr offset, bool secure)
{
    uint32_t val;
    val = gic_dist_readb(opaque, offset, secure);
    val |= gic_dist_readb(opaque, offset + 1, secure) << 8;
    return val;
}

static uint32_t gic_dist_readl(void *opaque, hwaddr offset, bool secure)
{
    uint32_t val;
    val = gic_dist_readw(opaque, offset, secure);
    val |= gic_dist_readw(opaque, offset + 2, secure) << 16;
    return val;
}

static void gic_dist_writeb(void *opaque, hwaddr offset,
                            uint32_t value, bool secure)
{
    GICState *s = (GICState *)opaque;
    int irq;
    int i;
    int cpu;

    cpu = gic_get_current_cpu(s);
    if (offset < 0x100) {
        if (offset == 0) {
            if (!secure) {
                s->enabled = (value & 1);
            } else {
                s->enabled = (value & 2);
                s->enabled_grp0 = (value & 1);
            }
            DPRINTF("Distribution %sabled\n", s->enabled ? "En" : "Dis");
        } else if (offset < 4) {
            /* ignored.  */
        } else if (offset >= 0x80) {
            if (secure && s->revision >= 2) {
                unsigned int irq = (offset - 0x80) * 8;
                for (i = 0; i < 8; i++) {
                    s->irq_state[irq + i].group = value & (1 << i);
                }
            }
        } else {
            goto bad_reg;
        }
    } else if (offset < 0x180) {
        /* Interrupt Set Enable.  */
        irq = (offset - 0x100) * 8 + GIC_BASE_IRQ;
        if (irq >= s->num_irq)
            goto bad_reg;
        if (irq < GIC_NR_SGIS) {
            value = 0xff;
        }

        for (i = 0; i < 8; i++) {
            if (value & (1 << i)) {
                int mask =
                    (irq < GIC_INTERNAL) ? (1 << cpu) : GIC_TARGET(irq + i);
                int cm = (irq < GIC_INTERNAL) ? (1 << cpu) : ALL_CPU_MASK;

                if (!s->irq_state[irq + i].group && !secure) {
                    continue;
                }

                if (!GIC_TEST_ENABLED(irq + i, cm)) {
                    DPRINTF("Enabled IRQ %d\n", irq + i);
                }
                GIC_SET_ENABLED(irq + i, cm);
                /* If a raised level triggered IRQ enabled then mark
                   is as pending.  */
                if (GIC_TEST_LEVEL(irq + i, mask)
                        && !GIC_TEST_EDGE_TRIGGER(irq + i)) {
                    DPRINTF("Set %d pending mask %x\n", irq + i, mask);
                    GIC_SET_PENDING(irq + i, mask);
                }
            }
        }
    } else if (offset < 0x200) {
        /* Interrupt Clear Enable.  */
        irq = (offset - 0x180) * 8 + GIC_BASE_IRQ;
        if (irq >= s->num_irq)
            goto bad_reg;
        if (irq < GIC_NR_SGIS) {
            value = 0;
        }

        for (i = 0; i < 8; i++) {
            if (value & (1 << i)) {
                int cm = (irq < GIC_INTERNAL) ? (1 << cpu) : ALL_CPU_MASK;

                if (!s->irq_state[irq + i].group && !secure) {
                    continue;
                }

                if (GIC_TEST_ENABLED(irq + i, cm)) {
                    DPRINTF("Disabled IRQ %d\n", irq + i);
                }
                GIC_CLEAR_ENABLED(irq + i, cm);
            }
        }
    } else if (offset < 0x280) {
        /* Interrupt Set Pending.  */
        irq = (offset - 0x200) * 8 + GIC_BASE_IRQ;
        qemu_log("pend irq=%d\n", irq);
        if (irq >= s->num_irq)
            goto bad_reg;
        if (irq < GIC_NR_SGIS) {
            value = 0;
        }

        for (i = 0; i < 8; i++) {
            if (value & (1 << i)) {
                GIC_SET_PENDING(irq + i, GIC_TARGET(irq + i));
            }
        }
    } else if (offset < 0x300) {
        /* Interrupt Clear Pending.  */
        irq = (offset - 0x280) * 8 + GIC_BASE_IRQ;
        if (irq >= s->num_irq)
            goto bad_reg;
        if (irq < GIC_NR_SGIS) {
            value = 0;
        }

        for (i = 0; i < 8; i++) {
            /* ??? This currently clears the pending bit for all CPUs, even
               for per-CPU interrupts.  It's unclear whether this is the
               corect behavior.  */
            if (value & (1 << i)) {
                GIC_CLEAR_PENDING(irq + i, ALL_CPU_MASK);
            }
        }
    } else if (offset < 0x400) {
        /* Interrupt Active.  */
        goto bad_reg;
    } else if (offset < 0x800) {
        /* Interrupt Priority.  */
        irq = (offset - 0x400) + GIC_BASE_IRQ;
        if (irq >= s->num_irq)
            goto bad_reg;
        if (s->irq_state[irq].group || secure) {
            gic_set_priority(s, cpu, irq, value);
        }
    } else if (offset < 0xc00) {
        /* Interrupt CPU Target. RAZ/WI on uniprocessor GICs, with the
         * annoying exception of the 11MPCore's GIC.
         */
        if (s->num_cpu != 1 || s->revision == REV_11MPCORE) {
            irq = (offset - 0x800) + GIC_BASE_IRQ;
            if (irq >= s->num_irq) {
                goto bad_reg;
            }
            if (irq < 29) {
                value = 0;
            } else if (irq < GIC_INTERNAL) {
                value = ALL_CPU_MASK;
            }
            if (s->irq_state[irq].group || secure) {
                s->irq_target[irq] = value & ALL_CPU_MASK;
            }
        }
    } else if (offset < 0xf00) {
        /* Interrupt Configuration.  */
        irq = (offset - 0xc00) * 4 + GIC_BASE_IRQ;
        if (irq >= s->num_irq)
            goto bad_reg;
        if (irq < GIC_NR_SGIS)
            value |= 0xaa;
        for (i = 0; i < 4; i++) {
            /* FIXME: This */
            // if (s->security_extn && !attrs.secure &&
            //     !GIC_TEST_GROUP(irq + i, 1 << cpu)) {
            //     continue; /* Ignore Non-secure access of Group0 IRQ */
            // }

            if (s->revision == REV_11MPCORE) {
                if (value & (1 << (i * 2))) {
                    GIC_SET_MODEL(irq + i);
                } else {
                    GIC_CLEAR_MODEL(irq + i);
                }
            }
            if (value & (2 << (i * 2))) {
                GIC_SET_EDGE_TRIGGER(irq + i);
            } else {
                GIC_CLEAR_EDGE_TRIGGER(irq + i);
            }
        }
    } else if (offset < 0xf10) {
        /* 0xf00 is only handled for 32-bit writes.  */
        goto bad_reg;
    } else if (offset < 0xf20) {
        /* GICD_CPENDSGIRn */
        if (s->revision == REV_11MPCORE) {
            goto bad_reg;
        }
        irq = (offset - 0xf10);

        s->sgi_pending[irq][cpu] &= ~value;
        if (s->sgi_pending[irq][cpu] == 0) {
            GIC_CLEAR_PENDING(irq, 1 << cpu);
        }
    } else if (offset < 0xf30) {
        /* GICD_SPENDSGIRn */
        if (s->revision == REV_11MPCORE) {
            goto bad_reg;
        }
        irq = (offset - 0xf20);

        GIC_SET_PENDING(irq, 1 << cpu);
        s->sgi_pending[irq][cpu] |= value;
    } else {
        goto bad_reg;
    }
    gic_update(s);
    return;
bad_reg:
    qemu_log_mask(LOG_GUEST_ERROR,
                  "gic_dist_writeb: Bad offset %x\n", (int)offset);
}

static void gic_dist_writew(void *opaque, hwaddr offset,
                            uint32_t value, bool secure)
{
    gic_dist_writeb(opaque, offset, value & 0xff, secure);
    gic_dist_writeb(opaque, offset + 1, value >> 8, secure);
}

static void gic_dist_writel(void *opaque, hwaddr offset,
                            uint32_t value, bool secure)
{
    GICState *s = (GICState *)opaque;
    if (offset == 0xf00) {
        int cpu;
        int irq;
        int mask;
        int target_cpu;

        cpu = gic_get_current_cpu(s);
        irq = value & 0x3ff;
        switch ((value >> 24) & 3) {
        case 0:
            mask = (value >> 16) & ALL_CPU_MASK;
            break;
        case 1:
            mask = ALL_CPU_MASK ^ (1 << cpu);
            break;
        case 2:
            mask = 1 << cpu;
            break;
        default:
            DPRINTF("Bad Soft Int target filter\n");
            mask = ALL_CPU_MASK;
            break;
        }
        GIC_SET_PENDING(irq, mask);
        target_cpu = ctz32(mask);
        while (target_cpu < GIC_N_REALCPU) {
            s->sgi_pending[irq][target_cpu] |= (1 << cpu);
            mask &= ~(1 << target_cpu);
            target_cpu = ctz32(mask);
        }
        gic_update(s);
        return;
    }
    gic_dist_writew(opaque, offset, value & 0xffff, secure);
    gic_dist_writew(opaque, offset + 2, value >> 16, secure);
}

static void gic_dist_access(MemoryTransaction *tr)
{
    bool sec = tr->attr.secure;
    if (tr->rw) {
        switch (tr->size) {
        case 1:
            gic_dist_writeb(tr->opaque, tr->addr, tr->data.u8, sec);
            break;
        case 2:
            gic_dist_writew(tr->opaque, tr->addr, tr->data.u16, sec);
            break;
        case 4:
            gic_dist_writel(tr->opaque, tr->addr, tr->data.u32, sec);
            break;
        }
    } else {
        switch (tr->size) {
        case 1:
            tr->data.u8 = gic_dist_readb(tr->opaque, tr->addr, sec);
            break;
        case 2:
            tr->data.u16 = gic_dist_readw(tr->opaque, tr->addr, sec);
            break;
        case 4:
            tr->data.u32 = gic_dist_readl(tr->opaque, tr->addr, sec);
            break;
        }
    }
#if 0
    qemu_log("GIC sz=%d rw=%d addr=%lx data32=%x secure=%d\n",
             tr->size, tr->rw, tr->addr, tr->data.u32, sec);
#endif
}

static const MemoryRegionOps gic_dist_ops = {
#if 1
    .access = gic_dist_access,
#else
    .old_mmio = {
        .read = { gic_dist_readb, gic_dist_readw, gic_dist_readl, },
        .write = { gic_dist_writeb, gic_dist_writew, gic_dist_writel, },
    },
#endif
    .endianness = DEVICE_NATIVE_ENDIAN,
};

#define GICC_ACK_CTL       (1 << 2)
#define GICC_FIQ_EN        (1 << 3)
#define GICC_EOIRMODE      (1 << 9)
#define GICC_EOIRMODE_NS   (1 << 10)
static uint32_t gicc_encode_ctrl(GICState *s, int cpu, bool secure)
{
    uint32_t r;

    if (secure) {
        r = s->gicc_ctrl[cpu].enable_grp[0];
        r |= s->gicc_ctrl[cpu].enable_grp[1] << 1;
        r |= s->gicc_ctrl[cpu].ack_ctl << 2;
        r |= s->gicc_ctrl[cpu].fiq_en << 3;
        r |= s->gicc_ctrl[cpu].eoirmode << 9;
        r |= s->gicc_ctrl[cpu].eoirmode_ns << 10;
    } else {
        r = s->gicc_ctrl[cpu].enable_grp[1];
        r |= s->gicc_ctrl[cpu].eoirmode_ns << 9;
    }
    return r;
}

static void gicc_decode_ctrl(GICState *s, int cpu, bool secure, uint32_t v)
{
    if (secure) {
        s->gicc_ctrl[cpu].enable_grp[0] = v & 1;
        s->gicc_ctrl[cpu].enable_grp[1] = v & 2;
        s->gicc_ctrl[cpu].ack_ctl = v & GICC_ACK_CTL;
        s->gicc_ctrl[cpu].fiq_en = v & GICC_FIQ_EN;
        s->gicc_ctrl[cpu].eoirmode = v & GICC_EOIRMODE;
        s->gicc_ctrl[cpu].eoirmode_ns = v & GICC_EOIRMODE_NS;
    } else {
        s->gicc_ctrl[cpu].enable_grp[1] = v & 1;
        s->gicc_ctrl[cpu].eoirmode_ns = v & GICC_EOIRMODE;
    }
}

static uint32_t gic_cpu_read(GICState *s, int cpu, int offset, bool secure)
{
    bool virt = cpu >= GIC_N_REALCPU;

    switch (offset) {
    case 0x00: /* Control */
        return gicc_encode_ctrl(s, cpu, secure);
    case 0x04: /* Priority mask */
        return s->priority_mask[cpu];
    case 0x08: /* Binary Point */
        return s->bpr[cpu];
    case 0x0c: /* Acknowledge */
        if (virt) {
            return gic_acknowledge_virq(s, cpu);
        } else {
            return gic_acknowledge_irq(s, cpu, secure);
        }
    case 0x14: /* Running Priority */
        return s->running_priority[cpu] == 0x100 ?
               IDLE_PRIORITY : s->running_priority[cpu];
    case 0x18: /* Highest Pending Interrupt */
        return s->current_pending[cpu];
    case 0x1c: /* Aliased Binary Point */
        return s->abpr[cpu];
    case 0x20: /* AIAR */
        qemu_log_mask(LOG_UNIMP, "unsupported AIAR\n");
        return 0;
    case 0xd0: case 0xd4: case 0xd8: case 0xdc:
        return s->apr[(offset - 0xd0) / 4][cpu];
    case 0xFC:
        return s->c_iidr;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "gic_cpu_read: Bad offset %x\n", (int)offset);
        return 0;
    }
}

static void gic_cpu_write(GICState *s, int cpu, int offset, uint32_t value, bool secure)
{
    bool virt = cpu >= GIC_N_REALCPU;

    switch (offset) {
    case 0x00: /* Control */
        gicc_decode_ctrl(s, cpu, secure, value);
        s->ctrl[cpu] = gicc_encode_ctrl(s, cpu, true);
        break;
    case 0x04: /* Priority mask */
        s->priority_mask[cpu] = (value & 0xff);
        break;
    case 0x08: /* Binary Point */
        s->bpr[cpu] = (value & 0x7);
        break;
    case 0x10: /* End Of Interrupt */
        if (virt) {
            gic_complete_virq(s, cpu, value & 0x3ff);
        } else {
            return gic_complete_irq(s, cpu, value & 0x3ff, secure);
        }
        break;
    case 0x1c: /* Aliased Binary Point */
        if (s->revision >= 2) {
            s->abpr[cpu] = (value & 0x7);
        }
        break;
    case 0xd0: case 0xd4: case 0xd8: case 0xdc:
        s->apr[(offset - 0xd0) / 4][cpu] = value;
        qemu_log_mask(LOG_UNIMP, "Writing APR not implemented\n");
        break;
    case 0x1000:
    case 0x10000:
        if (offset != s->map_stride) {
            qemu_log("Bad write to GIC 0x%x: Wrong GIC map stride?\n", offset);
        }
        if (virt) {
            qemu_log_mask(LOG_UNIMP, "Writing GICV_DIR not implemented\n");
        } else {
            return gic_complete_irq_force(s, cpu, value & 0x3ff, true, secure);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "gic_cpu_write: Bad offset %x\n", (int)offset);
        return;
    }
    gic_update(s);
}

static void thiscpu_access(MemoryTransaction *tr)
{
    GICState *s = (GICState *) tr->opaque;
    bool sec = tr->attr.secure;

    if (tr->rw) {
        gic_cpu_write(s, gic_get_current_cpu(s), tr->addr, tr->data.u32, sec);
    } else {
        tr->data.u32 = gic_cpu_read(s, gic_get_current_cpu(s), tr->addr, sec);
    }
}

static uint32_t gic_hyp_vmcr_read(GICState *s, int vcpu)
{
    int cpu = vcpu + GIC_N_REALCPU;
    uint32_t r;
    uint32_t ctrl;

    r = extract32(s->priority_mask[cpu], 3, 5) << 27;
    r |= extract32(s->bpr[cpu], 0, 3) << 21;
    r |= extract32(s->abpr[cpu], 0, 3) << 18;

    ctrl = gicc_encode_ctrl(s, cpu, false);
    r |= extract32(ctrl, 0, 10);
    return r;
}

static void gic_hyp_vmcr_write(GICState *s, int vcpu, uint32_t value)
{
    int cpu = vcpu + GIC_N_REALCPU;
    uint32_t primask = extract32(value, 27, 5);
    uint32_t bpr = extract32(value, 21, 3);
    uint32_t abpr = extract32(value, 18, 3);
    uint32_t ctrl = extract32(value, 0, 10);

    s->priority_mask[cpu] = primask << 3;
    s->bpr[cpu] = bpr;
    s->abpr[cpu] = abpr;
    gicc_decode_ctrl(s, cpu, false, ctrl);
}

static uint32_t gic_hyp_read(GICState *s, int vcpu, int offset)
{
    uint32_t r = 0;

    switch (offset) {
    case 0x00: /* HCR */
        r = s->gich.hcr[vcpu];
        break;
    case 0x04: /* VTR */
        /* 5 prio bits, 5 preempt bits and nr list regs.  */
        r = 5 << 29 | 5 << 26 | (GICV_NR_LR  - 1);
        break;
    case 0x08: /* VMCR */
        r = gic_hyp_vmcr_read(s, vcpu);
        break;
    case 0x10: /* MISR */
        r = s->gich.misr[vcpu];
        break;
    case 0x20: /* EISR0 */
        r = s->gich.eisr[vcpu] & 0xffffffff;
        qemu_log("eisr0=%x\n", r);
        break;
    case 0x24: /* EISR1 */
        r = s->gich.eisr[vcpu] >> 32;
        qemu_log("eisr1=%x\n", r);
        break;
    case 0x30: /* ELRSR0 */
        r = s->gich.elrsr[vcpu] & 0xffffffff;
        qemu_log("elrsr0=%x\n", r);
        break;
    case 0x34: /* ELRSR1 */
        r = s->gich.elrsr[vcpu] >> 32;
        qemu_log("elrsr1=%x\n", r);
        break;
    case 0xf0: /* apr */
        r = s->gich.apr[vcpu];
        break;
    case 0x100 ... 0x1fc: /* LRn */
        r = s->gich.lr[vcpu][(offset - 0x100) / 4];
#if 0
        if (r && (r & 0x1ff) == 27) {
            qemu_log("READ VCPU%d LR[%d]=%x\n",
                   vcpu, (offset - 0x100) / 4, r);
        }
#endif
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset %x\n", __func__, offset);
    }
    return r;
}

static void gic_hyp_write(GICState *s, int vcpu, int offset, uint32_t value)
{
    switch (offset) {
    case 0x00: /* HCR */
        s->gich.hcr[vcpu] = value;
        gicv_update(s);
        break;
    case 0x08: /* VMCR */
        gic_hyp_vmcr_write(s, vcpu, value);
        gicv_update(s);
        break;
    case 0xf0: /* apr */
        s->gich.apr[vcpu] = value;
        gicv_update(s);
        break;
    case 0x100 ... 0x1fc: /* LRn */
        if (s->gich.lr[vcpu][(offset - 0x100) / 4] != value) {
#if 0
            unsigned int state = extract32(s->gich.lr[vcpu][(offset - 0x100) / 4], 28, 2);
	    if (state) {
                qemu_log("BAD: OVERWRITE ACTIVE LR%d! vCPU%d %x -> %x\n",
                        (offset - 0x100) / 4, vcpu,
			s->gich.lr[vcpu][(offset - 0x100) / 4], value);
			gic_dump_lrs(s, "BAD");
            }
#endif
            s->gich.lr[vcpu][(offset - 0x100) / 4] = value;
#if 0
            if (value && (value & 0x1ff) == 27) {
                qemu_log("WRITE VCPU%d LR[%d]=%x\n",
                       vcpu, (offset - 0x100) / 4, value);
            }
#endif
            gicv_update(s);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset %x\n", __func__, offset);
        return;
    }
}

static uint64_t gic_do_hyp_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    GICState **backref = (GICState **)opaque;
    GICState *s = *backref;
    int id = backref - s->backref;
    uint64_t r;

    r = gic_hyp_read(s, id, addr);
    return r;
}

static void gic_do_hyp_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size)
{
    GICState **backref = (GICState **)opaque;
    GICState *s = *backref;
    int id = backref - s->backref;

    gic_hyp_write(s, id, addr, value);
}

static uint64_t gic_thishyp_read(void *opaque, hwaddr addr,
                                 unsigned size)
{
    GICState *s = (GICState *)opaque;
    int id = gic_get_current_cpu(s);
    uint64_t r;

    r = gic_hyp_read(s, id, addr);
    return r;
}

static void gic_thishyp_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned size)
{
    GICState *s = (GICState *)opaque;
    int id = gic_get_current_cpu(s);

    gic_hyp_write(s, id, addr, value);
}

/* Wrappers to read/write the GIC vCPU interface for the current vCPU */
static uint64_t gic_thisvcpu_read(void *opaque, hwaddr addr,
                                 unsigned size)
{
    GICState *s = (GICState *)opaque;
    int id = GIC_N_REALCPU + gic_get_current_cpu(s);
    uint64_t r;

    r = gic_cpu_read(s, id, addr, false);
    return r;
}

static void gic_thisvcpu_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned size)
{
    GICState *s = (GICState *)opaque;
    int id = GIC_N_REALCPU + gic_get_current_cpu(s);

    gic_cpu_write(s, id, addr, value, false);
}

static const MemoryRegionOps gic_thiscpu_ops = {
#if 1
    .access = thiscpu_access,
#else
    .read = gic_thiscpu_read,
    .write = gic_thiscpu_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
#endif
};

static const MemoryRegionOps gic_thishyp_ops = {
    .read = gic_thishyp_read,
    .write = gic_thishyp_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const MemoryRegionOps gic_hyp_ops = {
    .read = gic_do_hyp_read,
    .write = gic_do_hyp_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const MemoryRegionOps gic_thisvcpu_ops = {
    .read = gic_thisvcpu_read,
    .write = gic_thisvcpu_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void gic_init_irqs_and_distributor(GICState *s)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(s);
    int i;

    i = s->num_irq - GIC_INTERNAL;
    /* For the GIC, also expose incoming GPIO lines for PPIs for each CPU.
     * GPIO array layout is thus:
     *  [0..N-1] SPIs
     *  [N..N+31] PPIs for CPU 0
     *  [N+32..N+63] PPIs for CPU 1
     *   ...
     */
    i += (GIC_INTERNAL * s->num_cpu);

    qdev_init_gpio_in(DEVICE(s), gic_set_irq_cb, i);
    for (i = 0; i < GIC_N_REALCPU; i++) {
        sysbus_init_irq(sbd, &s->parent_irq[i]);
    }
    for (i = 0; i < GIC_N_REALCPU; i++) {
        sysbus_init_irq(sbd, &s->parent_irq[GIC_N_REALCPU + i]);
    }
    for (i = 0; i < GIC_N_REALCPU; i++) {
        sysbus_init_irq(sbd, &s->parent_fiq[i]);
    }
    for (i = 0; i < GIC_N_REALCPU; i++) {
        sysbus_init_irq(sbd, &s->parent_fiq[GIC_N_REALCPU + i]);
    }
    for (i = 0; i < NUM_CPU(s); i++) {
        sysbus_init_irq(sbd, &s->maint[i]);
    }
    qdev_init_gpio_out_named(DEVICE(s), s->parent_irq, "irq", GIC_N_REALCPU * 2);
    qdev_init_gpio_out_named(DEVICE(s), s->parent_fiq, "fiq", GIC_N_REALCPU * 2);
    qdev_init_gpio_out_named(DEVICE(s), s->maint, "maint", NUM_CPU(s));
    memory_region_init_io(&s->iomem, OBJECT(s), &gic_dist_ops, s,
                          "gic_dist", 0x1000);
}

static void arm_gic_realize(DeviceState *dev, Error **errp)
{
    /* Device instance realize function for the GIC sysbus device */
    int i;
    GICState *s = ARM_GIC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    ARMGICClass *agc = ARM_GIC_GET_CLASS(s);
    Error *local_err = NULL;

    agc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (kvm_enabled() && !kvm_arm_supports_user_irq()) {
        error_setg(errp, "KVM with user space irqchip only works when the "
                         "host kernel supports KVM_CAP_ARM_USER_IRQ");
        return;
    }

    /* This creates distributor and main CPU interface (s->cpuiomem[0]) */
    gic_init_irqs_and_distributor(s);

    /* Memory regions for the CPU interfaces (NVIC doesn't have these):
     * a region for "CPU interface for this core", then a region for
     * "CPU interface for core 0", "for core 1", ...
     * NB that the memory region size of 0x100 applies for the 11MPCore
     * and also cores following the GIC v1 spec (ie A9).
     * GIC v2 defines a larger memory region (0x1000) so this will need
     * to be extended when we implement A15.
     */
    memory_region_init_io(&s->cpuiomem[0], OBJECT(s), &gic_thiscpu_ops, s,
                          "gic_cpu", s->revision >= 2 ? s->map_stride * 2 : 0x100);
    memory_region_init_io(&s->hypiomem[0], OBJECT(s), &gic_thishyp_ops, s,
                          "gic_thishyp_cpu", 0x200);
    memory_region_init_io(&s->vcpuiomem, OBJECT(s), &gic_thisvcpu_ops, s,
                          "gic_thisvcpu",
                          s->revision >= 2 ? s->map_stride * 2 : 0x2000);
    for (i = 0; i < NUM_CPU(s); i++) {
        char *region_name = g_strdup_printf("gic_hyp_cpu-%d", i);
        s->backref[GIC_N_REALCPU + i] = s;
        memory_region_init_io(&s->hypiomem[i+1], OBJECT(s), &gic_hyp_ops,
                              &s->backref[i], region_name, 0x200);
        g_free(region_name);
    }
    /* Distributor */
    sysbus_init_mmio(sbd, &s->iomem);
    /* cpu interfaces (one for "current cpu" plus one per cpu) */
    sysbus_init_mmio(sbd, &s->cpuiomem[0]);
#if 0
    for (i = 1; i <= NUM_CPU(s); i++) {
        sysbus_init_mmio(sbd, &s->cpuiomem[i]);
    }
#endif
    sysbus_init_mmio(sbd, &s->hypiomem[0]);
    sysbus_init_mmio(sbd, &s->vcpuiomem);
#if 0
    /* virtual interface control blocks.
     * One for "current cpu" plus one per cpu.
     */
    for (i = 0; i <= NUM_CPU(s); i++) {
        sysbus_init_mmio(sbd, &s->hypiomem[i]);
    }
#endif
}

static void arm_gic_fdt_auto_parent(FDTGenericIntc *obj, Error **errp)
{
    GICState *s = ARM_GIC(obj);
    CPUState *cs;
    int i = 0;

    for (cs = first_cpu; cs; cs = CPU_NEXT(cs)) {
        if (i >= s->num_cpu) {
            break;
        }
        qdev_connect_gpio_out_named(DEVICE(obj), "irq", i,
                                    qdev_get_gpio_in(DEVICE(cs), 0));
        i++;
    }

    /* FIXME: Add some error checking */
}

static const FDTGenericGPIOSet arm_gic_client_gpios [] = {
    {
        .names = &fdt_generic_gpio_name_set_interrupts,
        .gpios = (FDTGenericGPIOConnection []) {
            { .name = "irq",        .range = 16 },
            { .name = "fiq",    .range = 16, .fdt_index = 16 },
            { .name = "maint",    .range = 4, .fdt_index = 32 },
            { },
        },
    },
    {
        .names = &fdt_generic_gpio_name_set_gpio,
        .gpios = (FDTGenericGPIOConnection []) {
            { .name = "pwr_cntrl", .range = 1, .fdt_index = 0 },
            { .name = "rst_cntrl", .range = 1, .fdt_index = 1 },
            { },
        },
    },
    { },
};

static void arm_gic_linux_init(LinuxDevice *obj)
{
    GICState *s = ARM_GIC(obj);
    int i;

    if (s->disable_linux_gic_init) {
        return;
    }

    for (i = 0 ; i < s->num_irq; ++i) {
        s->irq_state[i].group = 1;
    }
}

static void arm_gic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ARMGICClass *agc = ARM_GIC_CLASS(klass);
    FDTGenericIntcClass *fgic = FDT_GENERIC_INTC_CLASS(klass);
    FDTGenericGPIOClass *fggc = FDT_GENERIC_GPIO_CLASS(klass);
    LinuxDeviceClass *ldc = LINUX_DEVICE_CLASS(klass);

    agc->irq_handler = gic_set_irq;
    agc->parent_realize = dc->realize;
    dc->realize = arm_gic_realize;
    fgic->auto_parent = arm_gic_fdt_auto_parent;
    fggc->client_gpios = arm_gic_client_gpios;
    ldc->linux_init = arm_gic_linux_init;
}

static const TypeInfo arm_gic_info = {
    .name = TYPE_ARM_GIC,
    .parent = TYPE_ARM_GIC_COMMON,
    .instance_size = sizeof(GICState),
    .class_init = arm_gic_class_init,
    .class_size = sizeof(ARMGICClass),
    .interfaces = (InterfaceInfo []) {
        { TYPE_LINUX_DEVICE },
        { },
    }
};

static void arm_gic_register_types(void)
{
    type_register_static(&arm_gic_info);
}

type_init(arm_gic_register_types)
