#ifndef FDT_GENERIC_UTIL_H
#define FDT_GENERIC_UTIL_H

#include "qemu-common.h"
#include "fdt_generic.h"
#include "exec/memory.h"
#include "qom/object.h"

/* create a fdt_generic machine. the top level cpu irqs are required for
 * systems instantiating interrupt devices. The client is responsible for
 * destroying the returned FDTMachineInfo (using fdt_init_destroy_fdti)
 */

FDTMachineInfo *fdt_generic_create_machine(void *fdt, qemu_irq *cpu_irq);

/* get an irq for a device. The interrupt parent of a device is idenitified
 * and the specified irq (by the interrupts device-tree property) is retrieved
 */

qemu_irq *fdt_get_irq(FDTMachineInfo *fdti, char *node_path, int irq_idx,
                      bool *map_mode);

/* same as above, but poulates err with non-zero if something goes wrong, and
 * populates info with a human readable string giving some basic information
 * about the interrupt connection found (or not found). Both arguments are
 * optional (i.e. can be NULL)
 */

qemu_irq *fdt_get_irq_info(FDTMachineInfo *fdti, char *node_path, int irq_idx,
                           char * info, bool *map_mode);

#define TYPE_FDT_GENERIC_INTC "fdt-generic-intc"

#define FDT_GENERIC_INTC_CLASS(klass) \
     OBJECT_CLASS_CHECK(FDTGenericIntcClass, (klass), TYPE_FDT_GENERIC_INTC)
#define FDT_GENERIC_INTC_GET_CLASS(obj) \
    OBJECT_GET_CLASS(FDTGenericIntcClass, (obj), TYPE_FDT_GENERIC_INTC)
#define FDT_GENERIC_INTC(obj) \
     INTERFACE_CHECK(FDTGenericIntc, (obj), TYPE_FDT_GENERIC_INTC)

typedef struct FDTGenericIntc {
    /*< private >*/
    Object parent_obj;
} FDTGenericIntc;

typedef struct FDTGenericIntcClass {
    /*< private >*/
    InterfaceClass parent_class;

    /*< public >*/
    /**
     * get irq - Based on the FDT generic interrupt binding for this device
     * grab the irq(s) for the given interrupt cells description. In some device
     * tree bindings (E.G. ARM GIC with its PPI) a single interrupt cell-tuple
     * can describe more than one connection. So populates an array with all
     * relevant IRQs.
     *
     * @obj - interrupt controller to get irqs input for ("interrupt-parent")
     * @irqs - array to populate with irqs (must be >= @max length
     * @cells - interrupt cells values. Must be >= ncells length
     * @ncells - number of cells in @cells
     * @max - maximum number of irqs to return
     * @errp - Error condition
     *
     * @returns the number of interrupts populated in irqs. Undefined on error
     * (use errp for error checking). If it is valid for the interrupt
     * controller binding to specify no (or a disabled) connections it may
     * return 0 as a non-error.
     */

    int (*get_irq)(FDTGenericIntc *obj, qemu_irq *irqs, uint32_t *cells,
                   int ncells, int max, Error **errp);

    /**
     * auto_parent. An interrupt controller often infers its own interrupt
     * parent (usually a CPU or CPU cluster. This function allows an interrupt
     * controller to implement its own auto-connections. Is called if an
     * interrupt controller itself (detected via "interrupt-controller") has no
     * "interrupt-parent" node.
     *
     * @obj - Interrupt controller top attempt autoconnection
     * @errp - Error condition
     *
     * FIXME: More arguments need to be added for partial descriptions
     */

    void (*auto_parent)(FDTGenericIntc *obj, Error **errp);

} FDTGenericIntcClass;

#define TYPE_FDT_GENERIC_MMAP "fdt-generic-mmap"

#define FDT_GENERIC_MMAP_CLASS(klass) \
     OBJECT_CLASS_CHECK(FDTGenericMMapClass, (klass), TYPE_FDT_GENERIC_MMAP)
#define FDT_GENERIC_MMAP_GET_CLASS(obj) \
    OBJECT_GET_CLASS(FDTGenericMMapClass, (obj), TYPE_FDT_GENERIC_MMAP)
#define FDT_GENERIC_MMAP(obj) \
     INTERFACE_CHECK(FDTGenericMMap, (obj), TYPE_FDT_GENERIC_MMAP)

typedef struct FDTGenericMMap {
    /*< private >*/
    Object parent_obj;
} FDTGenericMMap;

/* The number of "things" in the tuple. Not to be confused with the cell length
 * of the tuple (which is variable based on content
 */

#define FDT_GENERIC_REG_TUPLE_LENGTH 4

typedef struct FDTGenericRegPropInfo {
    int n;
    union {
        struct {
            uint64_t *a;
            uint64_t *s;
            uint64_t *b;
            uint64_t *p;
        };
        uint64_t *x[FDT_GENERIC_REG_TUPLE_LENGTH];
    };
    Object **parents;
} FDTGenericRegPropInfo;

typedef struct FDTGenericMMapClass {
    /*< private >*/
    InterfaceClass parent_class;

    /*< public >*/
    bool (*parse_reg)(FDTGenericMMap *obj, FDTGenericRegPropInfo info,
                      Error **errp);
} FDTGenericMMapClass;

