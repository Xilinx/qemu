/*
 * Logging support
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu-common.h"
#include "qemu/log.h"
#include "exec/gdbstub.h"

static char *logfilename;
FILE *qemu_logfile;
int qemu_loglevel = 0;
int qemu_logmask = 0;
static int log_append = 0;

void qemu_log(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    if (qemu_logfile) {
        vfprintf(qemu_logfile, fmt, ap);
    }
    va_end(ap);
}

void qemu_log_mask(int mask, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    if ((qemu_logmask & mask) && qemu_logfile) {
        vfprintf(qemu_logfile, fmt, ap);
    }
    va_end(ap);

    /*
     * Break the GDB session (if connected) so that the user can inspect the
     * guest state.
     *
     * TODO: Consider conditionalizing this on a cmdline option.
     */
    if (qemu_logmask & mask & LOG_GUEST_ERROR) {
        char *msg;

        va_start(ap, fmt);
        if (vasprintf(&msg, fmt, ap) < 0) {
            msg = NULL;
        }
        va_end(ap);

        gdbserver_break(msg);
        g_free(msg);
    }
}

void qemu_log_mask_level(int mask, int level, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    if ((qemu_loglevel >= level) && (qemu_logmask & mask) && qemu_logfile) {
        vfprintf(qemu_logfile, fmt, ap);
    }
    va_end(ap);

    /*
     * Break the GDB session (if connected) so that the user can inspect the
     * guest state.
     *
     * TODO: Consider conditionalizing this on a cmdline option.
     */
    if ((qemu_loglevel >= level) && (qemu_logmask & mask & LOG_GUEST_ERROR)) {
        char *msg;

        va_start(ap, fmt);
        if (vasprintf(&msg, fmt, ap) < 0) {
            msg = NULL;
        }
        va_end(ap);

        gdbserver_break(msg);
        g_free(msg);
    }
}

int do_qemu_setup_log_args(const char *str, bool use_own_buffers)
{
    int mask, level;
    mask = qemu_str_to_log_mask(str, false);
    level = qemu_str_to_log_mask(str, true);

    if (mask) {
        do_qemu_set_log(mask, level, use_own_buffers);
    }
    return mask;
}

/* enable or disable low levels log */
void do_qemu_set_log(int log_flags, int log_level, bool use_own_buffers)
{
    qemu_logmask = log_flags;
    qemu_loglevel = log_level;
    if (qemu_logmask && !qemu_logfile) {
        if (logfilename) {
            qemu_logfile = fopen(logfilename, log_append ? "a" : "w");
            if (!qemu_logfile) {
                perror(logfilename);
                _exit(1);
            }
        } else {
            /* Default to stderr if no log file specified */
            qemu_logfile = stderr;
        }
        /* must avoid mmap() usage of glibc by setting a buffer "by hand" */
        if (use_own_buffers) {
            static char logfile_buf[4096];

            setvbuf(qemu_logfile, logfile_buf, _IOLBF, sizeof(logfile_buf));
        } else {
#if defined(_WIN32)
            /* Win32 doesn't support line-buffering, so use unbuffered output. */
            setvbuf(qemu_logfile, NULL, _IONBF, 0);
#else
            setvbuf(qemu_logfile, NULL, _IOLBF, 0);
#endif
            log_append = 1;
        }
    }
    if (!qemu_logmask && qemu_logfile) {
        qemu_log_close();
    }
}

void qemu_set_log_filename(const char *filename)
{
    g_free(logfilename);
    logfilename = g_strdup(filename);
    qemu_log_close();
    qemu_set_log_level(qemu_logmask, qemu_loglevel);
}

