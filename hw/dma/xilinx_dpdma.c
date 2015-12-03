/*
 * xilinx_dpdma.c
 *
 *  Copyright (C) 2015 : GreenSocs Ltd
 *      http://www.greensocs.com/ , email: info@greensocs.com
 *
 *  Developed by :
 *  Frederic Konrad   <fred.konrad@greensocs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "xilinx_dpdma.h"

#ifndef DEBUG_DPDMA
#define DEBUG_DPDMA 0
#endif

#define DPRINTF(fmt, ...) do {                                                 \
    if (DEBUG_DPDMA) {                                                         \
        qemu_log("xilinx_dpdma: " fmt , ## __VA_ARGS__);                       \
    }                                                                          \
} while (0);

/*
 * Registers offset for DPDMA.
 */
#define DPDMA_ERR_CTRL              (0x00000000)
#define DPDMA_ISR                   (0x00000004 >> 2)
#define DPDMA_IMR                   (0x00000008 >> 2)
#define DPDMA_IEN                   (0x0000000C >> 2)
#define DPDMA_IDS                   (0x00000010 >> 2)
#define DPDMA_EISR                  (0x00000014 >> 2)
#define DPDMA_EIMR                  (0x00000018 >> 2)
#define DPDMA_EIEN                  (0x0000001C >> 2)
#define DPDMA_EIDS                  (0x00000020 >> 2)
#define DPDMA_CNTL                  (0x00000100 >> 2)
#define DPDMA_GBL                   (0x00000104 >> 2)
#define DPDMA_ALC0_CNTL             (0x00000108 >> 2)
#define DPDMA_ALC0_STATUS           (0x0000010C >> 2)
#define DPDMA_ALC0_MAX              (0x00000110 >> 2)
#define DPDMA_ALC0_MIN              (0x00000114 >> 2)
#define DPDMA_ALC0_ACC              (0x00000118 >> 2)
#define DPDMA_ALC0_ACC_TRAN         (0x0000011C >> 2)
#define DPDMA_ALC1_CNTL             (0x00000120 >> 2)
#define DPDMA_ALC1_STATUS           (0x00000124 >> 2)
#define DPDMA_ALC1_MAX              (0x00000128 >> 2)
#define DPDMA_ALC1_MIN              (0x0000012C >> 2)
#define DPDMA_ALC1_ACC              (0x00000130 >> 2)
#define DPDMA_ALC1_ACC_TRAN         (0x00000134 >> 2)
#define DPDMA_CH0_DSCR_STRT_ADDRE   (0x00000200 >> 2)
#define DPDMA_CH0_DSCR_STRT_ADDR    (0x00000204 >> 2)
#define DPDMA_CH0_DSCR_NEXT_ADDRE   (0x00000208 >> 2)
#define DPDMA_CH0_DSCR_NEXT_ADDR    (0x0000020C >> 2)
#define DPDMA_CH0_PYLD_CUR_ADDRE    (0x00000210 >> 2)
#define DPDMA_CH0_PYLD_CUR_ADDR     (0x00000214 >> 2)
#define DPDMA_CH0_CNTL              (0x00000218 >> 2)
#define DPDMA_CH0_STATUS            (0x0000021C >> 2)
#define DPDMA_CH0_VDO               (0x00000220 >> 2)
#define DPDMA_CH0_PYLD_SZ           (0x00000224 >> 2)
#define DPDMA_CH0_DSCR_ID           (0x00000228 >> 2)
#define DPDMA_CH1_DSCR_STRT_ADDRE   (0x00000300 >> 2)
#define DPDMA_CH1_DSCR_STRT_ADDR    (0x00000304 >> 2)
#define DPDMA_CH1_DSCR_NEXT_ADDRE   (0x00000308 >> 2)
#define DPDMA_CH1_DSCR_NEXT_ADDR    (0x0000030C >> 2)
#define DPDMA_CH1_PYLD_CUR_ADDRE    (0x00000310 >> 2)
#define DPDMA_CH1_PYLD_CUR_ADDR     (0x00000314 >> 2)
#define DPDMA_CH1_CNTL              (0x00000318 >> 2)
#define DPDMA_CH1_STATUS            (0x0000031C >> 2)
#define DPDMA_CH1_VDO               (0x00000320 >> 2)
#define DPDMA_CH1_PYLD_SZ           (0x00000324 >> 2)
#define DPDMA_CH1_DSCR_ID           (0x00000328 >> 2)
#define DPDMA_CH2_DSCR_STRT_ADDRE   (0x00000400 >> 2)
#define DPDMA_CH2_DSCR_STRT_ADDR    (0x00000404 >> 2)
#define DPDMA_CH2_DSCR_NEXT_ADDRE   (0x00000408 >> 2)
#define DPDMA_CH2_DSCR_NEXT_ADDR    (0x0000040C >> 2)
#define DPDMA_CH2_PYLD_CUR_ADDRE    (0x00000410 >> 2)
#define DPDMA_CH2_PYLD_CUR_ADDR     (0x00000414 >> 2)
#define DPDMA_CH2_CNTL              (0x00000418 >> 2)
#define DPDMA_CH2_STATUS            (0x0000041C >> 2)
#define DPDMA_CH2_VDO               (0x00000420 >> 2)
#define DPDMA_CH2_PYLD_SZ           (0x00000424 >> 2)
#define DPDMA_CH2_DSCR_ID           (0x00000428 >> 2)
#define DPDMA_CH3_DSCR_STRT_ADDRE   (0x00000500 >> 2)
#define DPDMA_CH3_DSCR_STRT_ADDR    (0x00000504 >> 2)
#define DPDMA_CH3_DSCR_NEXT_ADDRE   (0x00000508 >> 2)
#define DPDMA_CH3_DSCR_NEXT_ADDR    (0x0000050C >> 2)
#define DPDMA_CH3_PYLD_CUR_ADDRE    (0x00000510 >> 2)
#define DPDMA_CH3_PYLD_CUR_ADDR     (0x00000514 >> 2)
#define DPDMA_CH3_CNTL              (0x00000518 >> 2)
#define DPDMA_CH3_STATUS            (0x0000051C >> 2)
#define DPDMA_CH3_VDO               (0x00000520 >> 2)
#define DPDMA_CH3_PYLD_SZ           (0x00000524 >> 2)
#define DPDMA_CH3_DSCR_ID           (0x00000528 >> 2)
#define DPDMA_CH4_DSCR_STRT_ADDRE   (0x00000600 >> 2)
#define DPDMA_CH4_DSCR_STRT_ADDR    (0x00000604 >> 2)
#define DPDMA_CH4_DSCR_NEXT_ADDRE   (0x00000608 >> 2)
#define DPDMA_CH4_DSCR_NEXT_ADDR    (0x0000060C >> 2)
#define DPDMA_CH4_PYLD_CUR_ADDRE    (0x00000610 >> 2)
#define DPDMA_CH4_PYLD_CUR_ADDR     (0x00000614 >> 2)
#define DPDMA_CH4_CNTL              (0x00000618 >> 2)
#define DPDMA_CH4_STATUS            (0x0000061C >> 2)
#define DPDMA_CH4_VDO               (0x00000620 >> 2)
#define DPDMA_CH4_PYLD_SZ           (0x00000624 >> 2)
#define DPDMA_CH4_DSCR_ID           (0x00000628 >> 2)
#define DPDMA_CH5_DSCR_STRT_ADDRE   (0x00000700 >> 2)
#define DPDMA_CH5_DSCR_STRT_ADDR    (0x00000704 >> 2)
#define DPDMA_CH5_DSCR_NEXT_ADDRE   (0x00000708 >> 2)
#define DPDMA_CH5_DSCR_NEXT_ADDR    (0x0000070C >> 2)
#define DPDMA_CH5_PYLD_CUR_ADDRE    (0x00000710 >> 2)
#define DPDMA_CH5_PYLD_CUR_ADDR     (0x00000714 >> 2)
#define DPDMA_CH5_CNTL              (0x00000718 >> 2)
#define DPDMA_CH5_STATUS            (0x0000071C >> 2)
#define DPDMA_CH5_VDO               (0x00000720 >> 2)
#define DPDMA_CH5_PYLD_SZ           (0x00000724 >> 2)
#define DPDMA_CH5_DSCR_ID           (0x00000728 >> 2)
#define DPDMA_ECO                   (0x00000FFC >> 2)

