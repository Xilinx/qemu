/*
 * QEMU model of the Xilinx ZynqMP Real Time Clock (RTC).
 *
 * Copyright (c) 2017 Xilinx Inc.
 *
 * Written-by: Alistair Francis <alistair.francis@xilinx.com>
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

#include "qemu/osdep.h"
#include <stdint.h>
#include "qemu-common.h"
#include "hw/sysbus.h"
#include "hw/register.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "qemu/cutils.h"
#include "sysemu/sysemu.h"
#include "trace.h"
#include "hw/rtc/xlnx-zynqmp-rtc.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "qemu/timer.h"

#ifndef XLNX_ZYNQMP_RTC_ERR_DEBUG
#define XLNX_ZYNQMP_RTC_ERR_DEBUG 0
#endif

#if XLNX_ZYNQMP_RTC_ERR_DEBUG
    #define DPRINT(fmt, args...) do { \
            if (XLNX_ZYNQMP_RTC_ERR_DEBUG) { \
                qemu_log(fmt, ##args); \
            } \
        } while (0)

    #define DPRINT_TM(fmt, args...) do { \
            if (XLNX_ZYNQMP_RTC_ERR_DEBUG) { \
                qemu_log("[%lld] -> " fmt,\
                        qemu_clock_get_ns(rtc_clock) / NANOSECONDS_PER_SECOND,\
                        ##args);\
            } \
        } while (0)
#else
    #define DPRINT(fmt, args...) do {} while (0)
    #define DPRINT_TM(fmt, args...) do {} while (0)
#endif

enum version_id {
    IP_VERSION_1_0_1 = 0,
    IP_VERSION_2_0_0 = 1
};

struct version_item_lookup {
    enum version_id id;
    const char *str;
};

static struct version_item_lookup version_table_lookup[] = {
    { IP_VERSION_1_0_1, "1.0.1" },
    { IP_VERSION_2_0_0, "2.0.0" }
};

static Property xlnx_rtc_properties[] = {
    DEFINE_PROP_STRING("version", XlnxZynqMPRTC, cfg.version),
    DEFINE_PROP_END_OF_LIST(),
};

/* Returns the current host time in seconds. */
static uint32_t get_host_time_now(void)
{
    int64_t host_time_now = qemu_clock_get_ns(rtc_clock);
    return host_time_now / NANOSECONDS_PER_SECOND;
}

/* Returns the qemu time (time set with the -rtc command line) in seconds. */
static uint32_t get_qemu_time_now(XlnxZynqMPRTC *s)
{
    return get_host_time_now() - s->tick_offset;
}

/* Return the guest time in seconds. */
static uint32_t get_guest_time_now(XlnxZynqMPRTC *s)
{
    return get_qemu_time_now(s) - s->guest_offset;
}

/*Return the designated Guest Time in Host time. */
static uint32_t host_time_from_guest(XlnxZynqMPRTC *s, uint32_t guest_time)
{
    return s->tick_offset + s->guest_offset + guest_time;
}

static void rtc_int_update_irq(XlnxZynqMPRTC *s)
{
    uint32_t pending = s->regs[R_RTC_INT_STATUS] & ~s->regs[R_RTC_INT_MASK];
    qemu_set_irq(s->irq_rtc_int[0],
                 !!(pending & R_RTC_INT_STATUS_ALARM_MASK));
    qemu_set_irq(s->irq_rtc_int[1],
                 !!(pending & R_RTC_INT_STATUS_SECONDS_MASK));

}

static void addr_error_int_update_irq(XlnxZynqMPRTC *s)
{
    bool pending = s->regs[R_ADDR_ERROR] & ~s->regs[R_ADDR_ERROR_INT_MASK];
    qemu_set_irq(s->irq_addr_error_int, pending);
}

