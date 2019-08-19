#include "qemu/osdep.h"
#include "hw/fdt_generic_util.h"
#include "hw/fdt_generic_devices.h"
#include "qom/object.h"
#include "sysemu/blockdev.h"
#include "sysemu/sysemu.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "chardev/char.h"
#include "qemu/coroutine.h"

#include "hw/char/serial.h"
#include "hw/qdev-core.h"

/* Piggy back fdt_generic_util.c ERR_DEBUG symbol as these two are really the
 * same feature
 */

#ifndef FDT_GENERIC_UTIL_ERR_DEBUG
#define FDT_GENERIC_UTIL_ERR_DEBUG 0
#endif
#define DB_PRINT(lvl, ...) do { \
    if (FDT_GENERIC_UTIL_ERR_DEBUG > (lvl)) { \
        qemu_log_mask(lvl, ": %s: ", __func__); \
        qemu_log_mask(lvl, ## __VA_ARGS__); \
    } \
} while (0);

#define DB_PRINT_NP(lvl, ...) do { \
    if (FDT_GENERIC_UTIL_ERR_DEBUG > (lvl)) { \
        qemu_log_mask(lvl, "%s", node_path); \
        DB_PRINT((lvl), ## __VA_ARGS__); \
    } \
} while (0);

static int uart16550_fdt_init(char *node_path, FDTMachineInfo *fdti,
    void *priv)
{
    /* FIXME: Pass in dynamically */
    MemoryRegion *address_space_mem = get_system_memory();
    hwaddr base;
    uint32_t baudrate;
    qemu_irq irqline;
    bool map_mode;
    char irq_info[1024];
    Error *err = NULL;

    /* FIXME: respect #address and size cells */
    base = qemu_fdt_getprop_cell(fdti->fdt, node_path, "reg", 0,
                                 false, &error_abort);
    base += qemu_fdt_getprop_cell(fdti->fdt, node_path, "reg-offset", 0,
                                  false, &error_abort);
    base &= ~3ULL; /* qemu uart16550 model starts with 3* 8bit offset */

    baudrate = qemu_fdt_getprop_cell(fdti->fdt, node_path, "current-speed",
                                     0, false, &err);
    if (err) {
        baudrate = 115200;
    }

    irqline = *fdt_get_irq_info(fdti, node_path, 0, irq_info, &map_mode);
    assert(!map_mode);
    DB_PRINT_NP(0, "UART16550a: baseaddr: 0x" TARGET_FMT_plx
                ", irq: %s, baud %d\n", base, irq_info, baudrate);

    /* it_shift = 2, reg-shift in DTS - for Xilnx IP is hardcoded */
    serial_mm_init(address_space_mem, base, 2, irqline, baudrate,
                   serial_hd(fdt_serial_ports), DEVICE_LITTLE_ENDIAN);
    return 0;
}

fdt_register_compatibility_n(uart16550_fdt_init, "compatible:ns16550", 0);
fdt_register_compatibility_n(uart16550_fdt_init, "compatible:ns16550a", 1);
