#include "qemu/osdep.h"
#include "hw/misc/sss.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/register.h"
#include "hw/misc/sss.h"

#ifndef PMC_SSS_ERR_DEBUG
#define PMC_SSS_ERR_DEBUG 0
#endif

#include "sss-pmc.h"

#define TYPE_PMC_SSS_BASE "pmc-sss-base"

#define PMC_SSS(obj) \
     OBJECT_CHECK(PMCSSS, (obj), TYPE_PMC_SSS_BASE)

#define TYPE_PMC_SSS "versal,pmc-sss"


REG32(CFG, 0x0)
#define R_MAX (R_CFG + 1)
#define R_PMC_SSS_FIELD_LENGTH 4

typedef struct PMCSSS PMCSSS;

struct PMCSSS {
    SSSBase parent;
    MemoryRegion iomem;

    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];
};

typedef struct PMCSSSDev {
    PMCSSS parent;
} PMCSSSDev;

static uint32_t pmc_get_sss_regfield(SSSBase *p, int remote)
{
    PMCSSS *s = PMC_SSS(p);
    uint32_t reg;
    uint32_t indx;

    reg = extract32(s->regs[R_CFG], p->r_sss_shifts[remote],
                      R_PMC_SSS_FIELD_LENGTH);
    for (indx = 0; indx < PMC_NUM_REMOTES; indx++) {
        if (reg == p->sss_cfg_mapping[remote][indx]) {
            break;
        }
    }
    /* indx == PMC_NUM_REMOTES indicates invalid SSS channel
     * and its handled by sss-base device
     */

    return indx;
}

static void r_cfg_post_write(RegisterInfo *reg, uint64_t val)
{
    SSSBase *s = SSS_BASE(reg->opaque);

    sss_notify_all(s);
}

static const RegisterAccessInfo pmc_sss_regs_info[] = {
    {    .name = "R_CFG", .addr = A_CFG,
         .post_write = r_cfg_post_write
    }
};

static const MemoryRegionOps sss_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void pmc_sss_realize(DeviceState *dev, Error **errp)
{
    PMCSSS *s = PMC_SSS(dev);
    SSSBase *p = SSS_BASE(dev);
    Error *local_errp = NULL;
    int r;

    for (r = 0; r < p->num_remotes; ++r) {
        SSSStream *ss = SSS_STREAM(&p->rx_devs[r]);

        object_property_add_link(OBJECT(ss), "sss", TYPE_PMC_SSS_BASE,
                             (Object **)&ss->sss,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
        if (local_errp) {
            goto pmc_sss_realize_fail;
        }
        object_property_set_link(OBJECT(ss), "sss", OBJECT(s), &local_errp);
        if (local_errp) {
            goto pmc_sss_realize_fail;
        }
        object_property_set_bool(OBJECT(ss), "realized", true, &error_fatal);
    }
    return;

pmc_sss_realize_fail:
    if (!*errp) {
        *errp = local_errp;
    }

}

static void sss_reset(DeviceState *dev)
{
    PMCSSS *s = PMC_SSS(dev);
    SSSBase *p = SSS_BASE(dev);
    int i;

    for (i = 0; i < R_MAX; ++i) {
        register_reset(&s->regs_info[i]);
    }
    sss_notify_all(p);
}

static void sss_init(PMCSSS *s, const char **remote_names)
{
    SSSBase *p = SSS_BASE(s);
    char *name;
    int remote;

    for (remote = 0 ; remote != p->num_remotes; remote++) {
        name = g_strdup_printf("stream-connected-%s",
                                     remote_names[remote]);
        object_property_add_link(OBJECT(s), name, TYPE_STREAM_SINK,
                             (Object **)&p->tx_devs[remote],
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
        g_free(name);
        object_initialize(&p->rx_devs[remote], sizeof(SSSStream),
                          TYPE_SSS_STREAM);
        name = g_strdup_printf("stream-connected-%s-target",
                               remote_names[remote]);
        object_property_add_child(OBJECT(s), name,
                                 (Object *)&p->rx_devs[remote]);
        g_free(name);
    }

}

static void pmc_sss_init(Object *obj)
{
    SSSBase *p = SSS_BASE(obj);
    PMCSSS *s = PMC_SSS(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(s);
    RegisterInfoArray *reg_array;

    p->sss_population = pmc_sss_population;
    p->r_sss_shifts = r_pmc_cfg_sss_shifts;
    p->r_sss_encodings = r_pmc_cfg_sss_encodings;
    p->num_remotes = PMC_NUM_REMOTES;
    p->notifys = g_new0(StreamCanPushNotifyFn, PMC_NUM_REMOTES);
    p->notify_opaques = g_new0(void *, PMC_NUM_REMOTES);
    p->get_sss_regfield = pmc_get_sss_regfield;
    p->sss_cfg_mapping = pmc_sss_cfg_mapping;

    p->rx_devs = (SSSStream *) g_new(SSSStream, PMC_NUM_REMOTES);
    p->tx_devs = (StreamSink **) g_new0(StreamSink *, PMC_NUM_REMOTES);

    sss_init(s, pmc_sss_remote_names);

    memory_region_init(&s->iomem, OBJECT(s), "versal.pmc-stream-switch",
                       R_MAX * 4);

    reg_array =
        register_init_block32(DEVICE(s), pmc_sss_regs_info,
                              ARRAY_SIZE(pmc_sss_regs_info),
                              s->regs_info, s->regs,
                              &sss_ops,
                              PMC_SSS_ERR_DEBUG,
                              R_MAX * 4);

    memory_region_add_subregion(&s->iomem, 0x0, &reg_array->mem);
    sysbus_init_mmio(sbd, &s->iomem);

}

static const VMStateDescription vmstate_pmc_sss = {
    .name = "pmc_sss",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, PMCSSS, R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void pmc_sss_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = sss_reset;
    dc->realize = pmc_sss_realize;
    dc->vmsd = &vmstate_pmc_sss;
}

static const TypeInfo pmc_sss_base_info = {
    .name          = TYPE_PMC_SSS_BASE,
    .parent        = TYPE_SSS_BASE,
    .instance_size = sizeof(PMCSSS),
};

static const TypeInfo pmc_sss_info = {
    .name          = TYPE_PMC_SSS,
    .parent        = TYPE_PMC_SSS_BASE,
    .instance_size = sizeof(PMCSSSDev),
    .class_init    = pmc_sss_class_init,
    .instance_init = pmc_sss_init,
};

static void sss_register_types(void)
{
    type_register_static(&pmc_sss_base_info);
    type_register_static(&pmc_sss_info);
}

type_init(sss_register_types)
