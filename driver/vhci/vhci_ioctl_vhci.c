#include "vhci.h"

#include <usbdi.h>
#include <usbuser.h>

#include "usbip_vhci_api.h"
#include "vhci_pnp.h"

extern NTSTATUS vhci_plugin_vpdo(pvhci_dev_t vhci, ioctl_usbip_vhci_plugin *plugin, PFILE_OBJECT fo);
extern NTSTATUS vhub_get_ports_status(pvhub_dev_t vhub, ioctl_usbip_vhci_get_ports_status *st);
extern NTSTATUS vhci_eject_port(pvhci_dev_t vhci, ULONG port);
extern NTSTATUS vhci_ioctl_user_request(pvhci_dev_t vhci, PVOID buffer, ULONG inlen, PULONG poutlen);

static PAGEABLE NTSTATUS
get_controller_name(pvhci_dev_t vhci, PVOID buffer, ULONG inlen, PULONG poutlen)
{
	PUSB_HUB_NAME	hub_name = (PUSB_HUB_NAME)buffer;
	ULONG	hub_namelen, drvkey_buflen;
	LPWSTR	drvkey;

	UNREFERENCED_PARAMETER(inlen);

	drvkey = get_device_prop(vhci->common.devobj_lower, DevicePropertyDriverKeyName, &drvkey_buflen);
	if (drvkey == NULL)
		return STATUS_UNSUCCESSFUL;

	hub_namelen = (ULONG)(sizeof(USB_HUB_NAME) + drvkey_buflen - sizeof(WCHAR));
	if (*poutlen < sizeof(USB_HUB_NAME)) {
		*poutlen = (ULONG)hub_namelen;
		ExFreePoolWithTag(drvkey, USBIP_VHCI_POOL_TAG);
		return STATUS_BUFFER_TOO_SMALL;
	}

	hub_name->ActualLength = hub_namelen;
	RtlStringCchCopyW(hub_name->HubName, (*poutlen - sizeof(USB_HUB_NAME) + sizeof(WCHAR)) / sizeof(WCHAR), drvkey);
	ExFreePoolWithTag(drvkey, USBIP_VHCI_POOL_TAG);

	if (*poutlen > hub_namelen)
		*poutlen = (ULONG)hub_namelen;
	return STATUS_SUCCESS;
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
vhub_get_roothub_name(pvhub_dev_t vhub, PVOID buffer, ULONG inlen, PULONG poutlen)
{
	PUSB_ROOT_HUB_NAME	roothub_name = (PUSB_ROOT_HUB_NAME)buffer;
	SIZE_T	roothub_namelen, prefix_len;

	UNREFERENCED_PARAMETER(inlen);

	prefix_len = get_name_prefix_size(vhub->DevIntfRootHub.Buffer);
	if (prefix_len == 0) {
		DBGE(DBG_HPDO, "inavlid root hub format: %S\n", vhub->DevIntfRootHub.Buffer);
		return STATUS_INVALID_PARAMETER;
	}
	roothub_namelen = sizeof(USB_ROOT_HUB_NAME) + vhub->DevIntfRootHub.Length - prefix_len * sizeof(WCHAR);
	if (*poutlen < sizeof(USB_ROOT_HUB_NAME)) {
		*poutlen = (ULONG)roothub_namelen;
		return STATUS_BUFFER_TOO_SMALL;
	}
	roothub_name->ActualLength = (ULONG)roothub_namelen;
	RtlStringCchCopyW(roothub_name->RootHubName, (*poutlen - sizeof(USB_ROOT_HUB_NAME) + sizeof(WCHAR)) / sizeof(WCHAR),
		vhub->DevIntfRootHub.Buffer + prefix_len);
	if (*poutlen > roothub_namelen)
		*poutlen = (ULONG)roothub_namelen;
	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
vhci_ioctl_vhci(pvhci_dev_t vhci, PIO_STACK_LOCATION irpstack, ULONG ioctl_code, PVOID buffer, ULONG inlen, ULONG *poutlen)
{
	NTSTATUS	status = STATUS_INVALID_DEVICE_REQUEST;

	switch (ioctl_code) {
	case IOCTL_USBIP_VHCI_PLUGIN_HARDWARE:
		if (inlen == sizeof(ioctl_usbip_vhci_plugin))
			status = vhci_plugin_vpdo(vhci, (ioctl_usbip_vhci_plugin *)buffer, irpstack->FileObject);
		*poutlen = 0;
		break;
	case IOCTL_USBIP_VHCI_GET_PORTS_STATUS:
		if (*poutlen == sizeof(ioctl_usbip_vhci_get_ports_status))
			status = vhub_get_ports_status(VHUB_FROM_VHCI(vhci), (ioctl_usbip_vhci_get_ports_status*)buffer);
		break;
	case IOCTL_USBIP_VHCI_UNPLUG_HARDWARE:
		if (inlen == sizeof(ioctl_usbip_vhci_unplug))
			status = vhci_unplug_port(vhci, ((ioctl_usbip_vhci_unplug *)buffer)->addr);
		*poutlen = 0;
		break;
	case IOCTL_USBIP_VHCI_EJECT_HARDWARE:
		if (inlen == sizeof(USBIP_VHCI_EJECT_HARDWARE) && ((PUSBIP_VHCI_EJECT_HARDWARE)buffer)->Size == inlen)
			status = vhci_eject_port(vhci, ((PUSBIP_VHCI_EJECT_HARDWARE)buffer)->port);
		*poutlen = 0;
		break;
	case IOCTL_INTERNAL_USB_GET_CONTROLLER_NAME:
		inlen = (ULONG)(ULONG_PTR)irpstack->Parameters.Others.Argument2;
		status = get_controller_name(vhci, buffer, inlen, poutlen);
		break;
	case IOCTL_USB_GET_ROOT_HUB_NAME:
		status = vhub_get_roothub_name(DEVOBJ_TO_VHUB(vhci->common.child_pdo->fdo->Self), buffer, inlen, poutlen);
		break;
	case IOCTL_USB_USER_REQUEST:
		status = vhci_ioctl_user_request(vhci, buffer, inlen, poutlen);
		break;
	default:
		DBGE(DBG_IOCTL, "unhandled vhci ioctl: %s\n", dbg_vhci_ioctl_code(ioctl_code));
		break;
	}

	return status;
}