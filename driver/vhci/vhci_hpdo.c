#include "vhci.h"

#include "usbip_vhci_api.h"
#include "vhci_pnp.h"

PAGEABLE phpdo_dev_t
create_hpdo(pvhci_dev_t vhci)
{
	phpdo_dev_t	hpdo;
	PDEVICE_OBJECT		devobj;

	PAGED_CODE();

	DBGI(DBG_VHUB, "creating hpdo\n");

	if ((devobj = vdev_create(vhci, VDEV_HPDO)) == NULL)
		return NULL;

	hpdo = DEVOBJ_TO_HPDO(devobj);
	RtlZeroMemory(hpdo, sizeof(phpdo_dev_t));

	INITIALIZE_PNP_STATE(hpdo);

	hpdo->common.type = VDEV_HPDO;
	hpdo->common.Self = devobj;

	hpdo->vhci = vhci;

	// Set the initial powerstate of the vhub
	hpdo->common.DevicePowerState = PowerDeviceUnspecified;
	hpdo->common.SystemPowerState = PowerSystemWorking;

	devobj->Flags |= DO_POWER_PAGABLE | DO_BUFFERED_IO;

	// We are done with initializing, so let's indicate that and return.
	// This should be the final step in the AddDevice process.
	devobj->Flags &= ~DO_DEVICE_INITIALIZING;
	return hpdo;
}

PAGEABLE BOOLEAN
start_hpdo(phpdo_dev_t hpdo)
{
	NTSTATUS	status;

	PAGED_CODE();

	status = IoRegisterDeviceInterface(TO_DEVOBJ(hpdo), (LPGUID)&GUID_DEVINTERFACE_USB_HUB, NULL, &hpdo->DevIntfRootHub);
	if (NT_ERROR(status)) {
		DBGE(DBG_PNP, "failed to register USB root hub device interface: %s\n", dbg_ntstatus(status));
		return FALSE;
	}
	status = IoSetDeviceInterfaceState(&hpdo->DevIntfRootHub, TRUE);
	if (NT_ERROR(status)) {
		DBGE(DBG_PNP, "failed to activate USB root hub device interface: %s\n", dbg_ntstatus(status));
		return FALSE;
	}
	return TRUE;
}

PAGEABLE void
invalidate_hpdo(phpdo_dev_t hpdo)
{
	IoSetDeviceInterfaceState(&hpdo->DevIntfRootHub, FALSE);
	RtlFreeUnicodeString(&hpdo->DevIntfRootHub);

	DBGI(DBG_PNP, "invalidating hpdo device object: 0x%p\n", TO_DEVOBJ(hpdo));
	hpdo->common.DevicePnPState = RemovePending;
	IoDeleteDevice(TO_DEVOBJ(hpdo));
}

PAGEABLE void
remove_hpdo(phpdo_dev_t hpdo)
{
	PAGED_CODE();

	if (hpdo->common.DevicePnPState == RemovePending) {
		DBGI(DBG_PNP, "hpdo already deleted: 0x%p\n", TO_DEVOBJ(hpdo));
		hpdo->vhci->hpdo = NULL;
		return;
	}

	IoSetDeviceInterfaceState(&hpdo->DevIntfRootHub, FALSE);
	RtlFreeUnicodeString(&hpdo->DevIntfRootHub);

	DBGI(DBG_PNP, "Deleting hpdo device object: 0x%p\n", TO_DEVOBJ(hpdo));

	hpdo->vhci->hpdo = NULL;
	IoDeleteDevice(TO_DEVOBJ(hpdo));
}

/* IOCTL_USB_GET_ROOT_HUB_NAME requires a device interface symlink name with the prefix(\??\) stripped */
static PAGEABLE SIZE_T
get_name_prefix_size(PWCHAR name)
{
	SIZE_T	i;
	for (i = 1; name[i]; i++) {
		if (name[i] == L'\\') {
			return i + 1;
		}
	}
	return 0;
}

PAGEABLE NTSTATUS
hpdo_get_roothub_name(phpdo_dev_t hpdo, PVOID buffer, ULONG inlen, PULONG poutlen)
{
	PUSB_ROOT_HUB_NAME	roothub_name = (PUSB_ROOT_HUB_NAME)buffer;
	SIZE_T	roothub_namelen, prefix_len;

	UNREFERENCED_PARAMETER(inlen);

	prefix_len = get_name_prefix_size(hpdo->DevIntfRootHub.Buffer);
	if (prefix_len == 0) {
		DBGE(DBG_HPDO, "inavlid root hub format: %S\n", hpdo->DevIntfRootHub.Buffer);
		return STATUS_INVALID_PARAMETER;
	}
	roothub_namelen = sizeof(USB_ROOT_HUB_NAME) + hpdo->DevIntfRootHub.Length - prefix_len * sizeof(WCHAR);
	if (*poutlen < sizeof(USB_ROOT_HUB_NAME)) {
		*poutlen = (ULONG)roothub_namelen;
		return STATUS_BUFFER_TOO_SMALL;
	}
	roothub_name->ActualLength = (ULONG)roothub_namelen;
	RtlStringCchCopyW(roothub_name->RootHubName, (*poutlen - sizeof(USB_ROOT_HUB_NAME) + sizeof(WCHAR)) / sizeof(WCHAR), 
		hpdo->DevIntfRootHub.Buffer + prefix_len);
	if (*poutlen > roothub_namelen)
		*poutlen = (ULONG)roothub_namelen;
	return STATUS_SUCCESS;
}