/*
 * Descriptor control field.
 */
#define CONTROL_PREAMBLE_VALUE      0xA5

#define CONTROL_PREAMBLE            0xFF
#define EN_DSCR_DONE_INTR           (1 << 8)
#define EN_DSCR_UPDATE              (1 << 9)
#define IGNORE_DONE                 (1 << 10)
#define AXI_BURST_TYPE              (1 << 11)
#define AXCACHE                     (0x0F << 12)
#define AXPROT                      (0x2 << 16)
#define DESCRIPTOR_MODE             (1 << 18)
#define LAST_DESCRIPTOR             (1 << 19)
#define ENABLE_CRC                  (1 << 20)
#define LAST_DESCRIPTOR_OF_FRAME    (1 << 21)

typedef enum DPDMABurstType {
    DPDMA_INCR = 0,
    DPDMA_FIXED = 1
} DPDMABurstType;

typedef enum DPDMAMode {
    DPDMA_CONTIGOUS = 0,
    DPDMA_FRAGMENTED = 1
} DPDMAMode;

typedef struct DPDMADescriptor {
    uint32_t control;
    uint32_t descriptor_id;
    /* transfer size in byte. */
    uint32_t xfer_size;
    uint32_t line_size_stride;
    uint32_t timestamp_lsb;
    uint32_t timestamb_msb;
    /* contains extension for both descriptor and source. */
    uint32_t address_extension;
    uint32_t next_descriptor;
    uint32_t source_address;
    uint32_t address_extension_23;
    uint32_t address_extension_45;
    uint32_t source_address2;
    uint32_t source_address3;
    uint32_t source_address4;
    uint32_t source_address5;
    uint32_t crc;
} DPDMADescriptor;

