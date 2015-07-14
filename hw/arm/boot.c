/*
 * ARM kernel loader.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "config.h"
#include "hw/hw.h"
#include "hw/arm/arm.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "elf.h"
#include "sysemu/device_tree.h"
#include "qemu/config-file.h"
#include "exec/address-spaces.h"
#include "sysemu/kvm.h"

#include "hw/guest/linux.h"

/* Kernel boot protocol is specified in the kernel docs
 * Documentation/arm/Booting and Documentation/arm64/booting.txt
 * They have different preferred image load offsets from system RAM base.
 */
#define KERNEL_ARGS_ADDR 0x100
#define KERNEL_LOAD_ADDR 0x00008000
#define KERNEL64_LOAD_ADDR 0x00080000

typedef enum {
    FIXUP_NONE = 0,   /* do nothing */
    FIXUP_TERMINATOR, /* end of insns */
    FIXUP_BOARDID,    /* overwrite with board ID number */
    FIXUP_ARGPTR,     /* overwrite with pointer to kernel args */
    FIXUP_ENTRYPOINT, /* overwrite with kernel entry point */
    FIXUP_GIC_CPU_IF, /* overwrite with GIC CPU interface address */
    FIXUP_BOOTREG,    /* overwrite with boot register address */
    FIXUP_DSB,        /* overwrite with correct DSB insn for cpu */
    FIXUP_EL,         /* overwrite with kernel entry EL */
    FIXUP_MAX,
} FixupType;

typedef struct ARMInsnFixup {
    uint32_t insn;
    FixupType fixup;
    int shift;
    int length;
} ARMInsnFixup;

static const ARMInsnFixup bootloader_aarch64[] = {
    { 0xd5384240 }, /* mrs x0, currentel */
    { 0x7100301f }, /* cmp w0, #0xc */
    { 0x54000001 + (9 << 5) }, /* b.ne ELx_start */
/* Jump down from EL3 to ELx */
    { 0x10000001 + (9 << 5) }, /* adr x1, ELx_start */
    { 0xd53e1100 }, /* mrs x0, scr_el3 */
    { 0xb2400000 }, /* orr x0, x0, #0x1 - SCR.NS */
    { 0xb2780000 }, /* orr x0, x0, #0x80 - SCR.HCE */
    { 0xd51e1100 }, /* msr scr_el3, x0 */
    { 0xd2807820, FIXUP_EL, 7, 2 }, /* movz x0, 0x3c1 (+ EL<<2) */
    { 0xd51e4000 }, /* msr spsr_el3, x0 */
    { 0xd51e4021 }, /* msr elr_el3, x1 */
    { 0xd69f03e0 }, /* eret */
/* ELx_start: */
    { 0x580000c0 }, /* ldr x0, arg ; Load the lower 32-bits of DTB */
    { 0xaa1f03e1 }, /* mov x1, xzr */
    { 0xaa1f03e2 }, /* mov x2, xzr */
    { 0xaa1f03e3 }, /* mov x3, xzr */
    { 0x58000084 }, /* ldr x4, entry ; Load the lower 32-bits of kernel entry */
    { 0xd61f0080 }, /* br x4      ; Jump to the kernel entry point */
    { 0, FIXUP_ARGPTR }, /* arg: .word @DTB Lower 32-bits */
    { 0 }, /* .word @DTB Higher 32-bits */
    { 0, FIXUP_ENTRYPOINT }, /* entry: .word @Kernel Entry Lower 32-bits */
    { 0 }, /* .word @Kernel Entry Higher 32-bits */
    { 0, FIXUP_TERMINATOR }
};

/* The worlds second smallest bootloader.  Set r0-r2, then jump to kernel.  */
static const ARMInsnFixup bootloader[] = {
    { 0xe3a00000 }, /* mov     r0, #0 */
    { 0xe59f1004 }, /* ldr     r1, [pc, #4] */
    { 0xe59f2004 }, /* ldr     r2, [pc, #4] */
    { 0xe59ff004 }, /* ldr     pc, [pc, #4] */
    { 0, FIXUP_BOARDID },
    { 0, FIXUP_ARGPTR },
    { 0, FIXUP_ENTRYPOINT },
    { 0, FIXUP_TERMINATOR }
};

