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

int mb_cpu_gdb_read_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    CPUMBState *env = &cpu->env;

    /*
     * GDB expects registers to be reported in this order:
     * R0-R31
     * PC-BTR
     * PVR0-PVR11
     * EDR-TLBHI
     * SLR-SHR
     */
    if (n < 32) {
        return gdb_get_reg32(mem_buf, env->regs[n]);
    } else {
        n -= 32;
        switch (n) {
        case 0 ... 5:
            return gdb_get_reg32(mem_buf, env->sregs[n]);
        /* PVR12 is intentionally skipped */
        case 6 ... 17:
            n -= 6;
            return gdb_get_reg32(mem_buf, env->pvr.regs[n]);
        case 18 ... 24:
            /* Add an offset of 6 to resume where we left off with SRegs */
            n = n - 18 + 6;
            return gdb_get_reg32(mem_buf, env->sregs[n]);
        case 25:
            return gdb_get_reg32(mem_buf, env->slr);
        case 26:
            return gdb_get_reg32(mem_buf, env->shr);
        default:
            return 0;
        }
    }
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

    if (n < 32) {
        env->regs[n] = tmp;
    } else {
        n -= 32;
        switch (n) {
        case 0 ... 5:
            env->sregs[n] = tmp;
            break;
        /* PVR12 is intentionally skipped */
        case 6 ... 17:
            n -= 6;
            env->pvr.regs[n] = tmp;
            break;
        case 18 ... 24:
            /* Add an offset of 6 to resume where we left off with SRegs */
            n = n - 18 + 6;
            env->sregs[n] = tmp;
            break;
        case 25:
            env->slr = tmp;
            break;
        case 26:
            env->shr = tmp;
            break;
        }
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
        "rslr",
        "rshr"
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