static bool xilinx_dpdma_desc_is_last(DPDMADescriptor *desc)
{
    return ((desc->control & 0x00080000) != 0);
}

static bool xilinx_dpdma_desc_is_last_of_frame(DPDMADescriptor *desc)
{
    return ((desc->control & 0x00200000) != 0);
}

static uint64_t xilinx_dpdma_desc_get_next_descriptor_address(DPDMADescriptor
                                                              *desc)
{
    return desc->next_descriptor
           + ((desc->address_extension & 0x00000FFF) << 8);
}

static uint64_t xilinx_dpdma_desc_get_source_address(DPDMADescriptor *desc,
                                                     uint8_t frag)
{
    uint64_t addr = 0;
    assert(frag < 5);

    switch (frag) {
    case 0:
        addr = desc->source_address
            + (extract32(desc->address_extension, 16, 12) << 20);
        break;
    case 1:
        addr = desc->source_address2
            + (extract32(desc->address_extension_23, 0, 12) << 8);
        break;
    case 2:
        addr = desc->source_address3
            + (extract32(desc->address_extension_23, 16, 12) << 20);
        break;
    case 3:
        addr = desc->source_address4
            + (extract32(desc->address_extension_45, 0, 12) << 8);
        break;
    case 4:
        addr = desc->source_address5
            + (extract32(desc->address_extension_45, 16, 12) << 20);
        break;
    default:
        addr = 0;
        break;
    }

    return addr;
}

static uint32_t xilinx_dpdma_desc_get_transfer_size(DPDMADescriptor *desc)
{
    return desc->xfer_size;
}

static uint32_t xilinx_dpdma_desc_get_line_size(DPDMADescriptor *desc)
{
    return desc->line_size_stride & 0x3FFFF;
}