/* Handling for secondary CPU boot in a multicore system.
 * Unlike the uniprocessor/primary CPU boot, this is platform
 * dependent. The default code here is based on the secondary
 * CPU boot protocol used on realview/vexpress boards, with
 * some parameterisation to increase its flexibility.
 * QEMU platform models for which this code is not appropriate
 * should override write_secondary_boot and secondary_cpu_reset_hook
 * instead.
 *
 * This code enables the interrupt controllers for the secondary
 * CPUs and then puts all the secondary CPUs into a loop waiting
 * for an interprocessor interrupt and polling a configurable
 * location for the kernel secondary CPU entry point.
 */
#define DSB_INSN 0xf57ff04f
#define CP15_DSB_INSN 0xee070f9a /* mcr cp15, 0, r0, c7, c10, 4 */

static const ARMInsnFixup smpboot[] = {
    { 0xe59f2028 }, /* ldr r2, gic_cpu_if */
    { 0xe59f0028 }, /* ldr r0, bootreg_addr */
    { 0xe3a01001 }, /* mov r1, #1 */
    { 0xe5821000 }, /* str r1, [r2] - set GICC_CTLR.Enable */
    { 0xe3a010ff }, /* mov r1, #0xff */
    { 0xe5821004 }, /* str r1, [r2, 4] - set GIC_PMR.Priority to 0xff */
    { 0, FIXUP_DSB },   /* dsb */
    { 0xe320f003 }, /* wfi */
    { 0xe5901000 }, /* ldr     r1, [r0] */
    { 0xe1110001 }, /* tst     r1, r1 */
    { 0x0afffffb }, /* beq     <wfi> */
    { 0xe12fff11 }, /* bx      r1 */
    { 0, FIXUP_GIC_CPU_IF }, /* gic_cpu_if: .word 0x.... */
    { 0, FIXUP_BOOTREG }, /* bootreg_addr: .word 0x.... */
    { 0, FIXUP_TERMINATOR }
};

static void write_bootloader(const char *name, hwaddr addr,
                             const ARMInsnFixup *insns, uint32_t *fixupcontext)
{
    /* Fix up the specified bootloader fragment and write it into
     * guest memory using rom_add_blob_fixed(). fixupcontext is
     * an array giving the values to write in for the fixup types
     * which write a value into the code array.
     */
    int i, len;
    uint32_t *code;

    len = 0;
    while (insns[len].fixup != FIXUP_TERMINATOR) {
        len++;
    }

    code = g_new0(uint32_t, len);

    for (i = 0; i < len; i++) {
        uint32_t insn = insns[i].insn;
        FixupType fixup = insns[i].fixup;
        int shift = insns[i].shift;
        int length = insns[i].length ? insns[i].length : 32;

        assert(shift + length <= 32);

        switch (fixup) {
        case FIXUP_NONE:
            break;
        case FIXUP_BOARDID:
        case FIXUP_ARGPTR:
        case FIXUP_ENTRYPOINT:
        case FIXUP_GIC_CPU_IF:
        case FIXUP_BOOTREG:
        case FIXUP_DSB:
        case FIXUP_EL:
            insn = deposit32(insn, shift, length, fixupcontext[fixup]);
            break;
        default:
            abort();
        }
        code[i] = tswap32(insn);
    }

    rom_add_blob_fixed(name, code, len * sizeof(uint32_t), addr);

    g_free(code);
}

static void default_write_secondary(ARMCPU *cpu,
                                    const struct arm_boot_info *info)
{
    uint32_t fixupcontext[FIXUP_MAX];

    fixupcontext[FIXUP_GIC_CPU_IF] = info->gic_cpu_if_addr;
    fixupcontext[FIXUP_BOOTREG] = info->smp_bootreg_addr;
    if (arm_feature(&cpu->env, ARM_FEATURE_V7)) {
        fixupcontext[FIXUP_DSB] = DSB_INSN;
    } else {
        fixupcontext[FIXUP_DSB] = CP15_DSB_INSN;
    }

    write_bootloader("smpboot", info->smp_loader_start,
                     smpboot, fixupcontext);
}

static void default_reset_secondary(ARMCPU *cpu,
                                    const struct arm_boot_info *info)
{
    CPUARMState *env = &cpu->env;

    stl_phys_notdirty(first_cpu->as, info->smp_bootreg_addr, 0);
    env->regs[15] = info->smp_loader_start;
}

