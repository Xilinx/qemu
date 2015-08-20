
/*
 *  QEMU model of the Xilinx framebuffer
 *    Derived from milkymist.c
 *
 *  Copyright (c) 2012 Peter Ryser <ryserp@xilinx.com>
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
 *
 */

#include "hw.h"
#include "sysbus.h"
#include "trace.h"
#include "console.h"
#include "framebuffer.h"
#include "pixel_ops.h"
#include "qemu-error.h"

#define BITS 32
#include "xilinx_fb.h"

#define FB_HRES     640
#define FB_VRES     480

struct XilinxFbState {
    SysBusDevice busdev;
    MemoryRegion regs_region;
    DisplayState *ds;

    int invalidate;
    uint32_t fb_offset;
    uint32_t fb_mask;
};
typedef struct XilinxFbState XilinxFbState;

static void xilinx_fb_update_display(void *opaque)
{
    XilinxFbState *s = opaque;
    int first = 0;
    int last = 0;
    drawfn fn;

    switch (ds_get_bits_per_pixel(s->ds)) {
    case 0:
        return;
    case 32:
        fn = draw_line_32;
        break;
    default:
        hw_error("xilinx_fb: bad color depth\n");
        break;
    }

    framebuffer_update_display(s->ds, sysbus_address_space(&s->busdev),
                               s->fb_offset,
                               FB_HRES,
                               FB_VRES,
                               FB_HRES * 4,
                               FB_HRES * 4,
                               0,
                               s->invalidate,
                               fn,
                               NULL,
                               &first, &last);

    if (first >= 0) {
        dpy_update(s->ds, 0, first, FB_HRES, last - first + 1);
    }
    s->invalidate = 0;
}

static void xilinx_fb_invalidate_display(void *opaque)
{
    XilinxFbState *s = opaque;
    s->invalidate = 1;

}

static void xilinx_fb_resize(XilinxFbState *s)
{
    qemu_console_resize(s->ds, FB_HRES, FB_VRES);
    s->invalidate = 1;
}

static const MemoryRegionOps xilinx_fb_mmio_ops = {
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int xilinx_fb_init(SysBusDevice *dev)
{
    XilinxFbState *s = FROM_SYSBUS(typeof(*s), dev);

    s->ds = graphic_console_init(xilinx_fb_update_display,
                                 xilinx_fb_invalidate_display,
                                 NULL, NULL, s);

    return 0;
}

static int xilinx_fb_post_load(void *opaque, int version_id)
{
    xilinx_fb_invalidate_display(opaque);
    return 0;
}

static const VMStateDescription vmstate_xilinx_fb = {
    .name = "xilinx_fb",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .post_load = xilinx_fb_post_load,
    .fields      = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static SysBusDeviceInfo xilinx_fb_info = {
    .init = xilinx_fb_init,
    .qdev.name  = "xilinx_fb",
    .qdev.size  = sizeof(XilinxFbState),
    .qdev.vmsd  = &vmstate_xilinx_fb,
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("fb_offset", XilinxFbState, fb_offset, 0x0),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void xilinx_fb_register(void)
{
    sysbus_register_withprop(&xilinx_fb_info);
}

device_init(xilinx_fb_register)
