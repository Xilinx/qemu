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

#define MAX_REMOTE 16
#define MULTI_BUF_SIZE 1024

typedef struct SSSBase SSSBase;
typedef struct SSSStream SSSStream;

struct SSSStream {
    DeviceState parent_obj;

    SSSBase *sss;
};

typedef struct SSSPendingTransaction SSSPendingTransaction;
/**
 * struct SSSPendingTransaction:
 *
 * This structure describes an unfinished transaction.  There should be one
 * per RX channel.
 *
 * @activated: There is a pending transaction on the associated RX channel.
 * @remaining: The amount of bytes which needs to be copied for the given TX
 *             channel.
 * @data:      The chunk of memory which needs to be transfered.
 * @data_len:  The size of the data.
 */
struct SSSPendingTransaction {
    bool active;
    size_t remaining[MAX_REMOTE];
    uint8_t data[MULTI_BUF_SIZE];
    size_t data_len;
};

struct SSSBase {
    SysBusDevice busdev;

    StreamSink **tx_devs;
    SSSStream *rx_devs;

    uint32_t (*get_sss_regfield)(SSSBase *, int);
    StreamCanPushNotifyFn *notifys;
    void **notify_opaques;

    const uint32_t *sss_population;
    const int *r_sss_shifts;
    const uint8_t *r_sss_encodings;
    const uint8_t (*sss_cfg_mapping)[MAX_REMOTE];
    uint8_t num_remotes;

    SSSPendingTransaction pending_transactions[MAX_REMOTE];
};

void sss_notify_all(SSSBase *s);

#endif