static inline bool have_dtb(const struct arm_boot_info *info)
{
    return info->dtb_filename || info->get_dtb;
}

#define WRITE_WORD(p, value) do { \
    stl_phys_notdirty(&address_space_memory, p, value);  \
    p += 4;                       \
} while (0)

static void set_kernel_args(const struct arm_boot_info *info)
{
    int initrd_size = info->initrd_size;
    hwaddr base = info->loader_start;
    hwaddr p;

    p = base + KERNEL_ARGS_ADDR;
    /* ATAG_CORE */
    WRITE_WORD(p, 5);
    WRITE_WORD(p, 0x54410001);
    WRITE_WORD(p, 1);
    WRITE_WORD(p, 0x1000);
    WRITE_WORD(p, 0);
    /* ATAG_MEM */
    /* TODO: handle multiple chips on one ATAG list */
    WRITE_WORD(p, 4);
    WRITE_WORD(p, 0x54410002);
    WRITE_WORD(p, info->ram_size);
    WRITE_WORD(p, info->loader_start);
    if (initrd_size) {
        /* ATAG_INITRD2 */
        WRITE_WORD(p, 4);
        WRITE_WORD(p, 0x54420005);
        WRITE_WORD(p, info->initrd_start);
        WRITE_WORD(p, initrd_size);
    }
    if (info->kernel_cmdline && *info->kernel_cmdline) {
        /* ATAG_CMDLINE */
        int cmdline_size;

        cmdline_size = strlen(info->kernel_cmdline);
        cpu_physical_memory_write(p + 8, info->kernel_cmdline,
                                  cmdline_size + 1);
        cmdline_size = (cmdline_size >> 2) + 1;
        WRITE_WORD(p, cmdline_size + 2);
        WRITE_WORD(p, 0x54410009);
        p += cmdline_size * 4;
    }
    if (info->atag_board) {
        /* ATAG_BOARD */
        int atag_board_len;
        uint8_t atag_board_buf[0x1000];

        atag_board_len = (info->atag_board(info, atag_board_buf) + 3) & ~3;
        WRITE_WORD(p, (atag_board_len + 8) >> 2);
        WRITE_WORD(p, 0x414f4d50);
        cpu_physical_memory_write(p, atag_board_buf, atag_board_len);
        p += atag_board_len;
    }
    /* ATAG_END */
    WRITE_WORD(p, 0);
    WRITE_WORD(p, 0);
}

static void set_kernel_args_old(const struct arm_boot_info *info)
{
    hwaddr p;
    const char *s;
    int initrd_size = info->initrd_size;
    hwaddr base = info->loader_start;

    /* see linux/include/asm-arm/setup.h */
    p = base + KERNEL_ARGS_ADDR;
    /* page_size */
    WRITE_WORD(p, 4096);
    /* nr_pages */
    WRITE_WORD(p, info->ram_size / 4096);
    /* ramdisk_size */
    WRITE_WORD(p, 0);
#define FLAG_READONLY	1
#define FLAG_RDLOAD	4
#define FLAG_RDPROMPT	8
    /* flags */
    WRITE_WORD(p, FLAG_READONLY | FLAG_RDLOAD | FLAG_RDPROMPT);
    /* rootdev */
    WRITE_WORD(p, (31 << 8) | 0);	/* /dev/mtdblock0 */
    /* video_num_cols */
    WRITE_WORD(p, 0);
    /* video_num_rows */
    WRITE_WORD(p, 0);
    /* video_x */
    WRITE_WORD(p, 0);
    /* video_y */
    WRITE_WORD(p, 0);
    /* memc_control_reg */
    WRITE_WORD(p, 0);
    /* unsigned char sounddefault */
    /* unsigned char adfsdrives */
    /* unsigned char bytes_per_char_h */
    /* unsigned char bytes_per_char_v */
    WRITE_WORD(p, 0);
    /* pages_in_bank[4] */
    WRITE_WORD(p, 0);
    WRITE_WORD(p, 0);
    WRITE_WORD(p, 0);
    WRITE_WORD(p, 0);
    /* pages_in_vram */
    WRITE_WORD(p, 0);
    /* initrd_start */
    if (initrd_size) {
        WRITE_WORD(p, info->initrd_start);
    } else {
        WRITE_WORD(p, 0);
    }
    /* initrd_size */
    WRITE_WORD(p, initrd_size);
    /* rd_start */
    WRITE_WORD(p, 0);
    /* system_rev */
    WRITE_WORD(p, 0);
    /* system_serial_low */
    WRITE_WORD(p, 0);
    /* system_serial_high */
    WRITE_WORD(p, 0);
    /* mem_fclk_21285 */
    WRITE_WORD(p, 0);
    /* zero unused fields */
    while (p < base + KERNEL_ARGS_ADDR + 256 + 1024) {
        WRITE_WORD(p, 0);
    }
    s = info->kernel_cmdline;
    if (s) {
        cpu_physical_memory_write(p, s, strlen(s) + 1);
    } else {
        WRITE_WORD(p, 0);
    }
}

