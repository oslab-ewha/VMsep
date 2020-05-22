#include "vhci.h"

#include <wdmsec.h> // for IoCreateDeviceSecure

#include "vhci_pnp.h"
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

extern PAGEABLE BOOLEAN vhub_is_empty_port(pvhub_dev_t vhub, ULONG port);
extern PAGEABLE void vhub_attach_vpdo(pvhub_dev_t vhub, pvpdo_dev_t vpdo);

static PAGEABLE void
vhci_init_vpdo(pvpdo_dev_t vpdo)
{
	PAGED_CODE();

	DBGI(DBG_PNP, "vhci_init_vpdo: 0x%p\n", vpdo);

	vpdo->plugged = TRUE;
	vpdo->ReportedMissing = FALSE; // not yet reported missing

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

	vhub_attach_vpdo(vpdo->vhub, vpdo);

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

	if (!vhub_is_empty_port(vhci->vhub, plugin->port))
		return STATUS_INVALID_PARAMETER;

	if ((devobj = vdev_create(vhci, VDEV_VPDO)) == NULL)
		return STATUS_UNSUCCESSFUL;

	vpdo = (pvpdo_dev_t)devobj->DeviceExtension;

	vpdo->vendor = plugin->vendor;
	vpdo->product = plugin->product;
	vpdo->revision = plugin->version;
	vpdo->usbclass = plugin->class;
	vpdo->subclass = plugin->subclass;
	vpdo->protocol = plugin->protocol;
	vpdo->inum = plugin->inum;
	if (plugin->winstid[0] != L'\0') {
		vpdo->winstid = ExAllocatePoolWithTag(PagedPool, (MAX_VHCI_INSTANCE_ID + 1) * sizeof(wchar_t), USBIP_VHCI_POOL_TAG);
		if (vpdo->winstid != NULL)
			RtlStringCchCopyW(vpdo->winstid, MAX_VHCI_INSTANCE_ID + 1, plugin->winstid);
	}
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

	vpdo->common.type = VDEV_VPDO;
	vpdo->common.Self = devobj;
	vpdo->vhub = vhci->vhub;

	vhci_init_vpdo(vpdo);

	///TODO///TODO//TODO
	// Device Relation changes if a new vpdo is created. So let
	// the PNP system now about that. This forces it to send bunch of pnp
	// queries and cause the function driver to be loaded.
	IoInvalidateDeviceRelations(vhci->UnderlyingPDO, BusRelations);

	return STATUS_SUCCESS;
}