#ifndef DMA_CTRL_H
#define DMA_CTRL_H 1

#include "qemu-common.h"
#include "hw/hw.h"
#include "qom/object.h"

#define TYPE_DMA_CTRL "dma-ctrl"

#define DMA_CTRL_CLASS(klass) \
     OBJECT_CLASS_CHECK(DmaCtrlClass, (klass), TYPE_DMA_CTRL)
#define DMA_CTRL_GET_CLASS(obj) \
    OBJECT_GET_CLASS(DmaCtrlClass, (obj), TYPE_DMA_CTRL)
#define DMA_CTRL(obj) \
     INTERFACE_CHECK(DmaCtrl, (obj), TYPE_DMA_CTRL)

typedef void (*dmactrl_notify_fn)(void *opaque);

typedef struct DmaCtrlNotify {
    void *opaque;
    dmactrl_notify_fn cb;
} DmaCtrlNotify;

typedef struct DmaCtrl {
    Object Parent;
} DmaCtrl;

typedef struct DmaCtrlClass {
    InterfaceClass parent;

    void (*read)(DmaCtrl *dma_ctrl, hwaddr addr, uint32_t len,
                 DmaCtrlNotify *notify, bool start_dma);
} DmaCtrlClass;

void dma_ctrl_read_with_notify(DmaCtrl *dma_ctrl, hwaddr addr, uint32_t len,
                   DmaCtrlNotify *notify, bool start_dma);

#endif /* DMA_CTRL_H */
