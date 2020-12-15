/*
 * Implementation of Interrupt redirect component for ZynqMP ACPUs and RCPUs.
 * Based on power state of a CPU (one of ACPUs or RCPUs) interrupt lines going
 * from GIC (IRQ, FIQ, VIRQ and VFIQ for ACPUs or IRQ for RPCUs) are directed
 * either to PMU (OR'ed) or to the CPU.
 *
 * 2014 Aggios, Inc.
 *
 * Written by Strahinja Jankovic <strahinja.jankovic@aggios.com>
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
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/register.h"
#include "qemu/bitops.h"
#include "qemu/log.h"

#include "hw/fdt_generic_util.h"

#define TYPE_XILINX_ZYNQMP_INTC_REDIRECT "xlnx.zynqmp-intc-redirect"

#define XILINX_ZYNQMP_INTC_REDIRECT(obj) \
     OBJECT_CHECK(INTCRedirect, (obj), TYPE_XILINX_ZYNQMP_INTC_REDIRECT)

#define NUM_LINES_FROM_GIC  64

typedef struct INTCRedirect {
    /* private */
    DeviceState parent;
    /* public */

    qemu_irq cpu_out[NUM_LINES_FROM_GIC];
    qemu_irq pmu_out;

    bool cpu_pwrdwn_en;
    uint64_t irq_in;

} INTCRedirect;

static void intc_redirect_update_irqs(void *opaque)
{
    INTCRedirect *s = XILINX_ZYNQMP_INTC_REDIRECT(opaque);
    bool gic_pmu_int = 0;
    unsigned int i;

    /* If CPU has set PWRDWN to 1, direct interrupts to PMU.  */
    if (s->cpu_pwrdwn_en) {
        gic_pmu_int = !!s->irq_in;
    }
    qemu_set_irq(s->pmu_out, gic_pmu_int);

    /* Always propagate IRQs between GIC and APU.  */
    for (i = 0; i < NUM_LINES_FROM_GIC; i++) {
        qemu_set_irq(s->cpu_out[i], !!(s->irq_in & (1U << i)));
    }
}

static void intc_redirect_in_from_gic(void *opaque, int irq, int level)
{
    INTCRedirect *s = XILINX_ZYNQMP_INTC_REDIRECT(opaque);

    s->irq_in = deposit32(s->irq_in, irq, 1, level);
    intc_redirect_update_irqs(s);
}

static void intc_redirect_pwr_cntrl_enable(void *opaque, int irq, int level)
{
    INTCRedirect *s = XILINX_ZYNQMP_INTC_REDIRECT(opaque);

    s->cpu_pwrdwn_en = level;
    intc_redirect_update_irqs(s);
}

static void intc_redirect_init(Object *obj)
{
    INTCRedirect *s = XILINX_ZYNQMP_INTC_REDIRECT(obj);
    DeviceState *dev = DEVICE(obj);

    qdev_init_gpio_in_named(dev, intc_redirect_in_from_gic, "gic_in",
                            NUM_LINES_FROM_GIC);
    qdev_init_gpio_out_named(dev, s->cpu_out, "cpu_out", NUM_LINES_FROM_GIC);
    qdev_init_gpio_out_named(dev, &s->pmu_out, "pmu_out", 1);
    qdev_init_gpio_in_named(dev, intc_redirect_pwr_cntrl_enable,
                            "cpu_pwrdwn_en", 1);
}

static int intc_redirect_get_irq(FDTGenericIntc *obj, qemu_irq *irqs,
                          uint32_t *cells, int ncells, int max,
                          Error **errp)
{
    if (cells[0] >= NUM_LINES_FROM_GIC) {
        error_setg(errp, "ZynqMP intc redirect only supports %u interrupts,"
                   "index %" PRIu32 " requested", NUM_LINES_FROM_GIC, cells[0]);
        return 0;
    }

    *irqs = qdev_get_gpio_in(DEVICE(obj), cells[0]);
    return 1;
};

/* Interrupt GPIOs are used to connect to CPU, and regular GPIOs
 * for connection to PMU.
 */
static const FDTGenericGPIOSet intc_redirect_client_gpios[] = {
    {
        .names = &fdt_generic_gpio_name_set_interrupts,
        .gpios = (FDTGenericGPIOConnection[]) {
            { .name = "cpu_out",        .range = NUM_LINES_FROM_GIC },
            { },
        },
    },
    {
        .names = &fdt_generic_gpio_name_set_gpio,
        .gpios = (FDTGenericGPIOConnection[]) {
            { .name = "pmu_out",         .fdt_index = 0, },
            { .name = "cpu_pwrdwn_en",   .fdt_index = 1, },
            { },
        },
    },
    { },
};
static const FDTGenericGPIOSet intc_redirect_controller_gpios[] = {
    {
        .names = &fdt_generic_gpio_name_set_interrupts,
        .gpios = (FDTGenericGPIOConnection[]) {
            { .name = "gic_in",        .range = NUM_LINES_FROM_GIC },
            { },
        },
    },
    { },
};

static void intc_redirect_class_init(ObjectClass *oc, void *data)
{
    FDTGenericIntcClass *fgic = FDT_GENERIC_INTC_CLASS(oc);
    FDTGenericGPIOClass *fggc = FDT_GENERIC_GPIO_CLASS(oc);

    fgic->get_irq = intc_redirect_get_irq;
    fggc->client_gpios = intc_redirect_client_gpios;
    fggc->controller_gpios = intc_redirect_controller_gpios;
}

static const TypeInfo intc_redirect_info = {
    .name          = TYPE_XILINX_ZYNQMP_INTC_REDIRECT,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(INTCRedirect),
    .instance_init = intc_redirect_init,
    .class_init    = intc_redirect_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_FDT_GENERIC_INTC },
        { TYPE_FDT_GENERIC_GPIO },
        { },
    },
};

static void intc_redirect_register_types(void)
{
    type_register_static(&intc_redirect_info);
}

type_init(intc_redirect_register_types)
