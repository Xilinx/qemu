#include "qemu/osdep.h"
#include "exec/hwaddr.h"
#include "hw/dma-ctrl.h"

void dma_ctrl_read_with_notify(DmaCtrl *dma_ctrl, hwaddr addr, uint32_t len,
                               DmaCtrlNotify *notify, bool start_dma)
{
    DmaCtrlClass *dcc =  DMA_CTRL_GET_CLASS(dma_ctrl);
    dcc->read(dma_ctrl, addr, len, notify, start_dma);
}

static const TypeInfo dma_ctrl_info = {
    .name          = TYPE_DMA_CTRL,
    .parent        = TYPE_INTERFACE,
    .class_size = sizeof(DmaCtrlClass),
};

static void dma_ctrl_register_types(void)
{
    type_register_static(&dma_ctrl_info);
}

type_init(dma_ctrl_register_types)
