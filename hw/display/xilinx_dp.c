/*
 * xilinx_dp.c
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
 * (at your option)any later version.
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

#include "hw/sysbus.h"
#include "ui/console.h"
#include "hw/aux.h"
#include "hw/i2c/i2c.h"
#include "hw/display/dpcd.h"
#include "hw/i2c/i2c-ddc.h"
#include "qemu/fifo.h"
#include "xilinx_dp.h"

#ifndef DEBUG_DP
#define DEBUG_DP 0
#endif

#define DPRINTF(fmt, ...) do {                                                 \
    if (DEBUG_DP) {                                                            \
        qemu_log("xilinx_dp: " fmt , ## __VA_ARGS__);                          \
    }                                                                          \
} while (0);

/*
 * Register offset for DP.
 */
#define DP_LINK_BW_SET                      (0x00000000 >> 2)
#define DP_LANE_COUNT_SET                   (0x00000004 >> 2)
#define DP_ENHANCED_FRAME_EN                (0x00000008 >> 2)
#define DP_TRAINING_PATTERN_SET             (0x0000000C >> 2)
#define DP_LINK_QUAL_PATTERN_SET            (0x00000010 >> 2)
#define DP_SCRAMBLING_DISABLE               (0x00000014 >> 2)
#define DP_DOWNSPREAD_CTRL                  (0x00000018 >> 2)
#define DP_SOFTWARE_RESET                   (0x0000001C >> 2)
#define DP_TRANSMITTER_ENABLE               (0x00000080 >> 2)
#define DP_MAIN_STREAM_ENABLE               (0x00000084 >> 2)
#define DP_FORCE_SCRAMBLER_RESET            (0x000000C0 >> 2)
#define DP_VERSION_REGISTER                 (0x000000F8 >> 2)
#define DP_CORE_ID                          (0x000000FC >> 2)
#define DP_AUX_COMMAND_REGISTER             (0x00000100 >> 2)
#define AUX_COMMAND_MASK                    (0x00000F00)
#define AUX_COMMAND_SHIFT                   (8)
#define AUX_COMMAND_NBYTES                  (0x0000000F)
#define AUX_COMMAND_ADDR_ONLY_TRANSFER_BIT  (1 << 12)
#define DP_AUX_WRITE_FIFO                   (0x00000104 >> 2)
#define DP_AUX_ADDRESS                      (0x00000108 >> 2)
#define DP_AUX_CLOCK_DIVIDER                (0x0000010C >> 2)
#define DP_TX_USER_FIFO_OVERFLOW            (0x00000110 >> 2)
#define DP_INTERRUPT_SIGNAL_STATE           (0x00000130 >> 2)
#define DP_AUX_REPLY_DATA                   (0x00000134 >> 2)
#define DP_AUX_REPLY_CODE                   (0x00000138 >> 2)
#define DP_AUX_REPLY_COUNT                  (0x0000013C >> 2)
#define DP_REPLY_DATA_COUNT                 (0x00000148 >> 2)
#define DP_REPLY_STATUS                     (0x0000014C >> 2)
#define DP_HPD_DURATION                     (0x00000150 >> 2)
#define DP_MAIN_STREAM_HTOTAL               (0x00000180 >> 2)
#define DP_MAIN_STREAM_VTOTAL               (0x00000184 >> 2)
#define DP_MAIN_STREAM_POLARITY             (0x00000188 >> 2)
#define DP_MAIN_STREAM_HSWIDTH              (0x0000018C >> 2)
#define DP_MAIN_STREAM_VSWIDTH              (0x00000190 >> 2)
#define DP_MAIN_STREAM_HRES                 (0x00000194 >> 2)
#define DP_MAIN_STREAM_VRES                 (0x00000198 >> 2)
#define DP_MAIN_STREAM_HSTART               (0x0000019C >> 2)
#define DP_MAIN_STREAM_VSTART               (0x000001A0 >> 2)
#define DP_MAIN_STREAM_MISC0                (0x000001A4 >> 2)
#define DP_MAIN_STREAM_MISC1                (0x000001A8 >> 2)
#define DP_MAIN_STREAM_M_VID                (0x000001AC >> 2)
#define DP_MSA_TRANSFER_UNIT_SIZE           (0x000001B0 >> 2)
#define DP_MAIN_STREAM_N_VID                (0x000001B4 >> 2)
#define DP_USER_DATA_COUNT_PER_LANE         (0x000001BC >> 2)
#define DP_MIN_BYTES_PER_TU                 (0x000001C4 >> 2)
#define DP_FRAC_BYTES_PER_TU                (0x000001C8 >> 2)
#define DP_INIT_WAIT                        (0x000001CC >> 2)
#define DP_PHY_RESET                        (0x00000200 >> 2)
#define DP_PHY_VOLTAGE_DIFF_LANE_0          (0x00000220 >> 2)
#define DP_PHY_VOLTAGE_DIFF_LANE_1          (0x00000224 >> 2)
#define DP_TRANSMIT_PRBS7                   (0x00000230 >> 2)
#define DP_PHY_CLOCK_SELECT                 (0x00000234 >> 2)
#define DP_TX_PHY_POWER_DOWN                (0x00000238 >> 2)
#define DP_PHY_PRECURSOR_LANE_0             (0x0000023C >> 2)
#define DP_PHY_PRECURSOR_LANE_1             (0x00000240 >> 2)
#define DP_PHY_POSTCURSOR_LANE_0            (0x0000024C >> 2)
#define DP_PHY_POSTCURSOR_LANE_1            (0x00000250 >> 2)
#define DP_PHY_STATUS                       (0x00000280 >> 2)
#define DP_TX_AUDIO_CONTROL                 (0x00000300 >> 2)
#define DP_TX_AUDIO_CHANNELS                (0x00000304 >> 2)
#define DP_TX_AUDIO_INFO_DATA0              (0x00000308 >> 2)
#define DP_TX_AUDIO_INFO_DATA1              (0x0000030C >> 2)
#define DP_TX_AUDIO_INFO_DATA2              (0x00000310 >> 2)
#define DP_TX_AUDIO_INFO_DATA3              (0x00000314 >> 2)
#define DP_TX_AUDIO_INFO_DATA4              (0x00000318 >> 2)
#define DP_TX_AUDIO_INFO_DATA5              (0x0000031C >> 2)
#define DP_TX_AUDIO_INFO_DATA6              (0x00000320 >> 2)
#define DP_TX_AUDIO_INFO_DATA7              (0x00000324 >> 2)
#define DP_TX_M_AUD                         (0x00000328 >> 2)
#define DP_TX_N_AUD                         (0x0000032C >> 2)
#define DP_TX_AUDIO_EXT_DATA0               (0x00000330 >> 2)
#define DP_TX_AUDIO_EXT_DATA1               (0x00000334 >> 2)
#define DP_TX_AUDIO_EXT_DATA2               (0x00000338 >> 2)
#define DP_TX_AUDIO_EXT_DATA3               (0x0000033C >> 2)
#define DP_TX_AUDIO_EXT_DATA4               (0x00000340 >> 2)
#define DP_TX_AUDIO_EXT_DATA5               (0x00000344 >> 2)
#define DP_TX_AUDIO_EXT_DATA6               (0x00000348 >> 2)
#define DP_TX_AUDIO_EXT_DATA7               (0x0000034C >> 2)
#define DP_TX_AUDIO_EXT_DATA8               (0x00000350 >> 2)
#define DP_INT_STATUS                       (0x000003A0 >> 2)
#define DP_INT_MASK                         (0x000003A4 >> 2)
#define DP_INT_EN                           (0x000003A8 >> 2)
#define DP_INT_DS                           (0x000003AC >> 2)

