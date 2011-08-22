#ifndef FDT_GENERIC_DEVICES_H
#define FDT_GENERIC_DEVICES_H

#include "fdt_generic.h"

int pflash_cfi01_fdt_init(char *node_path, FDTMachineInfo *fdti, void *opaque);

#endif /* FDT_GENERIC_DEVICES_H */
