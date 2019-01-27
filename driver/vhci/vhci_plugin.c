#include "vhci.h"

#include <wdmsec.h> // for IoCreateDeviceSecure

<<<<<<< HEAD
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
=======
#include "vhci_dev.h"
#include "usbip_vhci_api.h"

// This guid is used in IoCreateDeviceSecure call to create vpdos. The idea is to
// allow the administrators to control access to the child device, in case the
// device gets enumerated as a raw device - no function driver, by modifying the 
// registry. If a function driver is loaded for the device, the system will override
// the security descriptor specified in the call to IoCreateDeviceSecure with the 
// one specifyied for the setup class of the child device.
//
DEFINE_GUID(GUID_SD_USBIP_VHCI_VPDO,
	0x9d3039dd, 0xcca5, 0x4b4d, 0xb3, 0x3d, 0xe2, 0xdd, 0xc8, 0xa8, 0xc5, 0x2e);
// {9D3039DD-CCA5-4b4d-B33D-E2DDC8A8C52E}

extern PAGEABLE void
vhci_init_vpdo(pusbip_vpdo_dev_t vpdo);

PAGEABLE NTSTATUS
vhci_plugin_dev(ioctl_usbip_vhci_plugin *plugin, pusbip_vhub_dev_t vhub, PFILE_OBJECT fo)
{
	PDEVICE_OBJECT      devobj;
	pusbip_vpdo_dev_t    vpdo, devpdo_old;
	NTSTATUS            status;
	PLIST_ENTRY         entry;

	PAGED_CODE();

	DBGI(DBG_PNP, "Exposing vpdo: addr: %d, vendor:product: %04x:%04x\n", plugin->addr, plugin->vendor, plugin->product);
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo

	if (plugin->port <= 0)
		return STATUS_INVALID_PARAMETER;

<<<<<<< HEAD
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
=======
	ExAcquireFastMutex(&vhub->Mutex);

	for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		vpdo = CONTAINING_RECORD(entry, usbip_vpdo_dev_t, Link);
		if ((ULONG)plugin->addr == vpdo->SerialNo &&
			vpdo->common.DevicePnPState != SurpriseRemovePending) {
			ExReleaseFastMutex(&vhub->Mutex);
			return STATUS_INVALID_PARAMETER;
		}
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
	}
	vpdo->port = plugin->port;
	vpdo->fo = fo;
	vpdo->devid = plugin->devid;
	vpdo->speed = plugin->speed;

	vhci_init_vpdo(vpdo);

<<<<<<< HEAD
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
=======
	ExReleaseFastMutex(&vhub->Mutex);

	// Create the vpdo
	DBGI(DBG_PNP, "vhub->NextLowerDriver = 0x%p\n", vhub->NextLowerDriver);

	// vpdo must have a name. You should let the system auto generate a
	// name by specifying FILE_AUTOGENERATED_DEVICE_NAME in the
	// DeviceCharacteristics parameter. Let us create a secure deviceobject,
	// in case the child gets installed as a raw device (RawDeviceOK), to prevent
	// an unpriviledged user accessing our device. This function is avaliable
	// in a static WDMSEC.LIB and can be used in Win2k, XP, and Server 2003
	// Just make sure that  the GUID specified here is not a setup class GUID.
	// If you specify a setup class guid, you must make sure that class is
	// installed before enumerating the vpdo.

	status = IoCreateDeviceSecure(vhub->common.Self->DriverObject, sizeof(usbip_vpdo_dev_t), NULL,
		FILE_DEVICE_BUS_EXTENDER, FILE_AUTOGENERATED_DEVICE_NAME | FILE_DEVICE_SECURE_OPEN,
		FALSE, &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RWX_RES_RWX, // allow normal users to access the devices
		(LPCGUID)&GUID_SD_USBIP_VHCI_VPDO, &devobj);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	vpdo = (pusbip_vpdo_dev_t)devobj->DeviceExtension;

	vpdo->vendor = plugin->vendor;
	vpdo->product = plugin->product;
	vpdo->revision = plugin->version;
	vpdo->usbclass = plugin->class;
	vpdo->subclass = plugin->subclass;
	vpdo->protocol = plugin->protocol;
	vpdo->inum = plugin->inum;

	devpdo_old = (pusbip_vpdo_dev_t)InterlockedCompareExchangePointer(&(fo->FsContext), vpdo, 0);
	if (devpdo_old) {
		DBGI(DBG_GENERAL, "you can't plugin again");
		IoDeleteDevice(devobj);
		return STATUS_INVALID_PARAMETER;
	}
	vpdo->SerialNo = plugin->addr;
	vpdo->fo = fo;
	vpdo->devid = plugin->devid;
	vpdo->speed = plugin->speed;

	vpdo->common.is_vhub = FALSE;
	vpdo->common.Self = devobj;
	vpdo->vhub = vhub;

	vhci_init_vpdo(vpdo);

	// Device Relation changes if a new vpdo is created. So let
	// the PNP system now about that. This forces it to send bunch of pnp
	// queries and cause the function driver to be loaded.
	IoInvalidateDeviceRelations(vhub->UnderlyingPDO, BusRelations);
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo

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