/**
 * load_dtb() - load a device tree binary image into memory
 * @addr:       the address to load the image at
 * @binfo:      struct describing the boot environment
 * @addr_limit: upper limit of the available memory area at @addr
 *
 * Load a device tree supplied by the machine or by the user  with the
 * '-dtb' command line option, and put it at offset @addr in target
 * memory.
 *
 * If @addr_limit contains a meaningful value (i.e., it is strictly greater
 * than @addr), the device tree is only loaded if its size does not exceed
 * the limit.
 *
 * Returns: the size of the device tree image on success,
 *          0 if the image size exceeds the limit,
 *          -1 on errors.
 *
 * Note: Must not be called unless have_dtb(binfo) is true.
 */
static int load_dtb(hwaddr addr, const struct arm_boot_info *binfo,
                    hwaddr addr_limit)
{
    void *fdt = binfo->fdt;
    int size = binfo->fdt_size;
    int rc;
    uint32_t acells, scells;

    if (fdt) {
        /* do nothing */
    } else if (binfo->dtb_filename) {
        char *filename;
        filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, binfo->dtb_filename);
        if (!filename) {
            fprintf(stderr, "Couldn't open dtb file %s\n", binfo->dtb_filename);
            goto fail;
        }

        fdt = load_device_tree(filename, &size);
        if (!fdt) {
            fprintf(stderr, "Couldn't open dtb file %s\n", filename);
            g_free(filename);
            goto fail;
        }
        g_free(filename);
    } else {
        fdt = binfo->get_dtb(binfo, &size);
        if (!fdt) {
            fprintf(stderr, "Board was unable to create a dtb blob\n");
            goto fail;
        }
    }

    if (addr_limit > addr && size > (addr_limit - addr)) {
        /* Installing the device tree blob at addr would exceed addr_limit.
         * Whether this constitutes failure is up to the caller to decide,
         * so just return 0 as size, i.e., no error.
         */
        g_free(fdt);
        return 0;
    }

    acells = qemu_fdt_getprop_cell(fdt, "/", "#address-cells", 0, false, &error_abort);
    scells = qemu_fdt_getprop_cell(fdt, "/", "#size-cells", 0, false, &error_abort);

    if (acells == 0 || scells == 0) {
        fprintf(stderr, "dtb file invalid (#address-cells or #size-cells 0)\n");
        goto fail;
    }

    if (scells < 2 && binfo->ram_size >= (1ULL << 32)) {
        /* This is user error so deserves a friendlier error message
         * than the failure of setprop_sized_cells would provide
         */
        fprintf(stderr, "qemu: dtb file not compatible with "
                "RAM size > 4GB\n");
        goto fail;
    }

    if (!binfo->fdt) {
        rc = qemu_fdt_setprop_sized_cells(fdt, "/memory", "reg",
                                          acells, binfo->loader_start,
                                          scells, binfo->ram_size);
        if (rc < 0) {
            fprintf(stderr, "couldn't set /memory/reg\n");
            goto fail;
        }
    }

    if (binfo->kernel_cmdline && *binfo->kernel_cmdline) {
        int i;
        char args[1024];
        const char *part;

        memset(args, 0, sizeof(args));
        for (i = 0; i < 3; ++i) {
            switch (i) {
            case 0:
                part = qemu_fdt_getprop_string(fdt, "/chosen", "bootargs", 0,
                                               false, NULL);
                break;
            case 1:
                part = " ";
                break;
            case 2:
                part = binfo->kernel_cmdline;
                break;
            }
            if (part) {
                strcat(args, part);
            }
        }
        rc = qemu_fdt_setprop_string(fdt, "/chosen", "bootargs", args);
        if (rc < 0) {
            fprintf(stderr, "couldn't set /chosen/bootargs\n");
            goto fail;
        }
    }

    if (binfo->initrd_size) {
        rc = qemu_fdt_setprop_cell(fdt, "/chosen", "linux,initrd-start",
                                   binfo->initrd_start);
        if (rc < 0) {
            fprintf(stderr, "couldn't set /chosen/linux,initrd-start\n");
            goto fail;
        }

        rc = qemu_fdt_setprop_cell(fdt, "/chosen", "linux,initrd-end",
                                   binfo->initrd_start + binfo->initrd_size);
        if (rc < 0) {
            fprintf(stderr, "couldn't set /chosen/linux,initrd-end\n");
            goto fail;
        }
    }

    if (binfo->modify_dtb) {
        binfo->modify_dtb(binfo, fdt);
    }

    qemu_fdt_dumpdtb(fdt, size);

    /* Put the DTB into the memory map as a ROM image: this will ensure
     * the DTB is copied again upon reset, even if addr points into RAM.
     */
    rom_add_blob_fixed("dtb", fdt, size, addr);

    g_free(fdt);

    return size;

