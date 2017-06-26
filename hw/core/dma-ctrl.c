#include "qemu/osdep.h"
#include "hw/dma-ctrl.h"

void dma_ctrl_read(DmaCtrl *dma_ctrl, hwaddr addr, uint32_t len)
{
    DmaCtrlClass *dcc =  DMA_CTRL_GET_CLASS(dma_ctrl);
    dcc->read(dma_ctrl, addr, len);
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
