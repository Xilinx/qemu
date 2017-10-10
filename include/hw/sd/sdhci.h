/*
 * SD Association Host Standard Specification v2.0 controller emulation
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Mitsyanko Igor <i.mitsyanko@samsung.com>
 * Peter A.G. Crosthwaite <peter.crosthwaite@petalogix.com>
 *
 * Based on MMC controller for Samsung S5PC1xx-based board emulation
 * by Alexey Merkulov and Vladimir Monakhov.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU _General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SDHCI_H
#define SDHCI_H

#include "qemu-common.h"
#include "hw/block/block.h"
#include "hw/pci/pci.h"
#include "hw/sysbus.h"
#include "hw/sd/sd.h"

/* Default SD/MMC host controller features information, which will be
 * presented in CAPABILITIES register of generic SD host controller at reset.
 * If not stated otherwise:
 * 0 - not supported, 1 - supported, other - prohibited.
 */
#define SDHC_CAPAB_DRIVER_D       1ull       /* Driver type D support */
#define SDHC_CAPAB_DRIVER_C       1ull       /* Driver type C support */
#define SDHC_CAPAB_DRIVER_A       1ull       /* Driver type A support */
#define SDHC_CAPAB_DDR50          1ull       /* DDR50 support */
#define SDHC_CAPAB_SDR104         1ull       /* SDR104 support */
#define SDHC_CAPAB_SDR50          1ull       /* SDR50 support */
#define SDHC_CAPAB_64BITBUS       0ul        /* 64-bit System Bus Support */
#define SDHC_CAPAB_18V            1ul        /* Voltage support 1.8v */
#define SDHC_CAPAB_30V            0ul        /* Voltage support 3.0v */
#define SDHC_CAPAB_33V            1ul        /* Voltage support 3.3v */
#define SDHC_CAPAB_SUSPRESUME     0ul        /* Suspend/resume support */
#define SDHC_CAPAB_SDMA           1ul        /* SDMA support */
#define SDHC_CAPAB_HIGHSPEED      1ul        /* High speed support */
#define SDHC_CAPAB_ADMA1          1ul        /* ADMA1 support */
#define SDHC_CAPAB_ADMA2          1ul        /* ADMA2 support */
/* Maximum host controller R/W buffers size
 * Possible values: 512, 1024, 2048 bytes */
#define SDHC_CAPAB_MAXBLOCKLENGTH 512ul
/* Maximum clock frequency for SDclock in MHz
 * value in range 10-63 MHz, 0 - not defined */
#define SDHC_CAPAB_BASECLKFREQ    52ul
#define SDHC_CAPAB_TOUNIT         1ul  /* Timeout clock unit 0 - kHz, 1 - MHz */
/* Timeout clock frequency 1-63, 0 - not defined */
#define SDHC_CAPAB_TOCLKFREQ      52ul

/* Now check all parameters and calculate CAPABILITIES REGISTER value */
#if SDHC_CAPAB_64BITBUS > 1 || SDHC_CAPAB_18V > 1 || SDHC_CAPAB_30V > 1 ||     \
    SDHC_CAPAB_33V > 1 || SDHC_CAPAB_SUSPRESUME > 1 || SDHC_CAPAB_SDMA > 1 ||  \
    SDHC_CAPAB_HIGHSPEED > 1 || SDHC_CAPAB_ADMA2 > 1 || SDHC_CAPAB_ADMA1 > 1 ||\
    SDHC_CAPAB_TOUNIT > 1
#error Capabilities features can have value 0 or 1 only!
#endif

#if SDHC_CAPAB_MAXBLOCKLENGTH == 512
#define MAX_BLOCK_LENGTH 0ul
#elif SDHC_CAPAB_MAXBLOCKLENGTH == 1024
#define MAX_BLOCK_LENGTH 1ul
#elif SDHC_CAPAB_MAXBLOCKLENGTH == 2048
#define MAX_BLOCK_LENGTH 2ul
#else
#error Max host controller block size can have value 512, 1024 or 2048 only!
#endif

#if (SDHC_CAPAB_BASECLKFREQ > 0 && SDHC_CAPAB_BASECLKFREQ < 10) || \
    SDHC_CAPAB_BASECLKFREQ > 63
#error SDclock frequency can have value in range 0, 10-63 only!
#endif

#if SDHC_CAPAB_TOCLKFREQ > 63
#error Timeout clock frequency can have value in range 0-63 only!
#endif