fail:
    g_free(fdt);
    return -1;
}

static int do_linux_dev_init(Object *obj, void *opaue)
{
    if (object_dynamic_cast(obj, TYPE_LINUX_DEVICE)) {
        LinuxDevice *ld = LINUX_DEVICE(obj);
        LinuxDeviceClass *ldc = LINUX_DEVICE_GET_CLASS(obj);

        if (ldc->linux_init) {
            ldc->linux_init(ld);
        }
    }
    return 0;
}

static void do_cpu_reset(void *opaque)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;
    const struct arm_boot_info *info = env->boot_info;

    cpu_reset(CPU(cpu));
    if (info) {
        if (!info->is_linux) {
            /* Jump to the entry point.  */
            if (env->aarch64) {
                env->pc = info->entry;
            } else {
                env->regs[15] = info->entry & 0xfffffffe;
                env->thumb = info->entry & 1;
            }
        } else {
            /* If we are booting Linux then we need to check whether we are
             * booting into secure or non-secure state and adjust the state
             * accordingly.  Out of reset, ARM is defined to be in secure state
             * (SCR.NS = 0), we change that here if non-secure boot has been
             * requested.
             */
            if (arm_feature(env, ARM_FEATURE_EL3) && !info->secure_boot) {
                env->cp15.scr_el3 |= SCR_NS;
            }

            if (CPU(cpu) == first_cpu) {
                if (env->aarch64) {
                    env->pc = info->loader_start;
                    env->pstate = PSTATE_MODE_EL1h;
                } else {
                    env->regs[15] = info->loader_start;
                }

                if (!have_dtb(info)) {
                    if (old_param) {
                        set_kernel_args_old(info);
                    } else {
                        set_kernel_args(info);
                    }
                }
            } else {
                info->secondary_cpu_reset_hook(cpu, info);
            }
            /* FIXME: Be less brute force */
            /* FIXME: this create deps on reset ordering */
            object_child_foreach_recursive(object_get_root(),
                                           do_linux_dev_init, NULL);
        }
    }

}

/**
 * load_image_to_fw_cfg() - Load an image file into an fw_cfg entry identified
 *                          by key.
 * @fw_cfg:         The firmware config instance to store the data in.
 * @size_key:       The firmware config key to store the size of the loaded
 *                  data under, with fw_cfg_add_i32().
 * @data_key:       The firmware config key to store the loaded data under,
 *                  with fw_cfg_add_bytes().
 * @image_name:     The name of the image file to load. If it is NULL, the
 *                  function returns without doing anything.
 * @try_decompress: Whether the image should be decompressed (gunzipped) before
 *                  adding it to fw_cfg. If decompression fails, the image is
 *                  loaded as-is.
 *
 * In case of failure, the function prints an error message to stderr and the
 * process exits with status 1.
 */
