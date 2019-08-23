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
#include "cpu.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qapi-commands-ui.h"
#include "qapi/qapi-events-ui.h"
#include "qapi/qapi-events-injection.h"
#include "qapi/error.h"
#include "qapi/qapi-types-injection.h"
#include "qapi/qapi-commands-injection.h"
#include "qemu/timer.h"
#include "exec/memory.h"
#include "hw/irq.h"
#include "qemu/log.h"
#include "qemu/queue.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"
#include "exec/exec-all.h"

typedef struct FaultEventEntry FaultEventEntry;
static QLIST_HEAD(, FaultEventEntry) events = QLIST_HEAD_INITIALIZER(events);
static QEMUTimer *timer;

#ifndef DEBUG_FAULT_INJECTION
#define DEBUG_FAULT_INJECTION 0
#endif

#define DPRINTF(fmt, ...) do {                                                 \
    if (DEBUG_FAULT_INJECTION) {                                               \
        qemu_log("fault_injection: " fmt , ## __VA_ARGS__);                    \
    }                                                                          \
} while (0);

void qmp_write_mem(int64_t addr, int64_t val, int64_t size, bool has_cpu,
                   int64_t cpu, bool has_qom, const char *qom, bool debug,
                   Error **errp)
{
    int cpu_id = 0;
    Object *obj;
    CPUState *s;
    MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;

    attrs.debug = debug;
    if (has_qom) {
        obj = object_resolve_path(qom, NULL);
        s = (CPUState *)object_dynamic_cast(obj, TYPE_CPU);
        if (s) {
            cpu_id = s->cpu_index;
            DPRINTF("write memory addr=0x%" PRIx64 " val=0x%" PRIx64
                    " ld size=%"PRId64" cpu_path=%s (cpu=%d)\n", addr, val,
                    size, qom, cpu_id);
        } else {
            error_setg(errp, "'%s' is not a CPU or doesn't exists", qom);
            DPRINTF("write memory failed.\n");
            return;
        }
    } else {
        if (has_cpu) {
            cpu_id = cpu;
        }
        DPRINTF("write memory addr=0x%" PRIx64 "val=0x%" PRIx64 " "
                "size=%"PRId64" cpu=%d\n", addr, val, size, cpu_id);
    }

    if (address_space_write(cpu_get_address_space(qemu_get_cpu(cpu_id), 0),
                            addr, attrs, ((uint8_t *)&val), size)) {
        DPRINTF("write memory failed.\n");
    } else {
        DPRINTF("write memory succeed.\n");
    }
}

ReadValue *qmp_read_mem(int64_t addr, int64_t size, bool has_cpu, int64_t cpu,
                     bool has_qom, const char *qom, Error **errp)
{
    ReadValue *ret = g_new0(ReadValue, 1);
    int cpu_id = 0;
    Object *obj;
    CPUState *s;

    ret->value = 0;

    if (has_qom) {
        obj = object_resolve_path(qom, NULL);
        s = (CPUState *)object_dynamic_cast(obj, TYPE_CPU);
        if (s) {
            cpu_id = s->cpu_index;
            DPRINTF("read memory addr=0x%" PRIx64 " size=%"PRId64" cpu_path=%s"
                    " (cpu=%d)\n", addr, size, qom, cpu_id);
        } else {
            error_setg(errp, "'%s' is not a CPU or doesn't exists", qom);
            DPRINTF("read memory failed.\n");
            return ret;
        }
    } else {
        if (has_cpu) {
            cpu_id = cpu;
        }
        DPRINTF("read memory addr=0x%" PRIx64 " size=%"PRId64" (cpu=%d)\n",
                addr, size, cpu_id);
    }

    if (address_space_read(cpu_get_address_space(qemu_get_cpu(cpu_id), 0), addr,
                           MEMTXATTRS_UNSPECIFIED, (uint8_t *) &(ret->value), size)) {
        DPRINTF("read memory failed.\n");
        return ret;
    } else {
        DPRINTF("read memory succeed 0x%" PRIx64 ".\n", ret->value);
        return ret;
    }
}

struct FaultEventEntry {
    uint64_t time_ns;
    int64_t val;
    QLIST_ENTRY(FaultEventEntry) node;
};

static void mod_next_event_timer(void)
{
    uint64_t val;
    FaultEventEntry *entry;

    if (QLIST_EMPTY(&events)) {
        return;
    } else {
        val = QLIST_FIRST(&events)->time_ns;
    }

    QLIST_FOREACH(entry, &events, node) {
        if (val > entry->time_ns) {
            val = entry->time_ns;
        }
    }

    timer_mod(timer, val);
}

static void do_fault(void *opaque)
{
    FaultEventEntry *entry;
    FaultEventEntry *next;
    uint64_t current_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    QLIST_FOREACH_SAFE(entry, &events, node, next) {
        if (entry->time_ns < current_time) {
            DPRINTF("fault %"PRId64" happened @%"PRId64"!\n", entry->val,
                    qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
            qapi_event_send_fault_event(entry->val, current_time);
            QLIST_REMOVE(entry, node);
            g_free(entry);
            vm_stop_from_timer(RUN_STATE_DEBUG);
        }
    }

    mod_next_event_timer();
}

void qmp_trigger_event(int64_t time_ns, int64_t event_id, Error **errp)
{
    FaultEventEntry *entry;

    DPRINTF("trigger_event(%"PRId64", %"PRId64")\n", time_ns, event_id);

    entry = g_new0(FaultEventEntry, 1);
    entry->time_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + time_ns;
    entry->val = event_id;
    QLIST_INSERT_HEAD(&events, entry, node);

    if (!timer) {
        timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, do_fault, NULL);
    }

    mod_next_event_timer();
}

void qmp_inject_gpio(const char *device_name, bool has_gpio, const char *gpio,
                     int64_t num, int64_t val, Error **errp)
{
    DeviceState *dev;
    qemu_irq irq;

    dev = DEVICE(object_resolve_path(device_name, NULL));
    if (!dev) {
        error_setg(errp, "Device '%s' is not a device", device_name);
        return;
    }

    irq = qdev_get_gpio_in_named(dev, has_gpio ? gpio : NULL, num);
    if (!irq) {
        error_setg(errp, "GPIO '%s' doesn't exists", has_gpio ? gpio : "unnammed");
        return;
    }

    DPRINTF("inject gpio device %s, gpio %s, num %" PRId64 ", val %" PRIx64
            "\n", device_name, gpio, num, val);

    qemu_set_irq(irq, val);
}
