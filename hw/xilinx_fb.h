/*
/*
 *  QEMU model of the Xilinx framebuffer
 *    Derived from milkymist-vgafb_template.h
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

#if BITS == 32
#define COPY_PIXEL(to, r, g, b)                    \
    do {                                           \
        *(uint32_t *)to = rgb_to_pixel32(r, g, b); \
        to += 4;                                   \
    } while (0)
#else
#error unknown bit depth
#endif

static void glue(draw_line_, BITS)(void *opaque, uint8_t *d, const uint8_t *s,
        int width, int deststep)
{
    uint8_t r, g, b;

    while (width--) {
	b=s[0];
	g=s[1];
	r=s[2];
        COPY_PIXEL(d, r, g, b);
        s += 4;
    }
}

#undef BITS
#undef COPY_PIXEL