static uint32_t xilinx_dpdma_desc_get_line_stride(DPDMADescriptor *desc)
{
    return (desc->line_size_stride >> 18) * 16;
}

static inline bool xilinx_dpdma_desc_crc_enabled(DPDMADescriptor *desc)
{
    return ((desc->control & (1 << 20)) != 0);
}

static inline bool xilinx_dpdma_desc_check_crc(DPDMADescriptor *desc)
{
    uint32_t *p = (uint32_t *)(desc);
    uint32_t crc = 0;
    uint8_t i;

    for (i = 0; i < 15; i++) {
        crc += p[i];
    }

    return (crc == desc->crc);
}

static inline bool xilinx_dpdma_desc_completion_interrupt(DPDMADescriptor *desc)
{
    return ((desc->control & (1 << 8)) != 0);
}

static inline bool xilinx_dpdma_desc_is_valid(DPDMADescriptor *desc)
{
    return ((desc->control & 0xFF) == 0xA5);
}

static inline bool xilinx_dpdma_desc_is_contiguous(DPDMADescriptor *desc)
{
    return ((desc->control & 0x00040000) == 0);
}

struct XilinxDPDMAState {
    SysBusDevice parent_obj;

    MemoryRegion *dma_mr;
    AddressSpace *dma_as;
    MemoryRegion iomem;
    uint32_t registers[0x1000 >> 2];
    uint8_t *data[6];

    uint64_t next_desc_addr[6];
    qemu_irq irq;

    bool temp;
};

static const VMStateDescription vmstate_xilinx_dpdma = {
    .name = TYPE_XILINX_DPDMA,
    .version_id = 1,
    .fields = (VMStateField[]) {

        VMSTATE_END_OF_LIST()
    }
};

static void xilinx_dpdma_update_irq(XilinxDPDMAState *s)
{
    bool flags;

    flags = ((s->registers[DPDMA_ISR] & (~s->registers[DPDMA_IMR]))
          || (s->registers[DPDMA_EISR] & (~s->registers[DPDMA_EIMR])));
    qemu_set_irq(s->irq, flags);
}

static uint64_t xilinx_dpdma_descriptor_start_address(XilinxDPDMAState *s,
                                                      uint8_t channel)
{
    switch (channel) {
    case 0:
        return (s->registers[DPDMA_CH0_DSCR_STRT_ADDRE] << 16)
               + s->registers[DPDMA_CH0_DSCR_STRT_ADDR];
        break;
    case 1:
        return (s->registers[DPDMA_CH1_DSCR_STRT_ADDRE] << 16)
               + s->registers[DPDMA_CH1_DSCR_STRT_ADDR];
        break;
    case 2:
        return (s->registers[DPDMA_CH2_DSCR_STRT_ADDRE] << 16)
               + s->registers[DPDMA_CH2_DSCR_STRT_ADDR];
        break;
    case 3:
        return (s->registers[DPDMA_CH3_DSCR_STRT_ADDRE] << 16)
               + s->registers[DPDMA_CH3_DSCR_STRT_ADDR];
        break;
    case 4:
        return (s->registers[DPDMA_CH4_DSCR_STRT_ADDRE] << 16)
               + s->registers[DPDMA_CH4_DSCR_STRT_ADDR];
        break;
    case 5:
        return (s->registers[DPDMA_CH5_DSCR_STRT_ADDRE] << 16)
               + s->registers[DPDMA_CH5_DSCR_STRT_ADDR];
        break;
    default:
        /* Should not happen. */
        return 0;
        break;
    }
}