/*
 * Registers offset for Audio Video Buffer configuration.
 */
#define V_BLEND_OFFSET                      (0x0000A000)
#define V_BLEND_BG_CLR_0                    (0x00000000 >> 2)
#define V_BLEND_BG_CLR_1                    (0x00000004 >> 2)
#define V_BLEND_BG_CLR_2                    (0x00000008 >> 2)
#define V_BLEND_SET_GLOBAL_ALPHA_REG        (0x0000000C >> 2)
#define V_BLEND_OUTPUT_VID_FORMAT           (0x00000014 >> 2)
#define V_BLEND_LAYER0_CONTROL              (0x00000018 >> 2)
#define V_BLEND_LAYER1_CONTROL              (0x0000001C >> 2)
#define V_BLEND_RGB2YCBCR_COEFF0            (0x00000020 >> 2)
#define V_BLEND_RGB2YCBCR_COEFF1            (0x00000024 >> 2)
#define V_BLEND_RGB2YCBCR_COEFF2            (0x00000028 >> 2)
#define V_BLEND_RGB2YCBCR_COEFF3            (0x0000002C >> 2)
#define V_BLEND_RGB2YCBCR_COEFF4            (0x00000030 >> 2)
#define V_BLEND_RGB2YCBCR_COEFF5            (0x00000034 >> 2)
#define V_BLEND_RGB2YCBCR_COEFF6            (0x00000038 >> 2)
#define V_BLEND_RGB2YCBCR_COEFF7            (0x0000003C >> 2)
#define V_BLEND_RGB2YCBCR_COEFF8            (0x00000040 >> 2)
#define V_BLEND_IN1CSC_COEFF0               (0x00000044 >> 2)
#define V_BLEND_IN1CSC_COEFF1               (0x00000048 >> 2)
#define V_BLEND_IN1CSC_COEFF2               (0x0000004C >> 2)
#define V_BLEND_IN1CSC_COEFF3               (0x00000050 >> 2)
#define V_BLEND_IN1CSC_COEFF4               (0x00000054 >> 2)
#define V_BLEND_IN1CSC_COEFF5               (0x00000058 >> 2)
#define V_BLEND_IN1CSC_COEFF6               (0x0000005C >> 2)
#define V_BLEND_IN1CSC_COEFF7               (0x00000060 >> 2)
#define V_BLEND_IN1CSC_COEFF8               (0x00000064 >> 2)
#define V_BLEND_LUMA_IN1CSC_OFFSET          (0x00000068 >> 2)
#define V_BLEND_CR_IN1CSC_OFFSET            (0x0000006C >> 2)
#define V_BLEND_CB_IN1CSC_OFFSET            (0x00000070 >> 2)
#define V_BLEND_LUMA_OUTCSC_OFFSET          (0x00000074 >> 2)
#define V_BLEND_CR_OUTCSC_OFFSET            (0x00000078 >> 2)
#define V_BLEND_CB_OUTCSC_OFFSET            (0x0000007C >> 2)
#define V_BLEND_IN2CSC_COEFF0               (0x00000080 >> 2)
#define V_BLEND_IN2CSC_COEFF1               (0x00000084 >> 2)
#define V_BLEND_IN2CSC_COEFF2               (0x00000088 >> 2)
#define V_BLEND_IN2CSC_COEFF3               (0x0000008C >> 2)
#define V_BLEND_IN2CSC_COEFF4               (0x00000090 >> 2)
#define V_BLEND_IN2CSC_COEFF5               (0x00000094 >> 2)
#define V_BLEND_IN2CSC_COEFF6               (0x00000098 >> 2)
#define V_BLEND_IN2CSC_COEFF7               (0x0000009C >> 2)
#define V_BLEND_IN2CSC_COEFF8               (0x000000A0 >> 2)
#define V_BLEND_LUMA_IN2CSC_OFFSET          (0x000000A4 >> 2)
#define V_BLEND_CR_IN2CSC_OFFSET            (0x000000A8 >> 2)
#define V_BLEND_CB_IN2CSC_OFFSET            (0x000000AC >> 2)
#define V_BLEND_CHROMA_KEY_ENABLE           (0x000001D0 >> 2)
#define V_BLEND_CHROMA_KEY_COMP1            (0x000001D4 >> 2)
#define V_BLEND_CHROMA_KEY_COMP2            (0x000001D8 >> 2)
#define V_BLEND_CHROMA_KEY_COMP3            (0x000001DC >> 2)

/*
 * Registers offset for Audio Video Buffer configuration.
 */
