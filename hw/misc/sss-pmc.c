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

#define TYPE_PMC_SSS "versal,pmc-sss"

#define PMC_SSS(obj) \
     OBJECT_CHECK(PMCSSS, (obj), TYPE_PMC_SSS)

REG32(CFG, 0x0)
#define R_MAX (R_CFG + 1)
#define R_PMC_SSS_FIELD_LENGTH 4

typedef enum {
    DMA0,
    DMA1,
    PTPI,
    AES,
    SHA,
    SBI,
    PZM,
    PMC_NUM_REMOTES
} PMCSSSRemote;

#define NO_REMOTE PMC_NUM_REMOTES

static const char *pmc_sss_remote_names[] = {
    [DMA0] = "dma0",
    [DMA1] = "dma1",
    [PTPI] = "ptpi",
    [AES] = "aes",
    [SHA] = "sha",
    [SBI] = "sbi",
    [PZM] = "pzm",
};

static const uint32_t pmc_sss_population[] = {
    [DMA0] = (1 << DMA0) | (1 << AES) | (1 << SBI) | (1 << PZM),
    [DMA1] = (1 << DMA1) | (1 << AES) | (1 << SBI) | (1 << PZM),
    [PTPI] = (1 << DMA0) | (1 << DMA1),
    [AES] = (1 << DMA0) | (1 << DMA1),
    [SHA] = (1 << DMA0) | (1 << DMA1),
    [SBI] = (1 << DMA0) | (1 << DMA1),
    [NO_REMOTE] = 0,
};

static const int r_pmc_cfg_sss_shifts[] = {
    [DMA0] = 0,
    [DMA1] = 4,
    [PTPI] = 8,
    [AES] = 12,
    [SHA] = 16,
    [SBI] = 20,
    [PZM] = -1,
};

static const uint8_t r_pmc_cfg_sss_encodings[] = {
    [DMA0] = DMA0,
    [DMA1] = DMA1,
    [PTPI] = PTPI,
    [AES] = AES,
    [SHA] = SHA,
    [SBI] = SBI,
    [PZM] = PZM,
};

/* Remote Encodings
                 DMA0  DMA1  PTPI  AES   SHA   SBI   PZM    NONE*/
#define DMA0_MAP {0xD,  0xFF, 0xFF, 0x6,  0xFF, 0xB,  0x3,   0xFF}
#define DMA1_MAP {0xFF, 0x9,  0xFF, 0x7,  0xFF, 0xE,  0x4,   0xFF}
#define PTPI_MAP {0xD,  0xA,  0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  0xFF}
#define AES_MAP  {0xE,  0x5,  0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  0xFF}
#define SHA_MAP  {0xC,  0x7,  0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  0xFF}
#define SBI_MAP  {0x5,  0xB,  0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  0xFF}

static const uint8_t pmc_sss_cfg_mapping[][PMC_NUM_REMOTES + 1] = {
    [DMA0] = DMA0_MAP,
    [DMA1] = DMA1_MAP,
    [PTPI] = PTPI_MAP,
    [AES]  = AES_MAP,
    [SHA]  = SHA_MAP,
    [SBI]  = SBI_MAP,
};

typedef struct PMCSSS PMCSSS;

struct PMCSSS {
    SSSBase parent;
    MemoryRegion iomem;

    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];
};

static uint32_t pmc_get_sss_regfield(SSSBase *p, int remote)
{
    PMCSSS *s = PMC_SSS(p);
    uint32_t reg;
    uint32_t indx;

    reg = extract32(s->regs[R_CFG], r_pmc_cfg_sss_shifts[remote],
                      R_PMC_SSS_FIELD_LENGTH);
    for (indx = 0; indx < PMC_NUM_REMOTES; indx++) {
        if (reg == pmc_sss_cfg_mapping[remote][indx]) {
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

    for (r = 0; r < NO_REMOTE; ++r) {
        SSSStream *ss = SSS_STREAM(&p->rx_devs[r]);

        object_property_add_link(OBJECT(ss), "sss", TYPE_PMC_SSS,
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

static void pmc_sss_init(Object *obj)
{
    SSSBase *p = SSS_BASE(obj);
    PMCSSS *s = PMC_SSS(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;
    char *name;
    int remote;

    p->sss_population = pmc_sss_population;
    p->r_sss_shifts = r_pmc_cfg_sss_shifts;
    p->r_sss_encodings = r_pmc_cfg_sss_encodings;
    p->num_remotes = PMC_NUM_REMOTES;
    p->notifys = g_new0(StreamCanPushNotifyFn, PMC_NUM_REMOTES);
    p->notify_opaques = g_new0(void *, PMC_NUM_REMOTES);
    p->get_sss_regfield = pmc_get_sss_regfield;

    p->rx_devs = (SSSStream *) g_new(SSSStream, PMC_NUM_REMOTES);
    p->tx_devs = (StreamSlave **) g_new0(StreamSlave *, PMC_NUM_REMOTES);

    for (remote = 0 ; remote != NO_REMOTE; remote++) {
        name = g_strdup_printf("stream-connected-%s",
                                     pmc_sss_remote_names[remote]);
        object_property_add_link(OBJECT(s), name, TYPE_STREAM_SLAVE,
                             (Object **)&p->tx_devs[remote],
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
        g_free(name);
        object_initialize(&p->rx_devs[remote], sizeof(SSSStream),
                          TYPE_SSS_STREAM);
        name = g_strdup_printf("stream-connected-%s-target",
                               pmc_sss_remote_names[remote]);
        object_property_add_child(OBJECT(s), name,
                                 (Object *)&p->rx_devs[remote]);
        g_free(name);
    }

    memory_region_init(&s->iomem, obj, "versal.pmc-stream-switch", R_MAX * 4);

    reg_array =
        register_init_block32(DEVICE(obj), pmc_sss_regs_info,
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
    .minimum_version_id_old = 1,
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

static const TypeInfo pmc_sss_info = {
    .name          = TYPE_PMC_SSS,
    .parent        = TYPE_SSS_BASE,
    .instance_size = sizeof(PMCSSS),
    .class_init    = pmc_sss_class_init,
    .instance_init = pmc_sss_init,
};

static void sss_register_types(void)
{
    type_register_static(&pmc_sss_info);
}

type_init(sss_register_types)
