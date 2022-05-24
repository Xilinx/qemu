/*
 * Small Device-tree driven RISC-V machine creator.
 *
 * Copyright (c) 2022 Advanced Micro Devices.
 * Written by Edgar E. Iglesias.
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "sysemu/device_tree.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/riscv/boot.h"

#include "hw/fdt_generic_util.h"
#include "hw/fdt_generic_devices.h"

static void riscv_fdt_init(MachineState *machine)
{
    FDTMachineInfo *fdti;
    void *fdt = NULL;
    int fdt_size;

    if (!machine->dtb && !machine->hw_dtb) {
        error_report("No hw-dtb found");
        exit(1);
    }

    fdt = load_device_tree(machine->hw_dtb, &fdt_size);
    if (!fdt) {
        error_report("Error: Unable to load Hardware Device Tree %s\n",
                      machine->hw_dtb);
        exit(1);
    }

    /* Instantiate peripherals from the FDT.  */
    fdti = fdt_generic_create_machine(fdt, NULL);
    fdt_init_destroy_fdti(fdti);
    return;
}

static void riscv_fdt_machine_init(MachineClass *mc)
{
    mc->desc = "RISC-V flat device tree driven machine model";
    mc->init = riscv_fdt_init;
}

DEFINE_MACHINE("riscv-fdt", riscv_fdt_machine_init)