static void update_alarm(XlnxZynqMPRTC *s)
{
    uint32_t alarm;
    uint32_t host_time_now;

    timer_del(s->alarm);
    /*
     * Converts the guest alarm time to a host alarm time as all internal
     * QEMUTimers are based on host time, this will also take care of all
     * overflows.
     */
    alarm = host_time_from_guest(s, s->regs[R_ALARM]);
    host_time_now = get_host_time_now();

    /*
     * If the alarm time is earlier than the current host time the timer
     * callback will be called instantaneously. To avoid this we will only
     * arm the timer if the alarm value is at a time later than the current
     * host time. Conversion from Guest Time to Host time is taken care of
     * by the call to host_time_from_guest().
     */
    if (alarm > host_time_now) {
        timer_mod(s->alarm, alarm * NANOSECONDS_PER_SECOND);
    } else if (alarm == host_time_now) {
        s->regs[R_RTC_INT_STATUS] = FIELD_DP32(s->regs[R_RTC_INT_STATUS],
                                               RTC_INT_STATUS, ALARM, 1);
        /* Raise the interrupt if conditions are met. */
        rtc_int_update_irq(s);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%06d : %s() attempting to arm the "
                      "alarm timer with a timestamp that is earlier than "
                      "the current time: \talarm=%u,\n\tguest time=%u,\n",
                      __LINE__, __func__, alarm, get_guest_time_now(s));
    }
}

static void update_seconds(XlnxZynqMPRTC *s)
{
    uint32_t next_sec;

    timer_del(s->sec_tick);
    /* Re-arm the seconds tick and go. */
    next_sec = get_host_time_now() + 1;
    timer_mod(s->sec_tick, next_sec * NANOSECONDS_PER_SECOND);
}

static void rtc_set_time_write_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxZynqMPRTC *s = XLNX_ZYNQMP_RTC(reg->opaque);

    s->regs[R_SET_TIME_WRITE] = val64;
    /* Will force to read the last value written as per controller spec.*/
    s->regs[R_SET_TIME_READ] = val64;
    /* Update the guest offset to reflect the new time set. */
    s->guest_offset = get_qemu_time_now(s) - s->regs[R_SET_TIME_READ];
    DPRINT_TM("%06d : %s()\n", __LINE__, __func__);
    DPRINT_TM("Time Marks:\n\tQEMU Time = %u,\n \tHost Time = %u,\n"
              " \ts->tick_offset = %u,\n",
              get_qemu_time_now(s), get_host_time_now(), s->tick_offset);
    DPRINT("\tguest_offset = %010u\n", s->guest_offset);
}

static void rtc_calib_write_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxZynqMPRTC *s = XLNX_ZYNQMP_RTC(reg->opaque);

    s->regs[R_CALIB_READ] = val64;
    /*
     * Since we are not simulating calibration, force CURRENT_TICK
     * to always read the MAX_TICK.
     */
    s->regs[R_CURRENT_TICK] = FIELD_EX32(s->regs[R_CALIB_READ],
                                       CALIB_WRITE, MAX_TICK);
}

static uint64_t current_time_postr(RegisterInfo *reg, uint64_t val64)
{
    XlnxZynqMPRTC *s = XLNX_ZYNQMP_RTC(reg->opaque);
    uint32_t guest_time_now = get_guest_time_now(s);
    return guest_time_now;
}

static void alarm_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxZynqMPRTC *s = XLNX_ZYNQMP_RTC(reg->opaque);

    s->regs[R_ALARM] = val64;
    update_alarm(s);
}

static uint64_t rtc_int_en_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxZynqMPRTC *s = XLNX_ZYNQMP_RTC(reg->opaque);

    s->regs[R_RTC_INT_MASK] &= ~val64;
    if (FIELD_EX32(s->regs[R_RTC_INT_MASK], RTC_INT_MASK, SECONDS) == 1) {
        update_seconds(s);
    }
    rtc_int_update_irq(s);
    return 0;
}

static uint64_t rtc_int_dis_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxZynqMPRTC *s = XLNX_ZYNQMP_RTC(reg->opaque);

    s->regs[R_RTC_INT_MASK] |= val64;
    rtc_int_update_irq(s);
    return 0;
}

static void rtc_int_status_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxZynqMPRTC *s = XLNX_ZYNQMP_RTC(reg->opaque);

    if (FIELD_EX32(s->regs[R_RTC_INT_STATUS], RTC_INT_STATUS, SECONDS) == 0) {
        update_seconds(s);
    }
    rtc_int_update_irq(s);
}