static bool xilinx_dpdma_is_channel_enabled(XilinxDPDMAState *s,
                                            uint8_t channel)
{
    switch (channel) {
    case 0:
        return ((s->registers[DPDMA_CH0_CNTL] & 0x01) != 0);
        break;
    case 1:
        return ((s->registers[DPDMA_CH1_CNTL] & 0x01) != 0);
        break;
    case 2:
        return ((s->registers[DPDMA_CH2_CNTL] & 0x01) != 0);
        break;
    case 3:
        return ((s->registers[DPDMA_CH3_CNTL] & 0x01) != 0);
        break;
    case 4:
        return ((s->registers[DPDMA_CH4_CNTL] & 0x01) != 0);
        break;
    case 5:
        return ((s->registers[DPDMA_CH5_CNTL] & 0x01) != 0);
        break;
    default:
        /* Should not happen. */
        return 0;
        break;
    }
}

static bool xilinx_dpdma_is_channel_paused(XilinxDPDMAState *s,
                                           uint8_t channel)
{
    switch (channel) {
        case 0:
            return ((s->registers[DPDMA_CH0_CNTL] & 0x2) != 0);
        case 1:
            return ((s->registers[DPDMA_CH1_CNTL] & 0x2) != 0);
        case 2:
            return ((s->registers[DPDMA_CH2_CNTL] & 0x2) != 0);
        case 3:
            return ((s->registers[DPDMA_CH3_CNTL] & 0x2) != 0);
        case 4:
            return ((s->registers[DPDMA_CH4_CNTL] & 0x2) != 0);
        case 5:
            return ((s->registers[DPDMA_CH5_CNTL] & 0x2) != 0);
        default:
            /* Should not happen. */
            return false;
    }
}

static void xilinx_dpdma_pause_channel(XilinxDPDMAState *s,
                                       uint8_t channel)
{
    switch (channel) {
        case 0:
            s->registers[DPDMA_CH0_CNTL] |= 0x2;
        case 1:
            s->registers[DPDMA_CH1_CNTL] |= 0x2;
        case 2:
            s->registers[DPDMA_CH2_CNTL] |= 0x2;
        case 3:
            s->registers[DPDMA_CH3_CNTL] |= 0x2;
        case 4:
            s->registers[DPDMA_CH4_CNTL] |= 0x2;
        case 5:
            s->registers[DPDMA_CH5_CNTL] |= 0x2;
    }
}

#ifdef DEBUG_DPDMA
static void xilinx_dpdma_dump_descriptor(DPDMADescriptor *desc)
{
    uint8_t *p = ((uint8_t *)(desc));
    size_t i;

    qemu_log("DUMP DESCRIPTOR:\n");
    for (i = 0; i < 64; i++) {
        qemu_log(" 0x%2.2X", *p++);
        if (((i + 1) % 4) == 0) {
            qemu_log("\n");
        }
    }
}
#endif

static uint64_t xilinx_dpdma_read(void *opaque, hwaddr offset,
                                  unsigned size)
{
    XilinxDPDMAState *s = XILINX_DPDMA(opaque);
    assert(size == 4);
    assert((offset % 4) == 0);
    offset = offset >> 2;
    DPRINTF("read @%" PRIx64 "\n", offset << 2);

    switch (offset) {
    /*
     * Trying to read a write only register.
     */
    case DPDMA_GBL:
        return 0;
        break;
    default:
        assert(offset <= (0xFFC >> 2));
        return s->registers[offset];
        break;
    }
    return 0;
}

