/*
 * MicroBlaze gdb server stub
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2013 SUSE LINUX Products GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/gdbstub.h"

/*
 * GDB expects SREGs in the following order:
 * PC, MSR, EAR, ESR, FSR, BTR, EDR, PID, ZPR, TLBX, TLBSX, TLBLO, TLBHI.
 *
 * PID, ZPR, TLBx, TLBsx, TLBLO, and TLBHI aren't modeled, so we don't
 * map them to anything and return a value of 0 instead.
 */

enum {
    GDB_PC    = 32 + 0,
    GDB_MSR   = 32 + 1,
    GDB_EAR   = 32 + 2,
    GDB_ESR   = 32 + 3,
    GDB_FSR   = 32 + 4,
    GDB_BTR   = 32 + 5,
    GDB_PVR0  = 32 + 6,
    GDB_PVR11 = 32 + 17,
    GDB_EDR   = 32 + 18,
    GDB_SLR   = 32 + 25,
    GDB_SHR   = 32 + 26,
};

int mb_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    CPUClass *cc = CPU_GET_CLASS(cs);
    CPUMBState *env = &cpu->env;
    uint32_t val;

    if (n > cc->gdb_num_core_regs) {
        return 0;
    }

    switch (n) {
    case 1 ... 31:
        val = env->regs[n];
        break;
    case GDB_PC:
        val = env->pc;
        break;
    case GDB_MSR:
        val = mb_cpu_read_msr(env);
        break;
    case GDB_EAR:
        val = env->ear;
        break;
    case GDB_ESR:
        val = env->esr;
        break;
    case GDB_FSR:
        val = env->fsr;
        break;
    case GDB_BTR:
        val = env->btr;
        break;
    case GDB_PVR0 ... GDB_PVR11:
        /* PVR12 is intentionally skipped */
        val = cpu->cfg.pvr_regs[n - GDB_PVR0];
        break;
    case GDB_EDR:
        val = env->edr;
        break;
    case GDB_SLR:
        val = env->slr;
        break;
    case GDB_SHR:
        val = env->shr;
        break;
    default:
        /* Other SRegs aren't modeled, so report a value of 0 */
        val = 0;
        break;
    }
    return gdb_get_reg32(mem_buf, val);
}

int mb_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    CPUClass *cc = CPU_GET_CLASS(cs);
    CPUMBState *env = &cpu->env;
    uint32_t tmp;

    if (n > cc->gdb_num_core_regs) {
        return 0;
    }

    tmp = ldl_p(mem_buf);

    switch (n) {
    case 1 ... 31:
        env->regs[n] = tmp;
        break;
    case GDB_PC:
        env->pc = tmp;
        break;
    case GDB_MSR:
        mb_cpu_write_msr(env, tmp);
        break;
    case GDB_EAR:
        env->ear = tmp;
        break;
    case GDB_ESR:
        env->esr = tmp;
        break;
    case GDB_FSR:
        env->fsr = tmp;
        break;
    case GDB_BTR:
        env->btr = tmp;
        break;
    case GDB_EDR:
        env->edr = tmp;
        break;
    case GDB_SLR:
        env->slr = tmp;
        break;
    case GDB_SHR:
        env->shr = tmp;
        break;
    }
    return 4;
}

static void mb_gen_xml_reg_tag(const MicroBlazeCPU *cpu, GString *s,
                               const char *name, uint8_t bitsize,
                               const char *type)
{
    g_string_append_printf(s, "<reg name=\"%s\" bitsize=\"%d\"",
                           name, bitsize);
    if (type) {
        g_string_append_printf(s, " type=\"%s\"", type);
    }
    g_string_append_printf(s, "/>\n");
}

static uint8_t mb_cpu_sreg_size(const MicroBlazeCPU *cpu, uint8_t index)
{
    /*
     * FIXME: 3/16/20 - mb-gdb will refuse to connect if we say registers are
     * larger then 32-bits.
     * For now, say none of our registers are dynamically sized, and are
     * therefore only 32-bits.
     */
    /*
    if (index == 21 && cpu->cfg.use_mmu) {
        return cpu->cfg.addr_size;
    }
    if (index == 2 || (index >= 12 && index < 16)) {
        return cpu->cfg.addr_size;
    }
    */

    return 32;
}

static void mb_gen_xml_reg_tags(const MicroBlazeCPU *cpu, GString *s)
{
    uint8_t i;
    const char *type;
    char reg_name[4];
    bool has_hw_exception = cpu->cfg.dopb_bus_exception ||
                            cpu->cfg.iopb_bus_exception ||
                            cpu->cfg.illegal_opcode_exception ||
                            cpu->cfg.opcode_0_illegal ||
                            cpu->cfg.div_zero_exception ||
                            cpu->cfg.unaligned_exceptions;

    static const char *reg_types[32] = {
        [1] = "data_ptr",
        [14] = "code_ptr",
        [15] = "code_ptr",
        [16] = "code_ptr",
        [17] = "code_ptr"
    };

    for (i = 0; i < 32; ++i) {
        type = reg_types[i];
        /* r17 only has a code_ptr tag if we have HW exceptions */
        if (i == 17 && !has_hw_exception) {
            type = NULL;
        }

        sprintf(reg_name, "r%d", i);
        mb_gen_xml_reg_tag(cpu, s, reg_name, 32, type);
    }
}

static void mb_gen_xml_sreg_tags(const MicroBlazeCPU *cpu, GString *s)
{
    uint8_t i;

    static const char *sreg_names[] = {
        "rpc",
        "rmsr",
        "rear",
        "resr",
        "rfsr",
        "rbtr",
        "rpvr0",
        "rpvr1",
        "rpvr2",
        "rpvr3",
        "rpvr4",
        "rpvr5",
        "rpvr6",
        "rpvr7",
        "rpvr8",
        "rpvr9",
        "rpvr10",
        "rpvr11",
        "redr",
        "rpid",
        "rzpr",
        "rtlblo",
        "rtlbhi",
        "rtlbx",
        "rtlbsx",
        "slr",
        "shr"
    };

    static const char *sreg_types[ARRAY_SIZE(sreg_names)] = {
        [SR_PC] = "code_ptr"
    };

    for (i = 0; i < ARRAY_SIZE(sreg_names); ++i) {
        mb_gen_xml_reg_tag(cpu, s, sreg_names[i], mb_cpu_sreg_size(cpu, i),
                           sreg_types[i]);
    }
}

void mb_gen_dynamic_xml(MicroBlazeCPU *cpu)
{
    GString *s = g_string_new(NULL);

    g_string_printf(s, "<?xml version=\"1.0\"?>\n"
                       "<!DOCTYPE feature SYSTEM \"gdb-target.dtd\">\n"
                       "<feature name=\"org.gnu.gdb.microblaze.core\">\n");

    mb_gen_xml_reg_tags(cpu, s);
    mb_gen_xml_sreg_tags(cpu, s);

    g_string_append_printf(s, "</feature>");

    cpu->dyn_xml.xml = g_string_free(s, false);
}

const char *mb_gdb_get_dynamic_xml(CPUState *cs, const char *xmlname)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);

    return cpu->dyn_xml.xml;
}
