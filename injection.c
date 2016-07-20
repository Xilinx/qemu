/*
 * fault_injection.c
 *
 * Copyright (C) 2016 Fred KONRAD <fred.konrad@greensocs.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qjson.h"
#include "qmp-commands.h"
#include "qemu/timer.h"
#include "qapi-event.h"
#include "exec/memory.h"
#include "qom/cpu.h"
#include "qemu/log.h"

#ifndef DEBUG_FAULT_INJECTION
#define DEBUG_FAULT_INJECTION 0
#endif

#define DPRINTF(fmt, ...) do {                                                 \
    if (DEBUG_FAULT_INJECTION) {                                               \
        qemu_log("fault_injection: " fmt , ## __VA_ARGS__);                    \
    }                                                                          \
} while (0);

/* XXX: Is already implemented upstream */
static AddressSpace *cpu_get_address_space(CPUState *cpu, int asidx)
{
    assert(cpu->as);
    return &cpu->as[asidx];
}

void qmp_write_mem(int64_t addr, int64_t val, int64_t size, bool has_cpu,
                   int64_t cpu, bool has_qom, const char *qom, Error **errp)
{
    int64_t cpu_id = 0;
    Object *obj;
    CPUState *s;

    if (has_qom) {
        obj = object_resolve_path(qom, NULL);
        s = (CPUState *)object_dynamic_cast(obj, TYPE_CPU);
        if (s) {
            cpu_id = s->cpu_index;
            DPRINTF("write memory addr=0x%" PRIx64 " val=0x%" PRIx64
                    " ld size=%ld cpu_path=%s (cpu=%ld)\n", addr, val, size,
                    qom, cpu_id);
        } else {
            error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                            "'%s' is not a CPU or doesn't exists", qom);
            DPRINTF("write memory failed.\n");
            return;
        }
    } else {
        if (has_cpu) {
            cpu_id = cpu;
        }
        DPRINTF("write memory addr=0x%" PRIx64 "val=0x%" PRIx64 " size=%ld"
                " cpu=%ld\n", addr, val, size, cpu_id);
    }

    if (address_space_write(cpu_get_address_space(qemu_get_cpu(cpu_id), 0),
                            addr, ((uint8_t *)&val), size)) {
        DPRINTF("write memory failed.\n");
    } else {
        DPRINTF("write memory succeed.\n");
    }
}

int64_t qmp_read_mem(int64_t addr, int64_t size, bool has_cpu, int64_t cpu,
                     bool has_qom, const char *qom, Error **errp)
{
    int64_t val = 0;
    int64_t cpu_id = 0;
    Object *obj;
    CPUState *s;

    if (has_qom) {
        obj = object_resolve_path(qom, NULL);
        s = (CPUState *)object_dynamic_cast(obj, TYPE_CPU);
        if (s) {
            cpu_id = s->cpu_index;
            DPRINTF("read memory addr=0x%" PRIx64 " size=%ld cpu_path=%s"
                    " (cpu=%ld)\n", addr, size, qom, cpu_id);
        } else {
            error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                            "'%s' is not a CPU or doesn't exists", qom);
            DPRINTF("read memory failed.\n");
            return 0;
        }
    } else {
        if (has_cpu) {
            cpu_id = cpu;
        }
        DPRINTF("read memory addr=0x%" PRIx64 " size=%ld (cpu=%ld)\n", addr,
                size, cpu_id);
    }

    if (address_space_read(cpu_get_address_space(qemu_get_cpu(cpu_id), 0), addr,
                           ((uint8_t *) &val), size)) {
        DPRINTF("read memory failed.\n");
        return 0;
    } else {
        DPRINTF("read memory succeed 0x%" PRIx64 ".\n", val);
        return val;
    }
}