static void addr_error_set_status(void *opaque)
{
    RegisterInfo *ri = *((RegisterInfo **)((RegisterInfoArray*)opaque)->r);
    XlnxZynqMPRTC *s = (XlnxZynqMPRTC *)ri->opaque;

    s->regs[R_ADDR_ERROR] = FIELD_DP32(s->regs[R_ADDR_ERROR],
                                       ADDR_ERROR, STATUS, 1);
    addr_error_int_update_irq(s);
}

static void addr_error_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxZynqMPRTC *s = XLNX_ZYNQMP_RTC(reg->opaque);
    addr_error_int_update_irq(s);
}

static uint64_t addr_error_int_en_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxZynqMPRTC *s = XLNX_ZYNQMP_RTC(reg->opaque);

    s->regs[R_ADDR_ERROR_INT_MASK] &= ~val64;
    addr_error_int_update_irq(s);
    return 0;
}

static uint64_t addr_error_int_dis_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxZynqMPRTC *s = XLNX_ZYNQMP_RTC(reg->opaque);

    s->regs[R_ADDR_ERROR_INT_MASK] |= val64;;
    addr_error_int_update_irq(s);
    return 0;
}

static const RegisterAccessInfo rtc_regs_info[] = {
    {   .name = "SET_TIME_WRITE", .addr = A_SET_TIME_WRITE,
        .post_write = rtc_set_time_write_postw,
    },{ .name = "SET_TIME_READ", .addr = A_SET_TIME_READ,
        .ro = 0xffffffff,
    },{ .name = "CALIB_WRITE", .addr = A_CALIB_WRITE,
        .post_write = rtc_calib_write_postw,
    },{ .name = "CALIB_READ", .addr = A_CALIB_READ,
        .ro = 0x1fffff,
    },{ .name = "CURRENT_TIME", .addr = A_CURRENT_TIME,
        .ro = 0xffffffff,
        .post_read = current_time_postr,
    },{ .name = "CURRENT_TICK", .addr = A_CURRENT_TICK,
        .ro = 0xffff,
    },{ .name = "ALARM", .addr = A_ALARM,
        .post_write = alarm_postw,
        .reset = 0x00000000,
    },{ .name = "RTC_INT_STATUS", .addr = A_RTC_INT_STATUS,
        .w1c = 0x3,
        .post_write = rtc_int_status_postw,
    },{ .name = "RTC_INT_MASK", .addr = A_RTC_INT_MASK,
        .reset = 0x3,
        .ro = 0x3,
    },{ .name = "RTC_INT_EN", .addr = A_RTC_INT_EN,
        .pre_write = rtc_int_en_prew,
    },{ .name = "RTC_INT_DIS", .addr = A_RTC_INT_DIS,
        .pre_write = rtc_int_dis_prew,
    },{ .name = "ADDR_ERROR", .addr = A_ADDR_ERROR,
        .w1c = 0x1,
        .post_write = addr_error_postw,
    },{ .name = "ADDR_ERROR_INT_MASK", .addr = A_ADDR_ERROR_INT_MASK,
        .reset = 0x1,
        .ro = 0x1,
    },{ .name = "ADDR_ERROR_INT_EN", .addr = A_ADDR_ERROR_INT_EN,
        .pre_write = addr_error_int_en_prew,
    },{ .name = "ADDR_ERROR_INT_DIS", .addr = A_ADDR_ERROR_INT_DIS,
        .pre_write = addr_error_int_dis_prew,
    },{ .name = "CONTROL", .addr = A_CONTROL,
        .reset = 0x1000000,
        .rsvd = 0x70fffffe,
    },{ .name = "SAFETY_CHK", .addr = A_SAFETY_CHK,
    }
};

static const RegisterAccessInfo rtc_regs_control_v2_info = {
    .name = "CONTROL",   .addr = A_CONTROL,
    .reset = 0x2000000,  .rsvd = 0x70fffffe,
};

static enum version_id version_id_lookup(const char *str)
{
    uint32_t i;
    enum version_id version;

