#ifndef QEMU_HW_XEN_COMMON_H
#define QEMU_HW_XEN_COMMON_H 1

#include "config-host.h"

#include <stddef.h>
#include <inttypes.h>

#include <xenctrl.h>
#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 420
#  include <xs.h>
#else
#  include <xenstore.h>
#endif
#include <xen/io/xenbus.h>

#include "hw/hw.h"
#include "hw/xen/xen.h"
#include "qemu/queue.h"

/*
 * We don't support Xen prior to 3.3.0.
 */

/* Xen before 4.0 */
#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 400
static inline void *xc_map_foreign_bulk(int xc_handle, uint32_t dom, int prot,
                                        xen_pfn_t *arr, int *err,
                                        unsigned int num)
{
    return xc_map_foreign_batch(xc_handle, dom, prot, arr, num);
}
#endif


/* Xen before 4.1 */
#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 410

typedef int XenXC;
typedef int xenevtchn_handle;
typedef int xengnttab_handle;

#  define XC_INTERFACE_FMT "%i"
#  define XC_HANDLER_INITIAL_VALUE    -1

static inline xenevtchn_handle *xenevtchn_open(void *logger,
                                               unsigned int open_flags)
{
    xenevtchn_handle *h = malloc(sizeof(*h));
    if (!h) {
        return NULL;
    }
    *h = xc_evtchn_open();
    if (*h == -1) {
        free(h);
        h = NULL;
    }
    return h;
}
static inline int xenevtchn_close(xenevtchn_handle *h)
{
    int rc = xc_evtchn_close(*h);
    free(h);
    return rc;
}
#define xenevtchn_fd(h) xc_evtchn_fd(*h)
#define xenevtchn_pending(h) xc_evtchn_pending(*h)
#define xenevtchn_notify(h, p) xc_evtchn_notify(*h, p)
#define xenevtchn_bind_interdomain(h, d, p) xc_evtchn_bind_interdomain(*h, d, p)
#define xenevtchn_unmask(h, p) xc_evtchn_unmask(*h, p)
#define xenevtchn_unbind(h, p) xc_evtchn_unmask(*h, p)

static inline xengnttab_handle *xengnttab_open(void *logger,
                                               unsigned int open_flags)
{
    xengnttab_handle *h = malloc(sizeof(*h));
    if (!h) {
        return NULL;
    }
    *h = xc_gnttab_open();
    if (*h == -1) {
        free(h);
        h = NULL;
    }
    return h;
}
static inline int xengnttab_close(xengnttab_handle *h)
{
    int rc = xc_gnttab_close(*h);
    free(h);
    return rc;
}
#define xengnttab_set_max_grants(h, n) xc_gnttab_set_max_grants(*h, n)
#define xengnttab_map_grant_ref(h, d, r, p) xc_gnttab_map_grant_ref(*h, d, r, p)
#define xengnttab_map_grant_refs(h, c, d, r, p) \
    xc_gnttab_map_grant_refs(*h, c, d, r, p)
#define xengnttab_unmap(h, a, n) xc_gnttab_munmap(*h, a, n)

static inline XenXC xen_xc_interface_open(void *logger, void *dombuild_logger,
                                          unsigned int open_flags)
{
    return xc_interface_open();
}

static inline int xc_fd(int xen_xc)
{
    return xen_xc;
}


static inline int xc_domain_populate_physmap_exact
    (XenXC xc_handle, uint32_t domid, unsigned long nr_extents,
     unsigned int extent_order, unsigned int mem_flags, xen_pfn_t *extent_start)
{
    return xc_domain_memory_populate_physmap
        (xc_handle, domid, nr_extents, extent_order, mem_flags, extent_start);
}