#define AV_BUF_MANAGER_OFFSET               (0x0000B000)
#define AV_BUF_FORMAT                       (0x00000000 >> 2)
#define AV_BUF_NON_LIVE_LATENCY             (0x00000008 >> 2)
#define AV_CHBUF0                           (0x00000010 >> 2)
#define AV_CHBUF1                           (0x00000014 >> 2)
#define AV_CHBUF2                           (0x00000018 >> 2)
#define AV_CHBUF3                           (0x0000001C >> 2)
#define AV_CHBUF4                           (0x00000020 >> 2)
#define AV_CHBUF5                           (0x00000024 >> 2)
#define AV_BUF_STC_CONTROL                  (0x0000002C >> 2)
#define AV_BUF_STC_INIT_VALUE0              (0x00000030 >> 2)
#define AV_BUF_STC_INIT_VALUE1              (0x00000034 >> 2)
#define AV_BUF_STC_ADJ                      (0x00000038 >> 2)
#define AV_BUF_STC_VIDEO_VSYNC_TS_REG0      (0x0000003C >> 2)
#define AV_BUF_STC_VIDEO_VSYNC_TS_REG1      (0x00000040 >> 2)
#define AV_BUF_STC_EXT_VSYNC_TS_REG0        (0x00000044 >> 2)
#define AV_BUF_STC_EXT_VSYNC_TS_REG1        (0x00000048 >> 2)
#define AV_BUF_STC_CUSTOM_EVENT_TS_REG0     (0x0000004C >> 2)
#define AV_BUF_STC_CUSTOM_EVENT_TS_REG1     (0x00000050 >> 2)
#define AV_BUF_STC_CUSTOM_EVENT2_TS_REG0    (0x00000054 >> 2)
#define AV_BUF_STC_CUSTOM_EVENT2_TS_REG1    (0x00000058 >> 2)
#define AV_BUF_STC_SNAPSHOT0                (0x00000060 >> 2)
#define AV_BUF_STC_SNAPSHOT1                (0x00000064 >> 2)
#define AV_BUF_OUTPUT_AUDIO_VIDEO_SELECT    (0x00000070 >> 2)
#define AV_BUF_HCOUNT_VCOUNT_INT0           (0x00000074 >> 2)
#define AV_BUF_HCOUNT_VCOUNT_INT1           (0x00000078 >> 2)
#define AV_BUF_DITHER_CONFIG                (0x0000007C >> 2)
#define AV_BUF_DITHER_CONFIG_MAX            (0x0000008C >> 2)
#define AV_BUF_DITHER_CONFIG_MIN            (0x00000090 >> 2)
#define AV_BUF_PATTERN_GEN_SELECT           (0x00000100 >> 2)
#define AV_BUF_AUD_VID_CLK_SOURCE           (0x00000120 >> 2)
#define AV_BUF_SRST_REG                     (0x00000124 >> 2)
#define AV_BUF_AUDIO_RDY_INTERVAL           (0x00000128 >> 2)
#define AV_BUF_AUDIO_CH_CONFIG              (0x0000012C >> 2)
#define AV_BUF_GRAPHICS_COMP0_SCALE_FACTOR  (0x00000200 >> 2)
#define AV_BUF_GRAPHICS_COMP1_SCALE_FACTOR  (0x00000204 >> 2)
#define AV_BUF_GRAPHICS_COMP2_SCALE_FACTOR  (0x00000208 >> 2)
#define AV_BUF_VIDEO_COMP0_SCALE_FACTOR     (0x0000020C >> 2)
#define AV_BUF_VIDEO_COMP1_SCALE_FACTOR     (0x00000210 >> 2)
#define AV_BUF_VIDEO_COMP2_SCALE_FACTOR     (0x00000214 >> 2)
#define AV_BUF_LIVE_VIDEO_COMP0_SF          (0x00000218 >> 2)
#define AV_BUF_LIVE_VIDEO_COMP1_SF          (0x0000021C >> 2)
#define AV_BUF_LIVE_VIDEO_COMP2_SF          (0x00000220 >> 2)
#define AV_BUF_LIVE_VID_CONFIG              (0x00000224 >> 2)
#define AV_BUF_LIVE_GFX_COMP0_SF            (0x00000228 >> 2)
#define AV_BUF_LIVE_GFX_COMP1_SF            (0x0000022C >> 2)
#define AV_BUF_LIVE_GFX_COMP2_SF            (0x00000230 >> 2)
#define AV_BUF_LIVE_GFX_CONFIG              (0x00000234 >> 2)

#define AUDIO_MIXER_VOLUME_CONTROL          (0x0000C000 >> 2)
#define AUDIO_MIXER_META_DATA               (0x0000C004 >> 2)
#define AUD_CH_STATUS_REG0                  (0x0000C008 >> 2)
#define AUD_CH_STATUS_REG1                  (0x0000C00C >> 2)
#define AUD_CH_STATUS_REG2                  (0x0000C010 >> 2)
#define AUD_CH_STATUS_REG3                  (0x0000C014 >> 2)
#define AUD_CH_STATUS_REG4                  (0x0000C018 >> 2)
#define AUD_CH_STATUS_REG5                  (0x0000C01C >> 2)
#define AUD_CH_A_DATA_REG0                  (0x0000C020 >> 2)
#define AUD_CH_A_DATA_REG1                  (0x0000C024 >> 2)
#define AUD_CH_A_DATA_REG2                  (0x0000C028 >> 2)
#define AUD_CH_A_DATA_REG3                  (0x0000C02C >> 2)
#define AUD_CH_A_DATA_REG4                  (0x0000C030 >> 2)
#define AUD_CH_A_DATA_REG5                  (0x0000C034 >> 2)
#define AUD_CH_B_DATA_REG0                  (0x0000C038 >> 2)
#define AUD_CH_B_DATA_REG1                  (0x0000C03C >> 2)
#define AUD_CH_B_DATA_REG2                  (0x0000C040 >> 2)
#define AUD_CH_B_DATA_REG3                  (0x0000C044 >> 2)
#define AUD_CH_B_DATA_REG4                  (0x0000C048 >> 2)
#define AUD_CH_B_DATA_REG5                  (0x0000C04C >> 2)

typedef enum dp_graphic_fmt {
    DP_GRAPHIC_RGBA8888 = 0 << 8,
    DP_GRAPHIC_ABGR8888 = 1 << 8,
    DP_GRAPHIC_RGB888 = 2 << 8,
    DP_GRAPHIC_BGR888 = 3 << 8,
    DP_GRAPHIC_RGBA5551 = 4 << 8,
    DP_GRAPHIC_RGBA4444 = 5 << 8,
    DP_GRAPHIC_RGB565 = 6 << 8,
    DP_GRAPHIC_8BPP = 7 << 8,
    DP_GRAPHIC_4BPP = 8 << 8,
    DP_GRAPHIC_2BPP = 9 << 8,
    DP_GRAPHIC_1BPP = 10 << 8,
    DP_GRAPHIC_MASK = 0xF << 8
} dp_graphic_fmt;