    version = IP_VERSION_1_0_1;

    if (str) {
        for (i = 0; i < ARRAY_SIZE(version_table_lookup); ++i) {
            if (!strcmp(str, version_table_lookup[i].str)) {
                version = version_table_lookup[i].id;
                break;
            }
        }
    }

    return version;
}

static void clear_time(XlnxZynqMPRTC *s)
{
    time_t qemu_time;
    /*
     * Host Time is determined by the host clock, the QEMU RTC clock ticks
     * off from.
     */
    uint32_t host_time = get_host_time_now();
    /*
     * QEMU Time is determined by the ISO8601 value passed to QEMU
     * in the command line using the -rtc command line option. If
     * the user omits the -rtc command line option then QEMU Time
     * is equal to the Host Time.
     */
    struct tm qemu_tm = {0};
    qemu_get_timedate(&qemu_tm, 0);
    qemu_time = mktimegm(&qemu_tm);
    /*
     * tick_offset tracks the delta in seconds between the Host Time and
     * QEMU Time.
     */
    s->tick_offset = host_time - qemu_time;
    /*
     * The Guest Time is the time set by the guest, to begin with we'll use
     * the QEMU Time as the Guest Time as this is what was passed at command
     * line. We'll apply the QEMU Time to the Guest Set Time Read/Write
     * registers. The Guest can change that by writing to the Set Time Write
     * Register.
     */
    s->regs[R_SET_TIME_WRITE] = qemu_time;
    s->regs[R_SET_TIME_READ] = qemu_time;
    s->guest_offset = qemu_time - s->regs[R_SET_TIME_READ];

    DPRINT_TM("%06d : %s()\n", __LINE__, __func__);
    DPRINT_TM("Time Marks:\n\tQEMU Time = %lu,\n\tHost Time = %u,\n"
              " \ts->tick_offset = %u,\n",
              qemu_time, host_time, s->tick_offset);
    DPRINT("\tguest_offset = %010u\n", s->guest_offset);
    DPRINT("\t%04u-%02u-%02u-T%02u:%02u:%02u (yyyy-mm-ddThh:mm:ss ISO-8601)\n",
           qemu_tm.tm_year + 1900, qemu_tm.tm_mon + 1, qemu_tm.tm_mday,
           qemu_tm.tm_hour, qemu_tm.tm_min, qemu_tm.tm_sec);
}

static void rtc_reset(DeviceState *dev)
{
    XlnxZynqMPRTC *s = XLNX_ZYNQMP_RTC(dev);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }

    if (version_id_lookup(s->cfg.version) == IP_VERSION_2_0_0) {
        s->regs_info[R_CONTROL].access = &rtc_regs_control_v2_info;
        register_reset(&s->regs_info[R_CONTROL]);
    }

    clear_time(s);
}

static void alarm_timeout_cb(void *opaque)
{
    XlnxZynqMPRTC *s = (XlnxZynqMPRTC *)opaque;

    s->regs[R_RTC_INT_STATUS] = FIELD_DP32(s->regs[R_RTC_INT_STATUS],
        RTC_INT_STATUS, ALARM, 1);
    /* Raise Alarm Interrupt Level If Unmasked. */
    rtc_int_update_irq(s);
}

static void second_timeout_cb(void *opaque)
{
    XlnxZynqMPRTC *s = (XlnxZynqMPRTC *)opaque;

    s->regs[R_RTC_INT_STATUS] = FIELD_DP32(s->regs[R_RTC_INT_STATUS],
        RTC_INT_STATUS, SECONDS, 1);
    /* Raise Seconds Interrupt Level If Unmasked. */
    rtc_int_update_irq(s);
}

static uint64_t rtc_register_read_memory(void *opaque, hwaddr addr,
                                         unsigned size)
{
    if (addr >= (XLNX_ZYNQMP_RTC_R_MAX * 4)) {
        DPRINT_TM("%06d : %s()\n", __LINE__, __func__);
        DPRINT_TM("\tAttempting to Read from invalid RTC Memory"
                  " Space 0x%08lx\n", addr);
        addr_error_set_status(opaque);
        return (uint64_t)0;
    }

    return register_read_memory(opaque, addr, size);
}