static void xilinx_dpdma_write(void *opaque, hwaddr offset,
                               uint64_t value, unsigned size)
{
    XilinxDPDMAState *s = XILINX_DPDMA(opaque);
    assert(size == 4);
    assert((offset % 4) == 0);
    offset = offset >> 2;
    DPRINTF("write @%" PRIx64 " = 0x%8.8lX\n", offset << 2, value);

    switch (offset) {
    case DPDMA_ISR:
        value = ~value;
        s->registers[DPDMA_ISR] &= value;
        xilinx_dpdma_update_irq(s);
        break;
    case DPDMA_IEN:
        value = ~value;
        s->registers[DPDMA_IMR] &= value;
        break;
    case DPDMA_IDS:
        s->registers[DPDMA_IMR] |= value;
        break;
    case DPDMA_EISR:
        value = ~value;
        s->registers[DPDMA_EISR] &= value;
        xilinx_dpdma_update_irq(s);
        break;
    case DPDMA_EIEN:
        value = ~value;
        s->registers[DPDMA_EIMR] &= value;
        break;
    case DPDMA_EIDS:
        s->registers[DPDMA_EIMR] |= value;
        break;
    case DPDMA_IMR:
    case DPDMA_EIMR:
    case DPDMA_CH0_DSCR_NEXT_ADDRE:
    case DPDMA_CH0_DSCR_NEXT_ADDR:
    case DPDMA_CH1_DSCR_NEXT_ADDRE:
    case DPDMA_CH1_DSCR_NEXT_ADDR:
    case DPDMA_CH2_DSCR_NEXT_ADDRE:
    case DPDMA_CH2_DSCR_NEXT_ADDR:
    case DPDMA_CH3_DSCR_NEXT_ADDRE:
    case DPDMA_CH3_DSCR_NEXT_ADDR:
    case DPDMA_CH4_DSCR_NEXT_ADDRE:
    case DPDMA_CH4_DSCR_NEXT_ADDR:
    case DPDMA_CH5_DSCR_NEXT_ADDRE:
    case DPDMA_CH5_DSCR_NEXT_ADDR:
    case DPDMA_CH0_PYLD_CUR_ADDRE:
    case DPDMA_CH0_PYLD_CUR_ADDR:
    case DPDMA_CH1_PYLD_CUR_ADDRE:
    case DPDMA_CH1_PYLD_CUR_ADDR:
    case DPDMA_CH2_PYLD_CUR_ADDRE:
    case DPDMA_CH2_PYLD_CUR_ADDR:
    case DPDMA_CH3_PYLD_CUR_ADDRE:
    case DPDMA_CH3_PYLD_CUR_ADDR:
    case DPDMA_CH4_PYLD_CUR_ADDRE:
    case DPDMA_CH4_PYLD_CUR_ADDR:
    case DPDMA_CH5_PYLD_CUR_ADDRE:
    case DPDMA_CH5_PYLD_CUR_ADDR:
    case DPDMA_CH0_STATUS:
    case DPDMA_CH1_STATUS:
    case DPDMA_CH2_STATUS:
    case DPDMA_CH3_STATUS:
    case DPDMA_CH4_STATUS:
    case DPDMA_CH5_STATUS:
    case DPDMA_CH0_VDO:
    case DPDMA_CH1_VDO:
    case DPDMA_CH2_VDO:
    case DPDMA_CH3_VDO:
    case DPDMA_CH4_VDO:
    case DPDMA_CH5_VDO:
    case DPDMA_CH0_PYLD_SZ:
    case DPDMA_CH1_PYLD_SZ:
    case DPDMA_CH2_PYLD_SZ:
    case DPDMA_CH3_PYLD_SZ:
    case DPDMA_CH4_PYLD_SZ:
    case DPDMA_CH5_PYLD_SZ:
    case DPDMA_CH0_DSCR_ID:
    case DPDMA_CH1_DSCR_ID:
    case DPDMA_CH2_DSCR_ID:
    case DPDMA_CH3_DSCR_ID:
    case DPDMA_CH4_DSCR_ID:
    case DPDMA_CH5_DSCR_ID:
        /*
         * Trying to write to a read only register..
         */
        break;
    case DPDMA_GBL:
        /*
         * This is a write only register so it's read as zero in the read
         * callback.
         * We store the value anyway so we can know if the channel is
         * enabled.
         */
        s->registers[offset] = value & 0x00000FFF;
        /*
         * TODO: enable the channels, change next fetch to start address.
         */
        break;
    case DPDMA_CH0_DSCR_STRT_ADDRE:
    case DPDMA_CH1_DSCR_STRT_ADDRE:
    case DPDMA_CH2_DSCR_STRT_ADDRE:
    case DPDMA_CH3_DSCR_STRT_ADDRE:
    case DPDMA_CH4_DSCR_STRT_ADDRE:
    case DPDMA_CH5_DSCR_STRT_ADDRE:
        value &= 0x0000FFFF;
        s->registers[offset] = value;
        break;
    case DPDMA_CH0_CNTL:
    case DPDMA_CH1_CNTL:
    case DPDMA_CH2_CNTL:
    case DPDMA_CH3_CNTL:
    case DPDMA_CH4_CNTL:
    case DPDMA_CH5_CNTL:
        value &= 0x3FFFFFFF;
        s->registers[offset] = value;
        break;
    default:
        assert(offset <= (0xFFC >> 2));
        s->registers[offset] = value;
        break;
    }
}

