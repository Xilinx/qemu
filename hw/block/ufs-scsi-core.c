/*
 * UFS SCSI Device
 * Based on JESD220E
 *
 * SPDX-FileCopyrightText: 2023 AMD
 * SPDX-FileContributor: Author: Sai Pavan Boddu <sai.pavan.boddu@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "qemu/error-report.h"
#include "hw/block/ufs-scsi-if.h"
#include "scsi/constants.h"
#include "hw/block/ufs-scsi-core.h"

static void ufs_scsi_transfer_data(SCSIRequest *r, uint32_t len)
{
    UFSScsiCore *s = r->hba_private;
    UFSScsiTask *task = NULL;
    SCSIRequest *req = NULL;

    QTAILQ_FOREACH(task, &s->taskQ, link) {
        req = task->req;
        if (req->tag == r->tag) {
            break;
        }
    }
    if (task) {
        if (task->data_size) {
            /*
             * Read Data
             */
            task->buf_size += len;
            task->buf_off += ufs_scsi_if_handle_data(s->ufs_scsi_ini,
                                    scsi_req_get_buf(req) + task->buf_off,
                                    task->buf_size - task->buf_off, r->tag);
            scsi_req_continue(req);
        } else {
            /*
             * Write Data
             */
        }
     } else if (r->tag == 0xf000) {
        memcpy(s->rc10_resp, scsi_req_get_buf(r), 8);
     }
}

static void  ufs_scsi_command_complete(SCSIRequest *r, size_t resid)
{
    UFSScsiCore *s = r->hba_private;
    UFSScsiTask *task = NULL;
    SCSIRequest *req = NULL;
    uint8_t *sense = g_new0(uint8_t, 18);

    QTAILQ_FOREACH(task, &s->taskQ, link) {
        req = task->req;
        if (req->tag == r->tag) {
            break;
        }
    }
    if (task) {
        scsi_req_get_sense(req, sense, 18);
        ufs_scsi_if_handle_sense(s->ufs_scsi_ini, sense,
                                 18, req->tag);
        scsi_req_unref(req);
        QTAILQ_REMOVE(&s->taskQ, task, link);
    }
}

static void ufs_scsi_request_cancelled(SCSIRequest *r)
{
    scsi_req_unref(r);
}

static const struct SCSIBusInfo ufs_scsi_info = {
    .tcq = true,
    .max_target = 1,
    .max_lun = 255,

    .transfer_data = ufs_scsi_transfer_data,
    .complete = ufs_scsi_command_complete,
    .cancel = ufs_scsi_request_cancelled,
};

static void ufs_scsi_receive(ufs_scsi_if *ifs, void *pkt, uint32_t size,
                             uint8_t tag, uint8_t lun)
{
    UFSScsiCore *s = UFS_SCSI_CORE(ifs);
    SCSIRequest *req;
    SCSIDevice *sDev = scsi_device_find(&s->bus, 0, 0, lun);
    UFSScsiTask *task;
    uint32_t len;

    if (!sDev) {
        warn_report("ufs-scsi: lun %d scsi device not attached!", lun);
        return;
    }

    task = g_new0(UFSScsiTask, 1);
    req = scsi_req_new(sDev, tag, lun, pkt, size, s);
    task->req = req;
    task->buf_off = 0;
    task->buf_size = 0;
    QTAILQ_INSERT_TAIL(&s->taskQ, task, link);

    len = scsi_req_enqueue(req);
    if (len > 0) {
        /*
         * Data Read request
         */
        task->data_size = len;
        scsi_req_continue(req);
    }

}

static bool ufs_scsi_readCapacity10(ufs_scsi_if *ifs, uint8_t lun,
                                    uint8_t *rbuf)
{
    UFSScsiCore *s = UFS_SCSI_CORE(ifs);
    SCSIRequest *req;
    uint8_t cmd_rc10[10] = {READ_CAPACITY_10, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    SCSIDevice *sDev = scsi_device_find(&s->bus, 0, 0, lun);
    uint32_t len;

    if (!sDev) {
        return false;
    }
    req = scsi_req_new(sDev, 0xf000, lun, cmd_rc10, sizeof(cmd_rc10), s);

    len = scsi_req_enqueue(req);
    if (!len) {
        return false;
    }
    scsi_req_continue(req);
    memcpy(rbuf, s->rc10_resp, 8);
    return true;
}

static uint32_t ufs_scsi_handle_data(ufs_scsi_if *ifs, uint8_t *data,
                                    uint32_t size, uint8_t tag)
{
    /*
     * Implement Write
     */
    return 0;
}

static void ufs_scsi_realize(DeviceState *dev, Error **errp)
{
    UFSScsiCore *s = UFS_SCSI_CORE(dev);

    scsi_bus_init(&s->bus, sizeof(s->bus), dev,
                 &ufs_scsi_info);
}

static void ufs_scsi_init(Object *obj)
{
    UFSScsiCore *s = UFS_SCSI_CORE(obj);

    object_property_add_link(obj, "ufs-scsi-init", TYPE_UFS_SCSI_IF,
                             (Object **)&s->ufs_scsi_ini,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
    QTAILQ_INIT(&s->taskQ);
}

static void ufs_scsi_class_init(ObjectClass *klass, void *data)
{
   DeviceClass *dc = DEVICE_CLASS(klass);
   ufs_scsi_if_class *usc = UFS_SCSI_IF_CLASS(klass);

   dc->realize = ufs_scsi_realize;
   usc->handle_scsi = ufs_scsi_receive;
   usc->handle_data = ufs_scsi_handle_data;
   usc->read_capacity10 = ufs_scsi_readCapacity10;
   dc->user_creatable = false;
   set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
   dc->bus_type = "ufs-bus";
}

static const TypeInfo ufs_scsi_dev_info = {
    .name = TYPE_UFS_SCSI_CORE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(UFSScsiCore),
    .instance_init = ufs_scsi_init,
    .class_init = ufs_scsi_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_UFS_SCSI_IF },
        { },
    },
};

static void ufs_scsi_types(void)
{
    type_register_static(&ufs_scsi_dev_info);
}

type_init(ufs_scsi_types);