static void rtc_register_write_memory(void *opaque, hwaddr addr, uint64_t value,
                                      unsigned size)
{
    if (addr >= (XLNX_ZYNQMP_RTC_R_MAX * 4)) {
        DPRINT_TM("%06d : %s()\n", __LINE__, __func__);
        DPRINT_TM("\tAttempting to Write to invalid RTC Memory Space 0x%08lx\n",
                  addr);
        addr_error_set_status(opaque);
        return;
    }

    register_write_memory(opaque, addr, value, size);
}

static const MemoryRegionOps rtc_ops = {
    .read = rtc_register_read_memory,
    .write = rtc_register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void rtc_init(Object *obj)
{
    XlnxZynqMPRTC *s = XLNX_ZYNQMP_RTC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    memory_region_init(&s->iomem, obj, TYPE_XLNX_ZYNQMP_RTC,
                       XLNX_ZYNQMP_RTC_IO_REGION_SZ);

    reg_array =
        register_init_block32(DEVICE(obj), rtc_regs_info,
                              ARRAY_SIZE(rtc_regs_info),
                              s->regs_info, s->regs,
                              &rtc_ops,
                              XLNX_ZYNQMP_RTC_ERR_DEBUG,
                              XLNX_ZYNQMP_RTC_IO_REGION_SZ);
    memory_region_add_subregion(&s->iomem,
                                0x0,
                                &reg_array->mem);
    sysbus_init_mmio(sbd, &s->iomem);
    /* Error irq */
    sysbus_init_irq(sbd, &s->irq_addr_error_int);
    /* Alarm irq */
    sysbus_init_irq(sbd, &s->irq_rtc_int[0]);
    /* Seconds irq */
    sysbus_init_irq(sbd, &s->irq_rtc_int[1]);

    DPRINT_TM("%06d : %s()\n", __LINE__, __func__);
    clear_time(s);
    s->alarm = timer_new_ns(rtc_clock, alarm_timeout_cb, s);
    s->sec_tick = timer_new_ns(rtc_clock, second_timeout_cb, s);
}

static int rtc_pre_save(void *opaque)
{
    XlnxZynqMPRTC *s = opaque;
    int64_t now = qemu_clock_get_ns(rtc_clock) / NANOSECONDS_PER_SECOND;

    /* Add the time at migration. */
    s->tick_offset = s->tick_offset + now;

    return 0;
}

static int rtc_post_load(void *opaque, int version_id)
{
    XlnxZynqMPRTC *s = opaque;
    int64_t now = qemu_clock_get_ns(rtc_clock) / NANOSECONDS_PER_SECOND;

    /* Subtract the time after migration. This combined with the pre_save
     * action results in us having subtracted the time that the guest was
     * stopped to the offset.
     */
    s->tick_offset = s->tick_offset - now;

    return 0;
}

static const VMStateDescription vmstate_rtc = {
    .name = TYPE_XLNX_ZYNQMP_RTC,
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = rtc_pre_save,
    .post_load = rtc_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, XlnxZynqMPRTC, XLNX_ZYNQMP_RTC_R_MAX),
        VMSTATE_UINT32(tick_offset, XlnxZynqMPRTC),
        VMSTATE_END_OF_LIST(),
    }
};

static void rtc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = rtc_reset;
    device_class_set_props(dc, xlnx_rtc_properties);
    dc->vmsd = &vmstate_rtc;
}

static const TypeInfo rtc_info = {
    .name          = TYPE_XLNX_ZYNQMP_RTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxZynqMPRTC),
    .class_init    = rtc_class_init,
    .instance_init = rtc_init,
};

static const TypeInfo rtc_alias_info = {
    .name           = TYPE_XLNX_ZYNQMP_ALIAS_RTC,
    .parent         = TYPE_XLNX_ZYNQMP_RTC,
};

static void rtc_register_types(void)
{
    type_register_static(&rtc_info);
    type_register_static(&rtc_alias_info);
}

type_init(rtc_register_types)
