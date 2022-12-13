/*
 * Si5341 model
 *
 * Copyright (c) 2022 Advanced Micro Devices, Inc.
 *
 * Written by Frederic Konrad <fkonrad@amd.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * This models the si5341 i2c chip: A 4 inputs, 10 output clocks generator.
 *
 * By default if no properties are given it will be left unprogrammed:
 *   - In this case all the registers are zeros, and the kernel programs it
 *     (at least the linux kernel programs the root clock).
 *
 * It will be automagically programmed if the properties below are given:
 *   - In this case the model acts as if the configuration was written in the
 *     NVM, and the mux, numerators, denumerators and divisors registers are
 *     computed and programmed when the device is reset.
 *
 *  In any case this device only models the register accesses, so the guests
 *  can compute clock rates.
 */

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"

#define TYPE_SI5341 "si5341"
#define SI5341(obj)     \
    OBJECT_CHECK(Si5341State, (obj), TYPE_SI5341)

#define DEBUG_SI5341     0
#define DPRINTF(fmt, args...)                    \
    if (DEBUG_SI5341) {                          \
        qemu_log("%s: " fmt, __func__, ## args); \
    }

/*
 * PAGE_OFFSET and DEVICE_READY are accessible from all the pages.
 */
#define SI5341_DIE_REV_OFFSET            (0x00)
#define SI5341_PAGE_OFFSET               (0x01)
#define SI5341_PN_BASE_OFFSET(n)         (0x02 + (n))
#define SI5341_GRADE_OFFSET              (0x04)
#define SI5341_DEVICE_REV_OFFSET         (0x05)
#define SI5341_TEMP_GRADE_OFFSET         (0x09)
#define SI5341_PKG_ID_OFFSET             (0x0A)
#define SI5341_I2C_ADDR_OFFSET           (0x0B)
#define SI5341_STATUS_OFFSET             (0x0C)
#define SI5341_LOS_OFFSET                (0x0D)
#define SI5341_STICKY_STATUS_OFFSET      (0x11)
#define SI5341_STICKY_LOS_OFFSET         (0x12)
#define SI5341_STATUS_INTR_MASK_OFFSET   (0x17)
#define SI5341_LOS_INTR_MASK_OFFSET      (0x18)
#define SI5341_SOFT_RST_OFFSET           (0x1C)
#define SI5341_FINC_FDEC_OFFSET          (0x1D)
#define SI5341_SYNC_PDWN_HR_OFFSET       (0x1E)
#define SI5341_INPUT_CLK_SEL_OFFSET      (0x21)
#define SI5341_DEVICE_READY_OFFSET       (0xFE)
#define SI5341_CLK_OUT_MUX_INV_OFFSET(n) (0x10B + (n) * 0x5)
#define SI5341_M_NUM_OFFSET(n)           (0x235 + (n))
#define SI5341_M_DEN_OFFSET(n)           (0x23B + (n))
#define SI5341_R_DIV_OFFSET(m, n)        (0x24A + (n) + 0x3 * (m))
#define SI5341_N_NUM_OFFSET(m, n)        (0x302 + (n) + 0xB * (m))
#define SI5341_N_DEN_OFFSET(m, n)        (0x308 + (n) + 0xB * (m))
#define SI5341_N_UPDATE_OFFSET(m)        (0xB * (m))
#define SI5341_RMAX_OFFSET               (0xB58 + 1)

#define SI5341_MAX_PAGE                  (0xB)
#define SI5341_SYNTH_COUNT               (5)
#define SI5341_INPUT_IN0                 (0)
#define SI5341_INPUT_IN1                 (1)
#define SI5341_INPUT_IN2                 (2)
#define SI5341_INPUT_XA_XB               (3)
#define SI5341_MAX_INPUT                 (4)
#define SI5341_MAX_OUTPUT                (10)

enum si5341_events {
    IDEAL,
    ADDRESSING,
    ADDRESSING_DONE,
    WRITING,
    READING,
};

typedef struct Si5341 {
    I2CSlave i2c;

    uint8_t current_page;
    uint8_t addr;
    uint8_t state;
    uint8_t regs[SI5341_RMAX_OFFSET];
    uint32_t *input_rates;
    uint32_t input_rate_count;
    uint32_t *synth_rates;
    uint32_t synth_rate_count;
    uint32_t *output_rates;
    uint32_t output_rate_count;
    uint32_t *output_synth_sel;
    uint32_t output_synth_sel_count;
    uint8_t default_clock_sel;
} Si5341State;

/*
 * Return the register offset within s->regs for the current address, and page.
 */
static uint16_t si5341_get_register_offset(Si5341State *s)
{
    switch (s->addr) {
    case SI5341_PAGE_OFFSET:
    case SI5341_DEVICE_READY_OFFSET:
        return s->addr;
    default:
        return s->current_page * 0x100 + s->addr;
    }
}

static uint8_t si5341_read(I2CSlave *i2c)
{
    Si5341State *s = SI5341(i2c);
    uint16_t register_address = si5341_get_register_offset(s);

    DPRINTF("Read from 0x%2.2X, page: 0x%1.1X (0x%2.2x)\n", s->addr,
            s->current_page, s->regs[register_address]);
    return s->regs[register_address];
}

static int si5341_write(I2CSlave *i2c, uint8_t data)
{
    Si5341State *s = SI5341(i2c);
    uint16_t register_address;

    if (s->state == ADDRESSING) {
        DPRINTF("%s: 0x%2.2X -> addr\n", __func__, data);
        s->addr = data;
        s->state = ADDRESSING_DONE;
        return 0;
    }
    s->state = WRITING;

    DPRINTF("%s write 0x%2.2X at 0x%2.2X, page: 0x%1.1X\n", __func__, data,
            s->addr, s->current_page);
    if (s->addr == SI5341_PAGE_OFFSET) {
        if (data > SI5341_MAX_PAGE) {
            /*
             * Catch page out of range, drop a guest error, and ignore the
             * page switch.
             */
            qemu_log_mask(LOG_GUEST_ERROR,
                          "setting page above range: 0x%2.2X\n", data);
        } else {
            s->current_page = data;
        }
        s->addr++;
        return 0;
    }

    /*
     * Handle other writes.
     */
    register_address = si5341_get_register_offset(s);
    switch (register_address) {
    case SI5341_DIE_REV_OFFSET:
    case SI5341_PN_BASE_OFFSET(0):
    case SI5341_PN_BASE_OFFSET(1):
    case SI5341_GRADE_OFFSET:
    case SI5341_DEVICE_REV_OFFSET:
    case SI5341_TEMP_GRADE_OFFSET:
    case SI5341_PKG_ID_OFFSET:
    case SI5341_DEVICE_READY_OFFSET:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "writing a read only register: 0x%2.2X, page 0x%2.2X\n",
                      s->addr, s->current_page);
        break;
    default:
        s->regs[register_address] = data;
    }

    s->addr++;
    return 0;
}

