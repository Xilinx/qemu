#ifndef FDT_GENERIC_DEVICES_H
#define FDT_GENERIC_DEVICES_H

#include "fdt_generic.h"
#include "exec/cpu-common.h"

#if !defined(CONFIG_USER_ONLY)

/* XXX: Hack to find the last range in a memory node.  */
typedef struct FDTMemoryInfo {
    unsigned int nr_regions;
    ram_addr_t last_base;
    ram_addr_t last_size;
} FDTMemoryInfo;

#endif

int pflash_cfi01_fdt_init(char *node_path, FDTMachineInfo *fdti, void *opaque);

extern int fdt_generic_num_cpus;

#endif /* FDT_GENERIC_DEVICES_H */
