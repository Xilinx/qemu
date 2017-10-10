#ifndef SSS__H
#define SSS__H

#include "hw/sysbus.h"
#include "hw/stream.h"

#define TYPE_SSS_BASE "sss-base"
#define TYPE_SSS_STREAM "sss-stream"

#define SSS_BASE(obj) \
     OBJECT_CHECK(SSSBase, (obj), TYPE_SSS_BASE)

#define SSS_STREAM(obj) \
     OBJECT_CHECK(SSSStream, (obj), TYPE_SSS_STREAM)

#define NOT_REMOTE(s) \
    (s->num_remotes)

typedef struct SSSBase SSSBase;
typedef struct SSSStream SSSStream;

struct SSSStream {
    DeviceState parent_obj;

    SSSBase *sss;
};

struct SSSBase {
    SysBusDevice busdev;

    StreamSlave **tx_devs;
    SSSStream *rx_devs;

    uint32_t (*get_sss_regfield)(SSSBase *, int);
    StreamCanPushNotifyFn *notifys;
    void **notify_opaques;

    const uint32_t *sss_population;
    const int *r_sss_shifts;
    const uint8_t *r_sss_encodings;
    uint8_t num_remotes;
};

void sss_notify_all(SSSBase *s);

#endif