typedef enum dp_video_fmt {
    DP_VID_CB_Y0_CR_Y1 = 0,
    DP_VID_CR_Y0_CB_Y1 = 1,
    DP_VID_Y0_CR_Y1_CB = 2,
    DP_VID_Y0_CB_Y1_CR = 3,
    DP_VID_YV16 = 4,
    DP_VID_YV24 = 5,
    DP_VID_YV16CL = 6,
    DP_VID_MONO = 7,
    DP_VID_YV16CL2 = 8,
    DP_VID_YUV444 = 9,
    DP_VID_RGB888 = 10,
    DP_VID_RGBA8880 = 11,
    DP_VID_RGB888_10BPC = 12,
    DP_VID_YUV444_10BPC = 13,
    DP_VID_YV16CL2_10BPC = 14,
    DP_VID_YV16CL_10BPC = 15,
    DP_VID_YV16_10BPC = 16,
    DP_VID_YV24_10BPC = 17,
    DP_VID_Y_ONLY_10BPC = 18,
    DP_VID_YV16_420 = 19,
    DP_VID_YV16CL_420 = 20,
    DP_VID_YV16CL2_420 = 21,
    DP_VID_YV16_420_10BPC = 22,
    DP_VID_YV16CL_420_10BPC = 23,
    DP_VID_YV16CL2_420_10BPC = 24
} dp_video_fmt;

struct XilinxDPState {
    SysBusDevice parent_obj;
    MemoryRegion container;

    /*
     * Registers for the Core.
     */
    uint32_t core_registers[0x3AF >> 2];
    MemoryRegion core_iomem;

    /*
     * Registers for Audio Video Buffer Manager.
     */
    uint32_t avbufm_registers[0x238 >> 2];
    MemoryRegion avbufm_iomem;

    /*
     * Register for Video Blender.
     */
    uint32_t vblend_registers[0x1DF >> 2];
    MemoryRegion vblend_iomem;

    /*
     * QEMU Console related.
     */
    QemuConsole *console;
    pixman_format_code_t current_graphic_fmt;

    /*
     * Associated DPDMA controller.
     */
    XilinxDPDMAState *dpdma;

    /*
     * IRQ.
     */
    qemu_irq irq;

    /*
     * AUX bus.
     */
    AUXBus *aux_bus;

    Fifo rx_fifo;
    Fifo tx_fifo;

    uint32_t last_request;

    /*
     * XXX: This should be in an other module.
     */
    DPCDState *dpcd;
    I2CDDCState *edid;
};

static const VMStateDescription vmstate_dp = {
    .name = TYPE_XILINX_DP,
    .version_id = 1,
    .fields = (VMStateField[]){

        VMSTATE_END_OF_LIST()
    }
};

/*
 * AUX channel related function.
 */
static void dp_aux_clear_rx_fifo(XilinxDPState *s)
{
    fifo_reset(&s->rx_fifo);
}

static void dp_aux_push_rx_fifo(XilinxDPState *s, uint8_t *buf, size_t len)
{
    int i;

    DPRINTF("Push %u data in rx_fifo\n", (unsigned)len);
    for (i = 0; i < len; i++) {
        if (fifo_is_full(&s->rx_fifo)) {
            DPRINTF("rx_fifo overflow..\n");
            abort();
        }
        fifo_push8(&s->rx_fifo, buf[i]);
    }
}

static uint8_t dp_aux_pop_rx_fifo(XilinxDPState *s)
{
    uint8_t ret;

    if (fifo_is_empty(&s->rx_fifo)) {
        DPRINTF("rx_fifo underflow..\n");
        abort();
    }
    ret = fifo_pop8(&s->rx_fifo);
    DPRINTF("pop 0x%2.2X from rx_fifo.\n", ret);
    return ret;
}

static void dp_aux_clear_tx_fifo(XilinxDPState *s)
{
    fifo_reset(&s->tx_fifo);
}

static void dp_aux_push_tx_fifo(XilinxDPState *s, uint8_t *buf, size_t len)
{
    int i;

    DPRINTF("Push %u data in tx_fifo\n", (unsigned)len);
    for (i = 0; i < len; i++) {
        if (fifo_is_full(&s->tx_fifo)) {
            DPRINTF("tx_fifo overflow..\n");
            abort();
        }
        fifo_push8(&s->tx_fifo, buf[i]);
    }
}

static uint8_t dp_aux_pop_tx_fifo(XilinxDPState *s)
{
    uint8_t ret;

    if (fifo_is_empty(&s->tx_fifo)) {
        DPRINTF("tx_fifo underflow..\n");
        abort();
    }
    ret = fifo_pop8(&s->tx_fifo);
    DPRINTF("pop 0x%2.2X from tx_fifo.\n", ret);
    return ret;
}

static uint32_t dp_aux_get_address(XilinxDPState *s)
{
    return s->core_registers[DP_AUX_ADDRESS];
}

static uint8_t dp_aux_get_data(XilinxDPState *s)
{
    return dp_aux_pop_rx_fifo(s);
}

static void dp_aux_set_data(XilinxDPState *s, uint8_t value)
{
    dp_aux_push_tx_fifo(s, &value, 1);
}

/*
 * Get command from the register.
 */
