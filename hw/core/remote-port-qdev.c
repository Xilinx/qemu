/*
 * QEMU remote attach
 *
 * Copyright (c) 2013 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 * This code is licensed under the GNU GPL.
 */

#include "qemu/osdep.h"
#include "hw/qdev.h"
#include "hw/sysbus.h"
#include "monitor/monitor.h"
#include "monitor/qdev.h"
#include "qmp-commands.h"
#include "sysemu/arch_init.h"
#include "qapi/qmp/qerror.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/help_option.h"

#include "hw/remote-port.h"

#ifdef CONFIG_REMOTE_PORT
/* Scan for remote-port links to be setup.  */
void rp_device_add(QemuOpts *opts, DeviceState *dev, Error **errp)
{
    Error *err = NULL;
    Object *adaptor;
    bool ambiguous;
    const char *path;
    char *name;
    int i;

    /*
     * Find the adaptor this remote-port device is connected to.
     * At the moment, we only support one adaptor per device.
     */
    name = g_strdup_printf("rp-adaptor%d", 0);
    path = qemu_opt_get(opts, name);
    g_free(name);
    if (!path) {
        /* This is not a remote-port device.  */
        return;
    }
    adaptor = object_resolve_path(path, &ambiguous);
    if (!adaptor) {
        error_setg(errp, "Did not find rp adaptor %s!\n", path);
        return;
    }

    /*
     * Loop through the channels this device provides and attach
     * them to the adaptor.
     */
    for (i = 0; i < INT_MAX; i++) {
        unsigned long dev_nr;
        const char *dev_nr_str;

        name = g_strdup_printf("rp-chan%d", i);
        dev_nr_str = qemu_opt_get(opts, name);
        g_free(name);

        if (!dev_nr_str) {
            if (i == 0) {
                /* At least one channel must be provided.  */
                error_setg(errp, "Did not find rp-chan%d!\n", i);
            }
            return;
        }

        if (qemu_strtoul(dev_nr_str, NULL, 0, &dev_nr)) {
            error_setg(errp, "Invalid rp-chan%d!\n", i);
            return;
        }

        /* Now, attach the device to the adaptor.  */
        rp_device_attach(adaptor, OBJECT(dev), 0, dev_nr, &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }
    }
}
#else
void rp_device_add(QemuOpts *opts, DeviceState *dev, Error **errp)
{
    const char *path;
    char *name;

    /*
     * Find the adaptor this remote-port device is connected to.
     * At the moment, we only support one adaptor per device.
     */
    name = g_strdup_printf("rp-adaptor%d", 0);
    path = qemu_opt_get(opts, name);
    g_free(name);
    if (!path) {
        /* This is not a remote-port device.  */
        return;
    }

    error_setg(errp, "Remote Port was not enabled at compile time.\n");
    return;
}
#endif