/*
 * Push the 32bits denominator in the registers.
 */
static void si5341_push_den(uint32_t den, uint8_t *regs)
{
    int i;

    for (i = 0; i < 4; i++) {
        regs[i] = den & 0xff;
        den = den >> 8;
    }
}

/*
 * Push the 44bits numerator in the registers.
 */
static void si5341_push_num(uint64_t num, uint8_t *regs)
{
    int i;

    for (i = 0; i < 6; i++) {
        regs[i] = num & (i == 5 ? 0xf : 0xff);
        num = num >> 8;
    }
}

/*
 * Push the 24bits divider in the registers.
 */
static void si5341_push_divider(uint32_t num, uint8_t *regs)
{
    int i;

    for (i = 0; i < 3; i++) {
        regs[i] = num & 0xff;
        num >>= 8;
    }
}

static void si5341_reset(DeviceState *dev)
{
    Si5341State *s = SI5341(dev);
    int i;

    DPRINTF("%s\n", __func__);
    memset(s->regs, 0, sizeof(s->regs));

    /*
     * Chip ID.
     */
    s->regs[SI5341_PN_BASE_OFFSET(1)] = 0x53;
    s->regs[SI5341_PN_BASE_OFFSET(0)] = 0x41;
    s->regs[SI5341_GRADE_OFFSET] = 0x00;
    s->regs[SI5341_DEVICE_REV_OFFSET] = 0x00;

    /*
     * Put the device in a READY state.
     */
    s->regs[SI5341_DEVICE_READY_OFFSET] = 0x0F;

    /*
     * Clock input, can be IN0, IN1, IN2 or XA/XB set from property.
     */
    s->regs[SI5341_INPUT_CLK_SEL_OFFSET] = s->default_clock_sel << 1;

    if ((s->default_clock_sel >= s->input_rate_count)
        || !s->input_rates[s->default_clock_sel]) {
        /*
         * There isn't any default input frequency here.  Just give up and
         * leave the device unconfigured.
         */
        return;
    }

    /*
     * Compute the root numerator / denumerator to get 14Ghz, as the linux
     * kernel would do.
     */
    DPRINTF("%s - program the root numerator / denominator: %u / %u\n",
            __func__, 1400000000, s->input_rates[s->default_clock_sel] / 10);
    si5341_push_num(1400000000, &(s->regs[SI5341_M_NUM_OFFSET(0)]));
    si5341_push_den(s->input_rates[s->default_clock_sel] / 10,
                    &(s->regs[SI5341_M_DEN_OFFSET(0)]));

    /*
     * N{0..4} Synthetizer, program it only if the corresponding synth-rates
     * property has been set.
     */
    for (i = 0; i < SI5341_SYNTH_COUNT; i++) {
        if ((i < s->synth_rate_count) && (s->synth_rates[i])) {
            DPRINTF("%s - program the synth[%d] numerator / denominator:"
                    " %u / %u\n",
                    __func__, i, 1400000000, s->synth_rates[i] / 10);
            si5341_push_num(1400000000,
                            &(s->regs[SI5341_N_NUM_OFFSET(i, 0)]));
            si5341_push_den(s->synth_rates[i] / 10,
                            &(s->regs[SI5341_N_DEN_OFFSET(i, 0)]));
        }
    }

    /*
     * Output clocks, program it only if the corresponding output-synth-sel and
     * output-rates property has been set.
     */
    for (i = 0; i < SI5341_MAX_OUTPUT; i++) {
        if ((i < s->output_synth_sel_count)
         && (i < s->output_rate_count)
         && (s->output_rates[i])
         && (s->output_synth_sel[i] < SI5341_SYNTH_COUNT)
         && (s->output_synth_sel[i] < s->synth_rate_count)
         && (s->synth_rates[s->output_synth_sel[i]])) {
            DPRINTF("%s - program the output[%d] divider: %u\n",
                    __func__, i, s->synth_rates[s->output_synth_sel[i]]
                                 / (s->output_rates[i] * 2)
                                 - 1);
            si5341_push_divider(s->synth_rates[s->output_synth_sel[i]]
                                / (s->output_rates[i] * 2)
                                - 1,
                                &s->regs[SI5341_R_DIV_OFFSET(i, 0)]);
            DPRINTF("%s - program the mux for the output[%d]: %u\n",
                    __func__, i, s->output_synth_sel[i]);
            /*
             * And enable the output.
             */
            s->regs[SI5341_CLK_OUT_MUX_INV_OFFSET(i)] =
                s->output_synth_sel[i] | 0x80;
        }
    }
}