static void dp_aux_set_command(XilinxDPState *s, uint32_t value)
{
    /*
     * XXX: What happen in the corner case, eg: fifo under/overflow?
     */
    aux_command cmd = (value & AUX_COMMAND_MASK) >> AUX_COMMAND_SHIFT;
    uint8_t nbytes;
    uint8_t buf[16];
    int i;
    bool addr_only = value & AUX_COMMAND_ADDR_ONLY_TRANSFER_BIT;

    if (addr_only) {
        nbytes = 0;
    } else {
        nbytes = (value & AUX_COMMAND_NBYTES) + 1;
    }

    switch (cmd) {
    case READ_AUX:
    case READ_I2C:
    case READ_I2C_MOT:
        s->core_registers[DP_AUX_REPLY_CODE] = aux_request(s->aux_bus, cmd,
                                               dp_aux_get_address(s),
                                               nbytes, buf);
        s->core_registers[DP_REPLY_DATA_COUNT] = nbytes;

        if (s->core_registers[DP_AUX_REPLY_CODE] == AUX_I2C_ACK && !addr_only) {
            dp_aux_push_rx_fifo(s, buf, nbytes);
        }
    break;
    case WRITE_AUX:
    case WRITE_I2C:
    case WRITE_I2C_MOT:
        for (i = 0; i < nbytes; i++) {
            buf[i] = dp_aux_pop_tx_fifo(s);
        }
        s->core_registers[DP_AUX_REPLY_CODE] = aux_request(s->aux_bus, cmd,
                                               dp_aux_get_address(s),
                                               nbytes, buf);
        if (!addr_only)
            dp_aux_clear_tx_fifo(s);
    break;
    case WRITE_I2C_STATUS:
    default:
        abort();
    break;
    }

    /*
     * XXX: Trigger an interrupt here?
     * The reply is received.. so just assert the flag.
     */
    s->core_registers[DP_INTERRUPT_SIGNAL_STATE] |= 0x04;
}

static void dp_set_dpdma(Object *obj, const char *name, Object *val,
                         Error **errp)
{
    XilinxDPState *s = XILINX_DP(obj);

    if (s->console) {
        DisplaySurface *surface = qemu_console_surface(s->console);
        XilinxDPDMAState *dma = XILINX_DPDMA(val);
        xilinx_dpdma_set_host_data_location(dma, 3, surface_data(surface));
    }
}

/*
 * Recreate the surfaces for the DP.
 * This happen after a resolution or format change.
 */
static void dp_recreate_surface(XilinxDPState *s)
{
    uint16_t width = s->core_registers[DP_MAIN_STREAM_HRES];
    uint16_t height = s->core_registers[DP_MAIN_STREAM_VRES];
    DisplaySurface *new_surface;

    if ((width != 0) && (height != 0)) {
        new_surface = qemu_create_displaysurface_format(s->current_graphic_fmt,
                                                        width, height);
        dpy_gfx_replace_surface(s->console, new_surface);
        xilinx_dpdma_set_host_data_location(s->dpdma, 3,
                                            surface_data(new_surface));
    }
}

/*
 * Change the graphic format of the surface.
 * XXX: To be completed.
 */
static void dp_change_graphic_fmt(XilinxDPState *s)
{
    switch (s->avbufm_registers[AV_BUF_FORMAT] & DP_GRAPHIC_MASK) {
    case DP_GRAPHIC_RGBA8888:
        s->current_graphic_fmt = PIXMAN_r8g8b8a8;
        break;
    case DP_GRAPHIC_ABGR8888:
        s->current_graphic_fmt = PIXMAN_a8b8g8r8;
        break;
    case DP_GRAPHIC_RGB565:
        s->current_graphic_fmt = PIXMAN_r5g6b5;
        break;
    case DP_GRAPHIC_RGB888:
        s->current_graphic_fmt = PIXMAN_r8g8b8;
        break;
    case DP_GRAPHIC_BGR888:
        s->current_graphic_fmt = PIXMAN_b8g8r8;
        break;
    default:
        DPRINTF("error: unsupported graphic format %u.\n",
                s->avbufm_registers[AV_BUF_FORMAT] & DP_GRAPHIC_MASK);
        abort();
        break;
    }

    dp_recreate_surface(s);
}

static void dp_update_irq(XilinxDPState *s)
{
    uint32_t flags;

    flags = s->core_registers[DP_INT_STATUS] & ~s->core_registers[DP_INT_MASK];
    DPRINTF("update IRQ value = %" PRIx32 "\n", flags);
    qemu_set_irq(s->irq, flags != 0);
}

static uint64_t dp_read(void *opaque, hwaddr offset, unsigned size)
{
    XilinxDPState *s = XILINX_DP(opaque);
    uint64_t ret = 0;

    assert(size == 4);
    assert((offset % 4) == 0);
    offset = offset >> 2;

    switch (offset) {
    /*
     * Trying to read a write only register.
     */
    case DP_TX_USER_FIFO_OVERFLOW:
        ret = s->core_registers[DP_TX_USER_FIFO_OVERFLOW];
        s->core_registers[DP_TX_USER_FIFO_OVERFLOW] = 0;
    break;
    case DP_AUX_WRITE_FIFO:
        ret = 0;
    break;
    case DP_AUX_REPLY_DATA:
        ret = dp_aux_get_data(s);
    break;
    case DP_INTERRUPT_SIGNAL_STATE:
        /*
         * XXX: Not sure it is the right thing to do actually.
         * The register is not written by the device driver so it's stuck
         * to 0x04.
         */
        ret = s->core_registers[DP_INTERRUPT_SIGNAL_STATE];
        s->core_registers[DP_INTERRUPT_SIGNAL_STATE] &= ~0x04;
    break;
    default:
        assert(offset <= (0x3AC >> 2));
        ret = s->core_registers[offset];
    break;
    }

    DPRINTF("core read @%" PRIx64 " = 0x%8.8lX\n", offset << 2, ret);
    return ret;
}