const QEMULogItem qemu_log_items[] = {
    { CPU_LOG_TB_OUT_ASM, "out_asm",
      "show generated host assembly code for each compiled TB" },
    { CPU_LOG_TB_IN_ASM, "in_asm",
      "show target assembly code for each compiled TB" },
    { CPU_LOG_TB_OP, "op",
      "show micro ops for each compiled TB" },
    { CPU_LOG_TB_OP_OPT, "op_opt",
      "show micro ops (x86 only: before eflags optimization) and\n"
      "after liveness analysis" },
    { CPU_LOG_INT, "int",
      "show interrupts/exceptions in short format" },
    { CPU_LOG_EXEC, "exec",
      "show trace before each executed TB (lots of logs)" },
    { CPU_LOG_TB_CPU, "cpu",
      "show CPU state before block translation" },
    { CPU_LOG_MMU, "mmu",
      "log MMU-related activities" },
    { CPU_LOG_PCALL, "pcall",
      "x86 only: show protected mode far calls/returns/exceptions" },
    { CPU_LOG_RESET, "cpu_reset",
      "x86 only: show CPU state before CPU resets" },
    { CPU_LOG_IOPORT, "ioport",
      "show all i/o ports accesses" },
    { LOG_UNIMP, "unimp",
      "log unimplemented functionality" },
    { LOG_GUEST_ERROR, "guest_errors",
      "log when the guest OS does something invalid (eg accessing a\n"
      "non-existent register)" },

    { LOG_FDT, "fdt", "log Device Tree info." },
    { LOG_PM,  "pm", "log Power Management info." },

    /* device entries */
    { DEV_LOG_NET_DEV, "net-dev", "enable Network Device logs." },
    { DEV_LOG_NAND, "nand", "enable NAND log." },
    { DEV_LOG_NANDC, "nandc", "enable NAND Controller log." },
    { DEV_LOG_SD, "sd", "enable SD/MMC card log." },
    { DEV_LOG_SDHCI, "sdhci", "enable SDHCI log." },
    { DEV_LOG_SPI, "spi", "enable SPI controller log." },
    { DEV_LOG_SPI_DEV, "spi-dev", "enable SPI device logs." },

    { 0, NULL, NULL },
};

static int cmp1(const char *s1, int n, const char *s2)
{
    if (strlen(s2) != n) {
        return 0;
    }
    return memcmp(s1, s2, n) == 0;
}

/* takes a comma separated list of log masks. Return 0 if error. */
int qemu_str_to_log_mask(const char *str, bool lvl)
{
    const QEMULogItem *item;
    int mask;
    int level;
    const char *p, *p1, *p2;

    p = str;
    mask = 0;
    level = 0;
    for (;;) {
        p1 = strchr(p, ',');
        if (!p1) {
            p1 = p + strlen(p);
        }
        p2 = strchr(p, '=');
        if (!p2 || p2 > p1) {
            /* past end of mask token */
            p2 = NULL;
        }
        if (cmp1(p,p1-p,"all")) {
            for (item = qemu_log_items; item->mask != 0; item++) {
                mask |= item->mask;
            }
        } else if (p2 && cmp1(p, p2 - p, "loglevel")) {
            if (p2 + 1 == p1) {
                return 0;
            }
            char* endptr = NULL;
            level = (int)strtoul(p2 + 1, &endptr, 10);
            if (endptr != p1) {
                return 0;
            }
            goto skip;
        } else {
            for (item = qemu_log_items; item->mask != 0; item++) {
                if (cmp1(p, p1 - p, item->name)) {
                    goto found;
                }
            }
            return 0;
        }
    found:
        mask |= item->mask;
    skip:
        if (*p1 != ',') {
            break;
        }
        p = p1 + 1;
    }
    if (lvl) {
        return level;
    }
    return mask;
}

void qemu_print_log_usage(FILE *f)
{
    const QEMULogItem *item;
    fprintf(f, "Log items (comma separated):\n");
    for (item = qemu_log_items; item->mask != 0; item++) {
        fprintf(f, "%-10s %s\n", item->name, item->help);
    }

    fprintf(f, "\nloglevel=<level> Set the level of log output.\n");
}
