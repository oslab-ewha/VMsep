#include "vhci.h"

#include <wdmsec.h> // for IoCreateDeviceSecure

#include "vhci_pnp.h"
#include "vhci_dev.h"
#include "usbip_vhci_api.h"

extern BOOLEAN vhub_is_empty_port(pvhub_dev_t vhub, ULONG port);
extern void vhub_attach_vpdo(pvhub_dev_t vhub, pvpdo_dev_t vpdo);

extern void vhub_mark_unplugged_all_vpdos(pvhub_dev_t vhub);
extern void vhub_eject_all_vpdos(pvhub_dev_t vhub);

static PAGEABLE void
vhci_init_vpdo(pvpdo_dev_t vpdo)
{
	PAGED_CODE();

	DBGI(DBG_PNP, "vhci_init_vpdo: 0x%p\n", vpdo);

	vpdo->plugged = TRUE;

	vpdo->current_intf_num = 0;
	vpdo->current_intf_alt = 0;

	INITIALIZE_PNP_STATE(vpdo);

	// vpdo usually starts its life at D3
	vpdo->common.DevicePowerState = PowerDeviceD3;
	vpdo->common.SystemPowerState = PowerSystemWorking;

	InitializeListHead(&vpdo->head_urbr);
	InitializeListHead(&vpdo->head_urbr_pending);
	InitializeListHead(&vpdo->head_urbr_sent);
	KeInitializeSpinLock(&vpdo->lock_urbr);

	TO_DEVOBJ(vpdo)->Flags |= DO_POWER_PAGABLE|DO_DIRECT_IO;

	InitializeListHead(&vpdo->Link);

	vhub_attach_vpdo(VHUB_FROM_VPDO(vpdo), vpdo);

	// This should be the last step in initialization.
	TO_DEVOBJ(vpdo)->Flags &= ~DO_DEVICE_INITIALIZING;
}

PAGEABLE NTSTATUS
vhci_plugin_vpdo(pvhci_dev_t vhci, ioctl_usbip_vhci_plugin *plugin, PFILE_OBJECT fo)
{
	PDEVICE_OBJECT		devobj;
	pvpdo_dev_t	vpdo, devpdo_old;

	PAGED_CODE();

	DBGI(DBG_VPDO, "Plugin vpdo: port: %hhd, vendor:product: %04hx:%04hx\n", plugin->port, plugin->vendor, plugin->product);

	if (plugin->port <= 0)
		return STATUS_INVALID_PARAMETER;

	if (!vhub_is_empty_port(VHUB_FROM_VHCI(vhci), plugin->port))
		return STATUS_INVALID_PARAMETER;

	if ((devobj = vdev_create(TO_DEVOBJ(vhci)->DriverObject, VDEV_VPDO)) == NULL)
		return STATUS_UNSUCCESSFUL;

	vpdo = DEVOBJ_TO_VPDO(devobj);
	vpdo->common.parent = &VHUB_FROM_VHCI(vhci)->common;

	vpdo->vendor = plugin->vendor;
	vpdo->product = plugin->product;
	vpdo->revision = plugin->version;
	vpdo->usbclass = plugin->class;
	vpdo->subclass = plugin->subclass;
	vpdo->protocol = plugin->protocol;
	vpdo->inum = plugin->inum;
	if (plugin->winstid[0] != L'\0')
		vpdo->winstid = libdrv_strdupW(plugin->winstid);
	else
		vpdo->winstid = NULL;

	devpdo_old = (pvpdo_dev_t)InterlockedCompareExchangePointer(&fo->FsContext, vpdo, 0);
	if (devpdo_old) {
		DBGI(DBG_GENERAL, "you can't plugin again");
		IoDeleteDevice(devobj);
		return STATUS_INVALID_PARAMETER;
	}
	vpdo->port = plugin->port;
	vpdo->fo = fo;
	vpdo->devid = plugin->devid;
	vpdo->speed = plugin->speed;

	vhci_init_vpdo(vpdo);

	// Device Relation changes if a new vpdo is created. So let
	// the PNP system now about that. This forces it to send bunch of pnp
	// queries and cause the function driver to be loaded.
	IoInvalidateDeviceRelations(vhci->common.pdo, BusRelations);

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
vhci_unplug_port(pvhci_dev_t vhci, ULONG port)
{
	pvhub_dev_t	vhub = VHUB_FROM_VHCI(vhci);
	pvpdo_dev_t	vpdo;

	PAGED_CODE();

	if (vhub == NULL) {
		DBGI(DBG_PNP, "vhub has gone\n");
		return STATUS_NO_SUCH_DEVICE;
	}

	if (port == 0) {
		DBGI(DBG_PNP, "plugging out all the devices!\n");
		vhub_mark_unplugged_all_vpdos(vhub);
		return STATUS_SUCCESS;
	}

	DBGI(DBG_PNP, "plugging out device: port: %u\n", port);

	vpdo = vhub_find_vpdo(vhub, port);
	if (vpdo == NULL) {
		DBGI(DBG_PNP, "no matching vpdo: port: %u\n", port);
		return STATUS_NO_SUCH_DEVICE;
	}

	vhub_mark_unplugged_vpdo(vhub, vpdo);
	vdev_del_ref((pvdev_t)vpdo);

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
vhci_eject_port(pvhci_dev_t vhci, ULONG port)
{
	pvhub_dev_t	vhub = VHUB_FROM_VHCI(vhci);
	pvpdo_dev_t	vpdo;

	PAGED_CODE();

	if (vhub == NULL) {
		DBGI(DBG_PNP, "vhub has gone\n");
		return STATUS_NO_SUCH_DEVICE;
	}

	if (port == 0) {
		DBGI(DBG_PNP, "ejecting all the devices!\n");
		vhub_eject_all_vpdos(vhub);
		return STATUS_SUCCESS;
	}

	DBGI(DBG_PNP, "ejecting device: port: %u\n", port);

	vpdo = vhub_find_vpdo(vhub, port);
	if (vpdo == NULL) {
		DBGI(DBG_PNP, "no matching vpdo: port: %u\n", port);
		return STATUS_NO_SUCH_DEVICE;
	}

	IoRequestDeviceEject(vpdo->common.pdo);
	vdev_del_ref((pvdev_t)vpdo);

	return STATUS_SUCCESS;
}