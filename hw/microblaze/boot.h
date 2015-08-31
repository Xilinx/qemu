#ifndef __MICROBLAZE_BOOT__
#define __MICROBLAZE_BOOT__

#include "hw/hw.h"

void microblaze_load_kernel(MicroBlazeCPU *cpu, hwaddr ddr_base,
                            uint32_t ramsize,
                            const char *initrd_filename,
                            const char *dtb_filename,
                            void (*machine_cpu_reset)(MicroBlazeCPU *),
                            void *fdt, int fdt_size);

#endif /* __MICROBLAZE_BOOT __ */