static void dp_write(void *opaque, hwaddr offset,
                     uint64_t value, unsigned size)
{
    XilinxDPState *s = XILINX_DP(opaque);

    assert(size == 4);
    assert((offset % 4) == 0);

    DPRINTF("core write @%" PRIx64 " = 0x%8.8lX\n", offset, value);

    offset = offset >> 2;

    switch (offset) {
    /*
     * Only special write case are handled.
     */
    case DP_LINK_BW_SET:
        s->core_registers[offset] = value & 0x000000FF;
    break;
    case DP_LANE_COUNT_SET:
    case DP_MAIN_STREAM_MISC0:
        s->core_registers[offset] = value & 0x0000000F;
    break;
    case DP_TRAINING_PATTERN_SET:
    case DP_LINK_QUAL_PATTERN_SET:
    case DP_MAIN_STREAM_POLARITY:
    case DP_PHY_VOLTAGE_DIFF_LANE_0:
    case DP_PHY_VOLTAGE_DIFF_LANE_1:
        s->core_registers[offset] = value & 0x00000003;
    break;
    case DP_ENHANCED_FRAME_EN:
    case DP_SCRAMBLING_DISABLE:
    case DP_DOWNSPREAD_CTRL:
    case DP_MAIN_STREAM_ENABLE:
    case DP_TRANSMIT_PRBS7:
        s->core_registers[offset] = value & 0x00000001;
    break;
    case DP_PHY_CLOCK_SELECT:
        s->core_registers[offset] = value & 0x00000007;
    case DP_SOFTWARE_RESET:
        /*
         * No need to update this bit as it's read '0'.
         */
        /*
         * TODO: reset IP.
         */
    break;
    case DP_TRANSMITTER_ENABLE:
        s->core_registers[offset] = value & 0x01;
    break;
    case DP_FORCE_SCRAMBLER_RESET:
        /*
         * No need to update this bit as it's read '0'.
         */
        /*
         * TODO: force a scrambler reset??
         */
    break;
    case DP_AUX_COMMAND_REGISTER:
        s->core_registers[offset] = value & 0x00001F0F;
        dp_aux_set_command(s, s->core_registers[offset]);
    break;
    case DP_MAIN_STREAM_HTOTAL:
    case DP_MAIN_STREAM_VTOTAL:
    case DP_MAIN_STREAM_HSTART:
    case DP_MAIN_STREAM_VSTART:
        s->core_registers[offset] = value & 0x0000FFFF;
    break;
    case DP_MAIN_STREAM_HRES:
    case DP_MAIN_STREAM_VRES:
        s->core_registers[offset] = value & 0x0000FFFF;
        dp_recreate_surface(s);
    break;
    case DP_MAIN_STREAM_HSWIDTH:
    case DP_MAIN_STREAM_VSWIDTH:
        s->core_registers[offset] = value & 0x00007FFF;
    break;
    case DP_MAIN_STREAM_MISC1:
        s->core_registers[offset] = value & 0x00000086;
    break;
    case DP_MAIN_STREAM_M_VID:
    case DP_MAIN_STREAM_N_VID:
        s->core_registers[offset] = value & 0x00FFFFFF;
    break;
    case DP_MSA_TRANSFER_UNIT_SIZE:
    case DP_MIN_BYTES_PER_TU:
    case DP_INIT_WAIT:
        s->core_registers[offset] = value & 0x00000007;
    break;
    case DP_USER_DATA_COUNT_PER_LANE:
        s->core_registers[offset] = value & 0x0003FFFF;
    break;
    case DP_FRAC_BYTES_PER_TU:
        s->core_registers[offset] = value & 0x000003FF;
    break;
    case DP_PHY_RESET:
        s->core_registers[offset] = value & 0x00010003;
        /*
         * TODO: Reset something?
         */
    break;
    case DP_TX_PHY_POWER_DOWN:
        s->core_registers[offset] = value & 0x0000000F;
        /*
         * TODO: Power down things?
         */
    break;
    case DP_AUX_WRITE_FIFO:
        dp_aux_set_data(s, value & 0x0000000F);
    break;
    case DP_AUX_CLOCK_DIVIDER:
        /*
         * XXX: Do we need to model that?
         */
    break;
    case DP_AUX_REPLY_COUNT:
        /*
         * Writing to this register clear the counter.
         */
        s->core_registers[offset] = 0x00000000;
    break;
    case DP_AUX_ADDRESS:
        s->core_registers[offset] = value & 0x000FFFFF;
    break;
    case DP_VERSION_REGISTER:
    case DP_CORE_ID:
    case DP_TX_USER_FIFO_OVERFLOW:
    case DP_AUX_REPLY_DATA:
    case DP_AUX_REPLY_CODE:
    case DP_REPLY_DATA_COUNT:
    case DP_REPLY_STATUS:
    case DP_HPD_DURATION:
        /*
         * Write to read only location..
         */
    break;
    case DP_INT_STATUS:
        s->core_registers[DP_INT_STATUS] &= ~value;
        dp_update_irq(s);
    break;
    case DP_INT_EN:
        s->core_registers[DP_INT_MASK] &= ~value;
        dp_update_irq(s);
    break;
    case DP_INT_DS:
        s->core_registers[DP_INT_MASK] |= ~value;
        dp_update_irq(s);
    break;
    default:
        assert(offset <= (0x504C >> 2));
        s->core_registers[offset] = value;
    break;
    }
}