#define TYPE_FDT_GENERIC_GPIO "fdt-generic-gpio"

#define FDT_GENERIC_GPIO_CLASS(klass) \
     OBJECT_CLASS_CHECK(FDTGenericGPIOClass, (klass), TYPE_FDT_GENERIC_GPIO)
#define FDT_GENERIC_GPIO_GET_CLASS(obj) \
    OBJECT_GET_CLASS(FDTGenericGPIOClass, (obj), TYPE_FDT_GENERIC_GPIO)
#define FDT_GENERIC_GPIO(obj) \
     INTERFACE_CHECK(FDTGenericGPIO, (obj), TYPE_FDT_GENERIC_GPIO)

typedef struct FDTGenericGPIO {
    /*< private >*/
    Object parent_obj;
} FDTGenericGPIO;

typedef struct FDTGenericGPIOConnection {
    const char *name;
    uint16_t fdt_index;
    uint16_t range;
} FDTGenericGPIOConnection;

typedef struct FDTGenericGPIONameSet {
    const char *propname;
    const char *cells_propname;
    const char *names_propname;
} FDTGenericGPIONameSet;

typedef struct FDTGenericGPIOSet {
    const FDTGenericGPIONameSet *names;
    const FDTGenericGPIOConnection *gpios;
} FDTGenericGPIOSet;

static const FDTGenericGPIONameSet fdt_generic_gpio_name_set_power_gpio = {
    .propname = "power-gpios",
    .cells_propname = "#gpio-cells",
    .names_propname = "power-gpio-names",
};

static const FDTGenericGPIONameSet fdt_generic_gpio_name_set_reset_gpio = {
    .propname = "reset-gpios",
    .cells_propname = "#gpio-cells",
    .names_propname = "reset-gpio-names",
};

static const FDTGenericGPIONameSet fdt_generic_gpio_name_set_resets = {
    .propname = "resets",
    .cells_propname = "#reset-cells",
    .names_propname = "reset-names",
};

static const FDTGenericGPIONameSet fdt_generic_gpio_name_set_gpio = {
    .propname = "gpios",
    .cells_propname = "#gpio-cells",
    .names_propname = "gpio-names",
};

static const FDTGenericGPIONameSet fdt_generic_gpio_name_set_clock = {
    .propname = "clocks",
    .cells_propname = "#clock-cells",
    .names_propname = "clock-names",
};

static const FDTGenericGPIONameSet fdt_generic_gpio_name_set_interrupts = {
    .propname = "interrupts-extended",
    .cells_propname = "#interrupt-cells",
    .names_propname = "interrupt-names",
};

static const FDTGenericGPIOSet default_gpio_sets [] = {
    { .names = &fdt_generic_gpio_name_set_gpio },
    {
      .names = &fdt_generic_gpio_name_set_reset_gpio,
      .gpios = (FDTGenericGPIOConnection[]) {
        { .name = "rst_cntrl", .fdt_index = 0, .range = 6 },
      },
    },
    {
      .names = &fdt_generic_gpio_name_set_resets,
      .gpios = (FDTGenericGPIOConnection[]) {
        { .name = "rst_cntrl", .fdt_index = 0, .range = 6 },
      },
    },
    {
      .names = &fdt_generic_gpio_name_set_power_gpio,
      .gpios = (FDTGenericGPIOConnection[]) {
        { .name = "pwr_cntrl", .fdt_index = 0, .range = 1 },
      },
    },
    { .names = &fdt_generic_gpio_name_set_clock },
    { .names = &fdt_generic_gpio_name_set_interrupts },
    { },
};

typedef struct FDTGenericGPIOClass {
    /*< private >*/
    InterfaceClass parent_class;

    /*< public >*/
    /**
     * Unfortunately, FDT GPIOs aren't named. This allows a device to define
     * a mapping between a QEMU named GPIO and the FDT GPIO lists. Client GPIOs
     * name the GPIOs in the fdt 'gpios' property. E.G. An entry in this list
     * with .name = "foo' and fdt_index = 0 would associated the first element
     * in the gpios list with named gpio 'foo' on the device.
     *
     * controller_gpios is the same but for for gpio controllers. E.g. with the
     * example above, gpio "foo" would bt the first gpio defined for the device.
     */

    const FDTGenericGPIOSet *controller_gpios;
    const FDTGenericGPIOSet *client_gpios;
} FDTGenericGPIOClass;

#define TYPE_FDT_GENERIC_PROPS "fdt-generic-props"

#define FDT_GENERIC_PROPS_CLASS(klass) \
     OBJECT_CLASS_CHECK(FDTGenericPropsClass, (klass), \
                        TYPE_FDT_GENERIC_PROPS)
#define FDT_GENERIC_PROPS_GET_CLASS(obj) \
    OBJECT_GET_CLASS(FDTGenericPropsClass, (obj), \
                     TYPE_FDT_GENERIC_PROPS)

typedef struct FDTGenericPropsClass {
    /*< private >*/
    InterfaceClass parent_class;

    /*< public >*/
    void (*set_props)(Object *obj, Error **errp);
} FDTGenericPropsClass;

#endif /* FDT_GENERIC_UTIL_H */
