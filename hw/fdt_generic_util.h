#ifndef FDT_GENERIC_UTIL_H
#define FDT_GENERIC_UTIL_H

#include "qemu-common.h"
#include "fdt_generic.h"

/* create a fdt_generic machine. the top level cpu irqs are required for
 * systems instantiating interrupt devices. The client is responsible for
 * destroying the returned FDTMachineInfo (using fdt_init_destroy_fdti)
 */

FDTMachineInfo *fdt_generic_create_machine(void *fdt, qemu_irq *cpu_irq);

/* fdt init a simple bus. Search the bus for child nodes and instantiate or
 * invalidate devices as appropriate. Conformant to FDTInitFn prototype, i.e.
 * a bus may fdt_register_compatibilty this as its instantiator.
 */

int simple_bus_fdt_init(char *node_path, FDTMachineInfo *fdti, void *priv);

/* get an irq for a device. The interrupt parent of a device is idenitified
 * and the specified irq (by the interrupts device-tree property) is retrieved
 */

qemu_irq fdt_get_irq(FDTMachineInfo *fdti, char *node_path, int irq_idx);

/* same as above, but poulates err with non-zero if something goes wrong, and
 * populates info with a human readable string giving some basic information
 * about the interrupt connection found (or not found). Both arguments are
 * optional (i.e. can be NULL)
 */

qemu_irq fdt_get_irq_info(FDTMachineInfo *fdti, char *node_path, int irq_idx,
    int *err, char * info);

#endif /* FDT_GENERIC_UTIL_H */