static const MemoryRegionOps dma_ops = {
    .read = xilinx_dpdma_read,
    .write = xilinx_dpdma_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void xilinx_dpdma_realize(DeviceState *dev, Error **errp)
{
    XilinxDPDMAState *s = XILINX_DPDMA(dev);

    s->dma_as = s->dma_mr ? address_space_init_shareable(s->dma_mr, NULL)
                          : &address_space_memory;
}

static void xilinx_dpdma_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    XilinxDPDMAState *s = XILINX_DPDMA(obj);

    memory_region_init_io(&s->iomem, obj, &dma_ops, s,
                          TYPE_XILINX_DPDMA, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    object_property_add_link(obj, "dma", TYPE_MEMORY_REGION,
                             (Object **)&s->dma_mr,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &error_abort);
}

static void xilinx_dpdma_reset(DeviceState *dev)
{
    XilinxDPDMAState *s = XILINX_DPDMA(dev);
    memset(s->registers, 0, sizeof(s->registers));
    s->registers[DPDMA_IMR] =  0x07FFFFFF;
    s->registers[DPDMA_EIMR] = 0xFFFFFFFF;
    s->registers[DPDMA_ALC0_MIN] = 0x0000FFFF;
    s->registers[DPDMA_ALC1_MIN] = 0x0000FFFF;
}

static void xilinx_dpdma_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &vmstate_xilinx_dpdma;
    dc->reset = xilinx_dpdma_reset;
    dc->realize = xilinx_dpdma_realize;
}

static const TypeInfo xilinx_dpdma_info = {
    .name          = TYPE_XILINX_DPDMA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XilinxDPDMAState),
    .instance_init = xilinx_dpdma_init,
    .class_init    = xilinx_dpdma_class_init,
};

static void xilinx_dpdma_register_types(void)
{
    type_register_static(&xilinx_dpdma_info);
}