static void load_image_to_fw_cfg(FWCfgState *fw_cfg, uint16_t size_key,
                                 uint16_t data_key, const char *image_name,
                                 bool try_decompress)
{
    size_t size = -1;
    uint8_t *data;

    if (image_name == NULL) {
        return;
    }

    if (try_decompress) {
        size = load_image_gzipped_buffer(image_name,
                                         LOAD_IMAGE_MAX_GUNZIP_BYTES, &data);
    }

    if (size == (size_t)-1) {
        gchar *contents;
        gsize length;

        if (!g_file_get_contents(image_name, &contents, &length, NULL)) {
            fprintf(stderr, "failed to load \"%s\"\n", image_name);
            exit(1);
        }
        size = length;
        data = (uint8_t *)contents;
    }

    fw_cfg_add_i32(fw_cfg, size_key, size);
    fw_cfg_add_bytes(fw_cfg, data_key, data, size);
}

void arm_load_kernel(ARMCPU *cpu, struct arm_boot_info *info)
{
    CPUState *cs;
    int kernel_size;
    int initrd_size;
    int is_linux = 0;
    uint64_t elf_entry;
    int elf_machine;
    hwaddr entry, kernel_load_offset;
    int big_endian;
    static const ARMInsnFixup *primary_loader;
    hwaddr alloc_start;

    /* CPU objects (unlike devices) are not automatically reset on system
     * reset, so we must always register a handler to do so. If we're
     * actually loading a kernel, the handler is also responsible for
     * arranging that we start it correctly.
     */
    for (cs = CPU(cpu); cs; cs = CPU_NEXT(cs)) {
        qemu_register_reset(do_cpu_reset, ARM_CPU(cs));
    }

    /* Load the kernel.  */
    if (!info->kernel_filename || info->firmware_loaded) {

        if (have_dtb(info)) {
            /* If we have a device tree blob, but no kernel to supply it to (or
             * the kernel is supposed to be loaded by the bootloader), copy the
             * DTB to the base of RAM for the bootloader to pick up.
             */
            if (load_dtb(info->loader_start, info, 0) < 0) {
                exit(1);
            }
        }

        if (info->kernel_filename) {
            FWCfgState *fw_cfg;
            bool try_decompressing_kernel;

            fw_cfg = fw_cfg_find();
            try_decompressing_kernel = arm_feature(&cpu->env,
                                                   ARM_FEATURE_AARCH64);

            /* Expose the kernel, the command line, and the initrd in fw_cfg.
             * We don't process them here at all, it's all left to the
             * firmware.
             */
            load_image_to_fw_cfg(fw_cfg,
                                 FW_CFG_KERNEL_SIZE, FW_CFG_KERNEL_DATA,
                                 info->kernel_filename,
                                 try_decompressing_kernel);
            load_image_to_fw_cfg(fw_cfg,
                                 FW_CFG_INITRD_SIZE, FW_CFG_INITRD_DATA,
                                 info->initrd_filename, false);

            if (info->kernel_cmdline) {
                fw_cfg_add_i32(fw_cfg, FW_CFG_CMDLINE_SIZE,
                               strlen(info->kernel_cmdline) + 1);
                fw_cfg_add_string(fw_cfg, FW_CFG_CMDLINE_DATA,
                                  info->kernel_cmdline);
            }
        }

        /* We will start from address 0 (typically a boot ROM image) in the
         * same way as hardware.
         */
        return;
    }

    if (arm_feature(&cpu->env, ARM_FEATURE_AARCH64)) {
        primary_loader = bootloader_aarch64;
        kernel_load_offset = KERNEL64_LOAD_ADDR;
        elf_machine = EM_AARCH64;
    } else {
        primary_loader = bootloader;
        kernel_load_offset = KERNEL_LOAD_ADDR;
        elf_machine = EM_ARM;
    }

    info->dtb_filename = qemu_opt_get(qemu_get_machine_opts(), "dtb");
    is_linux = object_property_get_bool(OBJECT(qdev_get_machine()),
                                        "linux", NULL);

    if (!info->secondary_cpu_reset_hook) {
        info->secondary_cpu_reset_hook = default_reset_secondary;
    }
    if (!info->write_secondary_boot) {
        info->write_secondary_boot = default_write_secondary;
    }

    if (info->nb_cpus == 0)
        info->nb_cpus = 1;

#ifdef TARGET_WORDS_BIGENDIAN
    big_endian = 1;
#else
    big_endian = 0;
#endif

    /* Assume that raw images are linux kernels, and ELF images are not.  */
    kernel_size = load_elf(info->kernel_filename, NULL, NULL, &elf_entry,
                           NULL, NULL, big_endian, elf_machine, 1);
    entry = elf_entry;

    /* We want to put the initrd far enough into RAM that when the
     * kernel is uncompressed it will not clobber the initrd. However
     * on boards without much RAM we must ensure that we still leave
     * enough room for a decent sized initrd, and on boards with large
     * amounts of RAM we must avoid the initrd being so far up in RAM
     * that it is outside lowmem and inaccessible to the kernel.
     * So for boards with less  than 256MB of RAM we put the initrd
     * halfway into RAM, and for boards with 256MB of RAM or more we put
     * the initrd at 128MB.
     */
    alloc_start = ((kernel_size >= 0) ? QEMU_ALIGN_UP(elf_entry, 4096) :
                   info->loader_start) + MIN(info->ram_size / 2,
                   128 * 1024 * 1024);

    if (kernel_size < 0) {
        kernel_size = load_uimage(info->kernel_filename, &entry, NULL,
                                  &is_linux, NULL, NULL);
    }
    /* On aarch64, it's the bootloader's job to uncompress the kernel. */
    if (arm_feature(&cpu->env, ARM_FEATURE_AARCH64) && kernel_size < 0) {
        entry = info->loader_start + kernel_load_offset;
        kernel_size = load_image_gzipped(info->kernel_filename, entry,
                                         info->ram_size - kernel_load_offset);
        is_linux = 1;
    }
    if (kernel_size < 0) {
        entry = info->loader_start + kernel_load_offset;
        kernel_size = load_image_targphys(info->kernel_filename, entry,
                                          info->ram_size - kernel_load_offset);
        is_linux = 1;
    }
    if (kernel_size < 0) {
        fprintf(stderr, "qemu: could not load kernel '%s'\n",
                info->kernel_filename);
        exit(1);
    }
    info->entry = entry;
    if (is_linux) {
        uint32_t fixupcontext[FIXUP_MAX];

        if (info->initrd_filename) {
            initrd_size = load_ramdisk(info->initrd_filename,
                                       alloc_start,
                                       info->ram_size -
                                       alloc_start);
            if (initrd_size < 0) {
                initrd_size = load_image_targphys(info->initrd_filename,
                                                  alloc_start,
                                                  info->ram_size -
                                                  alloc_start);
            }
            if (initrd_size < 0) {
                fprintf(stderr, "qemu: could not load initrd '%s'\n",
                        info->initrd_filename);
                exit(1);
            }
        } else {
            initrd_size = 0;
        }
        info->initrd_start = alloc_start;
        info->initrd_size = initrd_size;
        alloc_start += initrd_size;
        /* Some kernels will trash anything in the 4K page the initrd
         * ends in, so make sure the anything else isn't caught up in that.
         */
        alloc_start = QEMU_ALIGN_UP(alloc_start, 4096);

        fixupcontext[FIXUP_BOARDID] = info->board_id;

        /* for device tree boot, we pass the DTB directly in r2. Otherwise
         * we point to the kernel args.
         */
        if (have_dtb(info)) {
            int dtb_size = load_dtb(alloc_start, info, 0);
            if (dtb_size < 0) {
                exit(1);
            }
            fixupcontext[FIXUP_ARGPTR] = alloc_start;
            alloc_start += dtb_size;
            alloc_start = QEMU_ALIGN_UP(alloc_start, 4096);
        } else {
            fixupcontext[FIXUP_ARGPTR] = info->loader_start + KERNEL_ARGS_ADDR;
            if (info->ram_size >= (1ULL << 32)) {
                fprintf(stderr, "qemu: RAM size must be less than 4GB to boot"
                        " Linux kernel using ATAGS (try passing a device tree"
                        " using -dtb)\n");
                exit(1);
            }
        }
        fixupcontext[FIXUP_ENTRYPOINT] = entry;

        fixupcontext[FIXUP_EL] = 1;
        if (arm_feature(&cpu->env, ARM_FEATURE_EL2)) {
            fixupcontext[FIXUP_EL] = 2;
        }

        write_bootloader("bootloader", info->loader_start,
                         primary_loader, fixupcontext);

        if (info->nb_cpus > 1) {
            info->write_secondary_boot(cpu, info);
        }
    }
    info->is_linux = is_linux;

    for (cs = CPU(cpu); cs; cs = CPU_NEXT(cs)) {
        ARM_CPU(cs)->env.boot_info = info;
    }
}