static inline int xc_domain_add_to_physmap(int xc_handle, uint32_t domid,
                                           unsigned int space, unsigned long idx,
                                           xen_pfn_t gpfn)
{
    struct xen_add_to_physmap xatp = {
        .domid = domid,
        .space = space,
        .idx = idx,
        .gpfn = gpfn,
    };

    return xc_memory_op(xc_handle, XENMEM_add_to_physmap, &xatp);
}

static inline struct xs_handle *xs_open(unsigned long flags)
{
    return xs_daemon_open();
}

static inline void xs_close(struct xs_handle *xsh)
{
    if (xsh != NULL) {
        xs_daemon_close(xsh);
    }
}


/* Xen 4.1 */
#else

typedef xc_interface *XenXC;
typedef xc_evtchn xenevtchn_handle;
typedef xc_gnttab xengnttab_handle;

#  define XC_INTERFACE_FMT "%p"
#  define XC_HANDLER_INITIAL_VALUE    NULL

#define xenevtchn_open(l, f) xc_evtchn_open(l, f);
#define xenevtchn_close(h) xc_evtchn_close(h)
#define xenevtchn_fd(h) xc_evtchn_fd(h)
#define xenevtchn_pending(h) xc_evtchn_pending(h)
#define xenevtchn_notify(h, p) xc_evtchn_notify(h, p)
#define xenevtchn_bind_interdomain(h, d, p) xc_evtchn_bind_interdomain(h, d, p)
#define xenevtchn_unmask(h, p) xc_evtchn_unmask(h, p)
#define xenevtchn_unbind(h, p) xc_evtchn_unbind(h, p)

#define xengnttab_open(l, f) xc_gnttab_open(l, f)
#define xengnttab_close(h) xc_gnttab_close(h)
#define xengnttab_set_max_grants(h, n) xc_gnttab_set_max_grants(h, n)
#define xengnttab_map_grant_ref(h, d, r, p) xc_gnttab_map_grant_ref(h, d, r, p)
#define xengnttab_unmap(h, a, n) xc_gnttab_munmap(h, a, n)
#define xengnttab_map_grant_refs(h, c, d, r, p) \
    xc_gnttab_map_grant_refs(h, c, d, r, p)

static inline XenXC xen_xc_interface_open(void *logger, void *dombuild_logger,
                                          unsigned int open_flags)
{
    return xc_interface_open(logger, dombuild_logger, open_flags);
}

/* FIXME There is now way to have the xen fd */
static inline int xc_fd(xc_interface *xen_xc)
{
    return -1;
}
#endif

/* Xen before 4.2 */
#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 420
static inline int xen_xc_hvm_inject_msi(XenXC xen_xc, domid_t dom,
        uint64_t addr, uint32_t data)
{
    return -ENOSYS;
}
/* The followings are only to compile op_discard related code on older
 * Xen releases. */
#define BLKIF_OP_DISCARD 5
struct blkif_request_discard {
    uint64_t nr_sectors;
    uint64_t sector_number;
};
#else
static inline int xen_xc_hvm_inject_msi(XenXC xen_xc, domid_t dom,
        uint64_t addr, uint32_t data)
{
    return xc_hvm_inject_msi(xen_xc, dom, addr, data);
}
#endif

void destroy_hvm_domain(bool reboot);

/* shutdown/destroy current domain because of an error */
void xen_shutdown_fatal_error(const char *fmt, ...) GCC_FMT_ATTR(1, 2);

#ifdef HVM_PARAM_VMPORT_REGS_PFN
static inline int xen_get_vmport_regs_pfn(XenXC xc, domid_t dom,
                                          unsigned long *vmport_regs_pfn)
{
    return xc_get_hvm_param(xc, dom, HVM_PARAM_VMPORT_REGS_PFN,
                            vmport_regs_pfn);
}
#else
static inline int xen_get_vmport_regs_pfn(XenXC xc, domid_t dom,
                                          unsigned long *vmport_regs_pfn)
{
    return -ENOSYS;
}
#endif

#endif /* QEMU_HW_XEN_COMMON_H */