bool xilinx_dpdma_start_operation(XilinxDPDMAState *s, uint8_t channel)
{
    uint64_t desc_addr;
    uint64_t source_addr[6];
    DPDMADescriptor desc;
    bool done;
    size_t ptr = 0;

    assert(channel <= 5);

    DPRINTF("dpdma_start_channel() on channel %u\n", channel);

    if (xilinx_dpdma_is_channel_paused(s, channel)) {
        DPRINTF("Channel is paused..\n");
        return false;
    }

    if (!xilinx_dpdma_is_channel_enabled(s, channel)) {
        DPRINTF("Channel isn't enabled..\n");
        return false;
    }

    if ((s->registers[DPDMA_GBL] & (1 << channel)) ||
        (s->registers[DPDMA_GBL] & ((1 << channel) << 6))) {
        desc_addr = xilinx_dpdma_descriptor_start_address(s, channel);
    } else {
        desc_addr = s->next_desc_addr[channel];
    }

    s->registers[DPDMA_ISR] |= (1 << 27);
    xilinx_dpdma_update_irq(s);

    /*
     * XXX: Maybe we can improve that:
     *      - load descriptors.
     *      - fetch all data in one call if they are contiguous for example.
     */

    do {
        if (dma_memory_read(s->dma_as, desc_addr, &desc,
                    sizeof(DPDMADescriptor))) {
            s->registers[DPDMA_EISR] |= ((1 << 1) << channel);
            xilinx_dpdma_update_irq(s);
            DPRINTF("Can't get the descriptor.\n");
            return false;
        }
        #ifdef DEBUG_DPDMA
        xilinx_dpdma_dump_descriptor(&desc);
        #endif

        DPRINTF("location of the descriptor: 0x%8.8lx\n", desc_addr);
        if (!xilinx_dpdma_desc_is_valid(&desc)) {
            s->registers[DPDMA_EISR] |= ((1 << 7) << channel);
            xilinx_dpdma_update_irq(s);
            DPRINTF("Invalid descriptor..\n");
            break;
        }

        if (xilinx_dpdma_desc_crc_enabled(&desc)
                & !xilinx_dpdma_desc_check_crc(&desc)) {
            s->registers[DPDMA_EISR] |= ((1 << 13) << channel);
            xilinx_dpdma_update_irq(s);
            DPRINTF("Bad CRC for descriptor..\n");
            break;
        }

        if (s->data[channel]) {
            int64_t transfer_len =
                xilinx_dpdma_desc_get_transfer_size(&desc);
            uint32_t line_size = xilinx_dpdma_desc_get_line_size(&desc);
            uint32_t line_stride = xilinx_dpdma_desc_get_line_stride(&desc);
            if (xilinx_dpdma_desc_is_contiguous(&desc)) {
                source_addr[0] =
                    xilinx_dpdma_desc_get_source_address(&desc, 0);
                while (transfer_len != 0) {
                    if (dma_memory_read(s->dma_as,
                                source_addr[0],
                                &(s->data[channel][ptr]),
                                line_size)) {
                        s->registers[DPDMA_ISR] |= ((1 << 12) << channel);
                        xilinx_dpdma_update_irq(s);
                        DPRINTF("Can't get data.\n");
                        break;
                    }
                    ptr += line_size;
                    transfer_len -= line_size;
                    source_addr[0] += line_stride;
                }
            } else {
                DPRINTF("Source address:\n");
                int frag;
                for (frag = 0; frag < 5; frag++) {
                    source_addr[frag] =
                        xilinx_dpdma_desc_get_source_address(&desc, frag);
                    DPRINTF("Fragment %u: 0x%8.8lX\n", frag + 1,
                            source_addr[frag]);
                }

                frag = 0;
                while (transfer_len < 0) {
                    if (frag >= 5) {
                        break;
                    }
                    size_t fragment_len = 4096 - (source_addr[frag] % 4096);

                    if (dma_memory_read(s->dma_as,
                                source_addr[frag],
                                &(s->data[channel][ptr]),
                                fragment_len)) {
                        s->registers[DPDMA_ISR] |= ((1 << 12) << channel);
                        xilinx_dpdma_update_irq(s);
                        DPRINTF("Can't get data.\n");
                        break;
                    }
                    ptr += fragment_len;
                    transfer_len -= fragment_len;
                    frag += 1;
                }
            }
        }
        desc_addr = xilinx_dpdma_desc_get_next_descriptor_address(&desc);
        DPRINTF("next descriptor address 0x%lx\n", desc_addr);

        if (xilinx_dpdma_desc_completion_interrupt(&desc)) {
            s->registers[DPDMA_ISR] |= (1 << channel);
            xilinx_dpdma_update_irq(s);
        }

        done = xilinx_dpdma_desc_is_last_of_frame(&desc);
        if (xilinx_dpdma_desc_is_last(&desc)) {
            done = true;
            xilinx_dpdma_pause_channel(s, channel);
        }
    } while (!done);

    return true;
}

/*
 * Set the host location to be filled with the data.
 */
void xilinx_dpdma_set_host_data_location(XilinxDPDMAState *s, uint8_t channel,
                                         void *p)
{
    if (!s) {
        qemu_log_mask(LOG_UNIMP, "DPDMA client not attached to valid DPDMA"
                      " instance\n");
        return;
    }

    assert(channel <= 5);
    s->data[channel] = p;
}

type_init(xilinx_dpdma_register_types)