static const MemoryRegionOps dp_ops = {
    .read = dp_read,
    .write = dp_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/*
 * This is to handle Read/Write to the Video Blender.
 */
static void vblend_write(void *opaque, hwaddr offset,
                      uint64_t value, unsigned size)
{
    XilinxDPState *s = XILINX_DP(opaque);
    assert(size == 4);
    assert((offset % 4) == 0);

    DPRINTF("vblend: write @%" PRIx64 " = 0x%8.8lX\n", offset, value);

    offset = offset >> 2;

    switch (offset) {
    case V_BLEND_BG_CLR_0:
    case V_BLEND_BG_CLR_1:
    case V_BLEND_BG_CLR_2:
        s->vblend_registers[offset] = value & 0x00000FFF;
    break;
    case V_BLEND_OUTPUT_VID_FORMAT:
        /*
         * TODO: create an enum for blended video format?
         * We don't really care of this I guess as we will output it as RGB
         * in the QEMU console.
         */
        s->vblend_registers[offset] = value & 0x00000017;
    break;
    default:
        s->vblend_registers[offset] = value;
    break;
    }
}

static uint64_t vblend_read(void *opaque, hwaddr offset, unsigned size)
{
    XilinxDPState *s = XILINX_DP(opaque);
    uint32_t ret;

    assert(size == 4);
    assert((offset % 4) == 0);
    offset = offset >> 2;

    ret = s->vblend_registers[offset];
    DPRINTF("vblend: read @%" PRIx64 " = 0x%8.8X\n", offset << 2, ret);
    return ret;
}

static const MemoryRegionOps vblend_ops = {
    .read = vblend_read,
    .write = vblend_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/*
 * This is to handle Read/Write to the Audio Video buffer manager.
 */
static void avbufm_write(void *opaque, hwaddr offset,
                      uint64_t value, unsigned size)
{
    XilinxDPState *s = XILINX_DP(opaque);
    assert(size == 4);
    assert((offset % 4) == 0);

    offset = offset >> 2;

    switch (offset) {
    case AV_BUF_FORMAT:
        s->avbufm_registers[offset] = value & 0x00000FFF;
        dp_change_graphic_fmt(s);
    break;
    case AV_CHBUF0:
    case AV_CHBUF1:
    case AV_CHBUF2:
    case AV_CHBUF3:
    case AV_CHBUF4:
    case AV_CHBUF5:
        /*
         * TODO: enable the operation?
         */
        s->avbufm_registers[offset] = value & 0x0000007F;
    break;
    case AV_BUF_OUTPUT_AUDIO_VIDEO_SELECT:
        /*
         * TODO:
         *       - Pattern generator for both Audio and Video.
         *       - An enumeration for the source?
         */
        s->avbufm_registers[offset] = value & 0x0000007F;
    break;
    case AV_BUF_DITHER_CONFIG:
        s->avbufm_registers[offset] = value & 0x000007FF;
    break;
    case AV_BUF_DITHER_CONFIG_MAX:
    case AV_BUF_DITHER_CONFIG_MIN:
        s->avbufm_registers[offset] = value & 0x00000FFF;
    break;
    case AV_BUF_PATTERN_GEN_SELECT:
        s->avbufm_registers[offset] = value & 0xFFFFFF03;
    break;
    case AV_BUF_AUD_VID_CLK_SOURCE:
        s->avbufm_registers[offset] = value & 0x00000007;
    break;
    case AV_BUF_SRST_REG:
        /*
         * TODO: Reset the Audio Video Buffer Manager module?
         */
        s->avbufm_registers[offset] = value & 0x00000002;
    break;
    case AV_BUF_AUDIO_CH_CONFIG:
        s->avbufm_registers[offset] = value & 0x00000003;
    break;
    case AV_BUF_GRAPHICS_COMP0_SCALE_FACTOR:
    case AV_BUF_GRAPHICS_COMP1_SCALE_FACTOR:
    case AV_BUF_GRAPHICS_COMP2_SCALE_FACTOR:
    case AV_BUF_VIDEO_COMP0_SCALE_FACTOR:
    case AV_BUF_VIDEO_COMP1_SCALE_FACTOR:
    case AV_BUF_VIDEO_COMP2_SCALE_FACTOR:
        s->avbufm_registers[offset] = value & 0x0000FFFF;
    break;


    case AV_BUF_LIVE_VIDEO_COMP0_SF:
    case AV_BUF_LIVE_VIDEO_COMP1_SF:
    case AV_BUF_LIVE_VIDEO_COMP2_SF:
    case AV_BUF_LIVE_VID_CONFIG:
    case AV_BUF_LIVE_GFX_COMP0_SF:
    case AV_BUF_LIVE_GFX_COMP1_SF:
    case AV_BUF_LIVE_GFX_COMP2_SF:
    case AV_BUF_LIVE_GFX_CONFIG:
    case AV_BUF_NON_LIVE_LATENCY:
    case AV_BUF_STC_CONTROL:
    case AV_BUF_STC_INIT_VALUE0:
    case AV_BUF_STC_INIT_VALUE1:
    case AV_BUF_STC_ADJ:
    case AV_BUF_STC_VIDEO_VSYNC_TS_REG0:
    case AV_BUF_STC_VIDEO_VSYNC_TS_REG1:
    case AV_BUF_STC_EXT_VSYNC_TS_REG0:
    case AV_BUF_STC_EXT_VSYNC_TS_REG1:
    case AV_BUF_STC_CUSTOM_EVENT_TS_REG0:
    case AV_BUF_STC_CUSTOM_EVENT_TS_REG1:
    case AV_BUF_STC_CUSTOM_EVENT2_TS_REG0:
    case AV_BUF_STC_CUSTOM_EVENT2_TS_REG1:
    case AV_BUF_STC_SNAPSHOT0:
    case AV_BUF_STC_SNAPSHOT1:
    case AV_BUF_HCOUNT_VCOUNT_INT0:
    case AV_BUF_HCOUNT_VCOUNT_INT1:
        /*
         * Non implemented.
         */
    break;
    default:
        s->avbufm_registers[offset] = value;
    break;
    }
}

static uint64_t avbufm_read(void *opaque, hwaddr offset, unsigned size)
{
    XilinxDPState *s = XILINX_DP(opaque);
    assert(size == 4);
    assert((offset % 4) == 0);

    offset = offset >> 2;

    return s->avbufm_registers[offset];
}

static const MemoryRegionOps avbufm_ops = {
    .read = avbufm_read,
    .write = avbufm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void dpdma_update_display(void *opaque)
{
    XilinxDPState *s = XILINX_DP(opaque);
    DisplaySurface *surface = qemu_console_surface(s->console);

    if ((s->core_registers[DP_TRANSMITTER_ENABLE] & 0x01) == 0) {
        return;
    }

    int64_t time = get_clock();

    s->core_registers[DP_INT_STATUS] |= (1 << 13);
    dp_update_irq(s);

    /*
     * Trigger the DMA channel.
     */
    if (!xilinx_dpdma_start_operation(s->dpdma, 3)) {
        /*
         * An error occured don't do anything with the data..
         * Trigger an underflow interrupt.
         */
        s->core_registers[DP_INT_STATUS] |= (1 << 21);
        dp_update_irq(s);
        return;
    }

    /*
     * XXX: Get data from other channel and do the blending if there is any
     *      blending to do.
     */

    /*
     * XXX: We might want to update only what changed.
     */
    dpy_gfx_update(s->console, 0, 0, surface_width(surface),
                                     surface_height(surface));

    time = get_clock() - time;
    DPRINTF("Time elapsed: %li\n", time);
}

static void dpdma_invalidate_display(void *opaque)
{

}

static const GraphicHwOps dpdma_gfx_ops = {
    .invalidate  = dpdma_invalidate_display,
    .gfx_update  = dpdma_update_display,
};

static void dp_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    XilinxDPState *s = XILINX_DP(obj);

    memory_region_init(&s->container, obj, TYPE_XILINX_DP, 0xC050);

    memory_region_init_io(&s->core_iomem, obj, &dp_ops, s, TYPE_XILINX_DP
                          ".core", 0x3AF);
    memory_region_add_subregion(&s->container, 0x0000, &s->core_iomem);

    memory_region_init_io(&s->vblend_iomem, obj, &vblend_ops, s, TYPE_XILINX_DP
                          ".v_blend", 0x1DF);
    memory_region_add_subregion(&s->container, 0xA000, &s->vblend_iomem);

    memory_region_init_io(&s->avbufm_iomem, obj, &avbufm_ops, s, TYPE_XILINX_DP
                          ".av_buffer_manager", 0x238);
    memory_region_add_subregion(&s->container, 0xB000, &s->avbufm_iomem);

    sysbus_init_mmio(sbd, &s->container);

    sysbus_init_irq(sbd, &s->irq);

    object_property_add_link(obj, "dpdma", TYPE_XILINX_DPDMA,
                             (Object **) &s->dpdma,
                             dp_set_dpdma,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &error_abort);
    /*
     * Initialize AUX Bus.
     */
    s->aux_bus = aux_init_bus(DEVICE(obj), "aux");

    /*
     * Initialize DPCD and EDID..
     */
    s->dpcd = DPCD(aux_create_slave(s->aux_bus, "dpcd", 0x00000));
    s->edid = I2CDDC(qdev_create(BUS(aux_get_i2c_bus(s->aux_bus)), "i2c-ddc"));
    i2c_set_slave_address(I2C_SLAVE(s->edid), 0x50);
}

static void dp_realize(DeviceState *dev, Error **errp)
{
    XilinxDPState *s = XILINX_DP(dev);
    DisplaySurface *surface;

    s->console = graphic_console_init(dev, 0, &dpdma_gfx_ops, s);
    surface = qemu_console_surface(s->console);
    xilinx_dpdma_set_host_data_location(s->dpdma, 3, surface_data(surface));
    fifo_create8(&s->rx_fifo, 16);
    fifo_create8(&s->tx_fifo, 16);
}

static void dp_reset(DeviceState *dev)
{
    XilinxDPState *s = XILINX_DP(dev);

    /*
     * Reset the Display Port registers.
     */
    memset(s->core_registers, 0, sizeof(s->core_registers));
    s->core_registers[DP_VERSION_REGISTER] = 0x04010000;
    s->core_registers[DP_CORE_ID] = 0x01020000;
    s->core_registers[DP_REPLY_STATUS] = 0x00000010;
    s->core_registers[DP_MSA_TRANSFER_UNIT_SIZE] = 0x00000040;
    s->core_registers[DP_INIT_WAIT] = 0x00000020;
    s->core_registers[DP_PHY_RESET] = 0x00010003;
    s->core_registers[DP_INT_MASK] = 0xFFFFF03F;

    /*
     * XXX: We are ready so simply reset that to 0x43, some bit missing from the
     *      documentation.
     */
    s->core_registers[DP_PHY_STATUS] = 0x00000043;

    /*
     * XXX: Assume we have something connected on the AUX connector.
     */
    s->core_registers[DP_INTERRUPT_SIGNAL_STATE] = 0x00000001;

    /*
     * Video Blender register reset.
     */
    s->vblend_registers[V_BLEND_RGB2YCBCR_COEFF0] = 0x00001000;
    s->vblend_registers[V_BLEND_RGB2YCBCR_COEFF4] = 0x00001000;
    s->vblend_registers[V_BLEND_RGB2YCBCR_COEFF8] = 0x00001000;
    s->vblend_registers[V_BLEND_IN1CSC_COEFF0] = 0x00001000;
    s->vblend_registers[V_BLEND_IN1CSC_COEFF4] = 0x00001000;
    s->vblend_registers[V_BLEND_IN1CSC_COEFF8] = 0x00001000;
    s->vblend_registers[V_BLEND_IN2CSC_COEFF0] = 0x00001000;
    s->vblend_registers[V_BLEND_IN2CSC_COEFF4] = 0x00001000;
    s->vblend_registers[V_BLEND_IN2CSC_COEFF8] = 0x00001000;

    /*
     * Audio Video Buffer Manager register reset.
     */
    s->avbufm_registers[AV_BUF_NON_LIVE_LATENCY] = 0x00000180;
    s->avbufm_registers[AV_BUF_OUTPUT_AUDIO_VIDEO_SELECT] = 0x00000008;
    s->avbufm_registers[AV_BUF_DITHER_CONFIG_MAX] = 0x00000FFF;
    s->avbufm_registers[AV_BUF_GRAPHICS_COMP0_SCALE_FACTOR] = 0x00010101;
    s->avbufm_registers[AV_BUF_GRAPHICS_COMP1_SCALE_FACTOR] = 0x00010101;
    s->avbufm_registers[AV_BUF_GRAPHICS_COMP2_SCALE_FACTOR] = 0x00010101;
    s->avbufm_registers[AV_BUF_VIDEO_COMP0_SCALE_FACTOR] = 0x00010101;
    s->avbufm_registers[AV_BUF_VIDEO_COMP1_SCALE_FACTOR] = 0x00010101;
    s->avbufm_registers[AV_BUF_VIDEO_COMP2_SCALE_FACTOR] = 0x00010101;
    s->avbufm_registers[AV_BUF_LIVE_VIDEO_COMP0_SF] = 0x00010101;
    s->avbufm_registers[AV_BUF_LIVE_VIDEO_COMP1_SF] = 0x00010101;
    s->avbufm_registers[AV_BUF_LIVE_VIDEO_COMP2_SF] = 0x00010101;
    s->avbufm_registers[AV_BUF_LIVE_GFX_COMP0_SF] = 0x00010101;
    s->avbufm_registers[AV_BUF_LIVE_GFX_COMP1_SF] = 0x00010101;
    s->avbufm_registers[AV_BUF_LIVE_GFX_COMP2_SF] = 0x00010101;

    dp_aux_clear_rx_fifo(s);
    s->current_graphic_fmt = PIXMAN_x8r8g8b8;
    dp_recreate_surface(s);
}

static void dp_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = dp_realize;
    dc->vmsd = &vmstate_dp;
    dc->reset = dp_reset;
}

static const TypeInfo dp_info = {
    .name          = TYPE_XILINX_DP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XilinxDPState),
    .instance_init = dp_init,
    .class_init    = dp_class_init,
};

static void dp_register_types(void)
{
    type_register_static(&dp_info);
}

type_init(dp_register_types)