#define SDHC_CAPAB_REG_DEFAULT                                 \
   ((SDHC_CAPAB_DRIVER_D << 38) | (SDHC_CAPAB_DRIVER_C << 37) |\
    (SDHC_CAPAB_DRIVER_A << 36) | (SDHC_CAPAB_DDR50 << 34) |   \
    (SDHC_CAPAB_SDR104 << 33) | (SDHC_CAPAB_SDR50 << 32) |     \
    (SDHC_CAPAB_64BITBUS << 28) | (SDHC_CAPAB_18V << 26) |     \
    (SDHC_CAPAB_30V << 25) | (SDHC_CAPAB_33V << 24) |          \
    (SDHC_CAPAB_SUSPRESUME << 23) | (SDHC_CAPAB_SDMA << 22) |  \
    (SDHC_CAPAB_HIGHSPEED << 21) | (SDHC_CAPAB_ADMA1 << 20) |  \
    (SDHC_CAPAB_ADMA2 << 19) | (MAX_BLOCK_LENGTH << 16) |      \
    (SDHC_CAPAB_BASECLKFREQ << 8) | (SDHC_CAPAB_TOUNIT << 7) | \
    (SDHC_CAPAB_TOCLKFREQ))

#define MASK_TRNMOD     0x0037

/* SD/MMC host controller state */
typedef struct SDHCIState {
    union {
        PCIDevice pcidev;
        SysBusDevice busdev;
    };
    SDBus sdbus;
    MemoryRegion iomem;
    BlockBackend *blk;
    MemoryRegion *dma_mr;
    AddressSpace *dma_as;

    QEMUTimer *insert_timer;       /* timer for 'changing' sd card. */
    QEMUTimer *transfer_timer;
    qemu_irq eject_cb;
    qemu_irq ro_cb;
    qemu_irq irq;

    uint32_t sdmasysad;    /* SDMA System Address register */
    uint16_t blksize;      /* Host DMA Buff Boundary and Transfer BlkSize Reg */
    uint16_t blkcnt;       /* Blocks count for current transfer */
    uint32_t argument;     /* Command Argument Register */
    uint16_t trnmod;       /* Transfer Mode Setting Register */
    uint16_t cmdreg;       /* Command Register */
    uint32_t rspreg[4];    /* Response Registers 0-3 */
    uint32_t prnsts;       /* Present State Register */
    uint8_t  hostctl;      /* Host Control Register */
    uint8_t  pwrcon;       /* Power control Register */
    uint8_t  blkgap;       /* Block Gap Control Register */
    uint8_t  wakcon;       /* WakeUp Control Register */
    uint16_t clkcon;       /* Clock control Register */
    uint8_t  timeoutcon;   /* Timeout Control Register */
    uint8_t  admaerr;      /* ADMA Error Status Register */
    uint16_t norintsts;    /* Normal Interrupt Status Register */
    uint16_t errintsts;    /* Error Interrupt Status Register */
    uint16_t norintstsen;  /* Normal Interrupt Status Enable Register */
    uint16_t errintstsen;  /* Error Interrupt Status Enable Register */
    uint16_t norintsigen;  /* Normal Interrupt Signal Enable Register */
    uint16_t errintsigen;  /* Error Interrupt Signal Enable Register */
    uint16_t acmd12errsts; /* Auto CMD12 error status register */
    uint16_t hostctl2;     /* Host Control 2 */
    uint64_t admasysaddr;  /* ADMA System Address Register */

    uint32_t capareg;      /* Capabilities Register */
    uint32_t maxcurr;      /* Maximum Current Capabilities Register */
    uint8_t  *fifo_buffer; /* SD host i/o FIFO buffer */
    uint32_t buf_maxsz;
    uint16_t data_count;   /* current element in FIFO buffer */
    uint8_t  stopped_state;/* Current SDHC state */
    bool     pending_insert_quirk;/* Quirk for Raspberry Pi card insert int */
    bool     pending_insert_state;
    /* Buffer Data Port Register - virtual access point to R and W buffers */
    /* Software Reset Register - always reads as 0 */
    /* Force Event Auto CMD12 Error Interrupt Reg - write only */
    /* Force Event Error Interrupt Register- write only */
    /* RO Host Controller Version Register always reads as 0x2401 */
} SDHCIState;

#define TYPE_PCI_SDHCI "sdhci-pci"
#define PCI_SDHCI(obj) OBJECT_CHECK(SDHCIState, (obj), TYPE_PCI_SDHCI)

#define TYPE_SYSBUS_SDHCI "generic-sdhci"
#define SYSBUS_SDHCI(obj)                               \
     OBJECT_CHECK(SDHCIState, (obj), TYPE_SYSBUS_SDHCI)

#endif /* SDHCI_H */