static int si5341_event(I2CSlave *i2c, enum i2c_event event)
{
    Si5341State *s = SI5341(i2c);

    switch (event) {
    case I2C_START_SEND:
        s->state = ADDRESSING;
        break;
    case I2C_START_RECV:
        s->state = READING;
        break;
    case I2C_FINISH:
    case I2C_NACK:
        s->state = IDEAL;
        break;
    default:
        return -1;
    }

    return 0;
}

static Property si5341_properties[] = {
    DEFINE_PROP_UINT8("default-clock-sel", Si5341State,
                      default_clock_sel, SI5341_INPUT_XA_XB),
    DEFINE_PROP_ARRAY("input-rates", Si5341State, input_rate_count,
                      input_rates, qdev_prop_uint32, uint32_t),
    DEFINE_PROP_ARRAY("synth-rates", Si5341State, synth_rate_count,
                      synth_rates,
                      qdev_prop_uint32, uint32_t),
    DEFINE_PROP_ARRAY("output-synth-sel", Si5341State, output_synth_sel_count,
                      output_synth_sel, qdev_prop_uint32, uint32_t),
    DEFINE_PROP_ARRAY("output-rates", Si5341State, output_rate_count,
                      output_rates, qdev_prop_uint32, uint32_t),
    DEFINE_PROP_END_OF_LIST(),
};

static void si5341_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->recv = si5341_read;
    k->send = si5341_write;
    k->event = si5341_event;
    dc->reset = si5341_reset;
    device_class_set_props(dc, si5341_properties);
}

static const TypeInfo si5341_info = {
    .name = TYPE_SI5341,
    .parent = TYPE_I2C_SLAVE,
    .class_init = si5341_class_init,
    .instance_size = sizeof(Si5341State),
};

static void si5341_register_type(void)
{
    type_register_static(&si5341_info);
}

type_init(si5341_register_type)
