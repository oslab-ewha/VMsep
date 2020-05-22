#include "vhci.h"

#include <usbuser.h>

#include "usbreq.h"
#include "vhci_devconf.h"
#include "vhci_pnp.h"
#include "usbip_vhci_api.h"

extern NTSTATUS
vhci_plugin_vpdo(pvhci_dev_t vhci, ioctl_usbip_vhci_plugin *plugin, PFILE_OBJECT fo);

extern NTSTATUS
vhci_get_ports_status(ioctl_usbip_vhci_get_ports_status *st, pvhub_dev_t vhub);

extern NTSTATUS
vhci_get_controller_name(pvhci_dev_t vhci, PVOID buffer, ULONG inlen, PULONG poutlen);

extern NTSTATUS
hpdo_get_roothub_name(phpdo_dev_t hpdo, PVOID buffer, ULONG inlen, PULONG poutlen);

extern NTSTATUS
vpdo_get_nodeconn_info(pvpdo_dev_t vpdo, PUSB_NODE_CONNECTION_INFORMATION nodeconn, PULONG poutlen);

extern NTSTATUS
vpdo_get_nodeconn_info_ex(pvpdo_dev_t vpdo, PUSB_NODE_CONNECTION_INFORMATION_EX nodeconn, PULONG poutlen);

extern NTSTATUS
vpdo_get_nodeconn_info_ex_v2(pvpdo_dev_t vpdo, PUSB_NODE_CONNECTION_INFORMATION_EX_V2 nodeconn, PULONG poutlen);

extern NTSTATUS
vpdo_get_dsc_from_nodeconn(pvpdo_dev_t vpdo, PUSB_DESCRIPTOR_REQUEST dsc_req, PULONG poutlen);

extern NTSTATUS
vhci_ioctl_user_request(pvhci_dev_t vhci, PVOID buffer, ULONG inlen, PULONG poutlen);

extern NTSTATUS
vhub_get_information_ex(pvhub_dev_t vhub, PUSB_HUB_INFORMATION_EX pinfo);

extern NTSTATUS
vhub_get_capabilities_ex(pvhub_dev_t vhub, PUSB_HUB_CAPABILITIES_EX pinfo);

extern NTSTATUS
vhub_get_port_connector_properties(pvhub_dev_t vhub, PUSB_PORT_CONNECTOR_PROPERTIES pinfo, PULONG poutlen);

NTSTATUS
vhci_ioctl_abort_pipe(pvpdo_dev_t vpdo, USBD_PIPE_HANDLE hPipe)
{
	KIRQL		oldirql;
	PLIST_ENTRY	le;
	unsigned char	epaddr;

	if (!hPipe) {
		DBGI(DBG_IOCTL, "vhci_ioctl_abort_pipe: empty pipe handle\n");
		return STATUS_INVALID_PARAMETER;
	}

	epaddr = PIPE2ADDR(hPipe);

	DBGI(DBG_IOCTL, "vhci_ioctl_abort_pipe: EP: %02x\n", epaddr);

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	// remove all URBRs of the aborted pipe
	for (le = vpdo->head_urbr.Flink; le != &vpdo->head_urbr;) {
		struct urb_req	*urbr_local = CONTAINING_RECORD(le, struct urb_req, list_all);
		le = le->Flink;

		if (!is_port_urbr(urbr_local, epaddr))
			continue;

		DBGI(DBG_IOCTL, "aborted urbr removed: %s\n", dbg_urbr(urbr_local));

		if (urbr_local->irp) {
			PIRP	irp = urbr_local->irp;
			BOOLEAN	valid_irp;

			KIRQL oldirql_cancel;
			IoAcquireCancelSpinLock(&oldirql_cancel);
			valid_irp = IoSetCancelRoutine(irp, NULL) != NULL;
			IoReleaseCancelSpinLock(oldirql_cancel);

			if (valid_irp) {
				irp->IoStatus.Status = STATUS_CANCELLED;
				irp->IoStatus.Information = 0;
				IoCompleteRequest(irp, IO_NO_INCREMENT);
			}
		}
		RemoveEntryListInit(&urbr_local->list_state);
		RemoveEntryListInit(&urbr_local->list_all);
		free_urbr(urbr_local);
	}

	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

	return STATUS_SUCCESS;
}

static NTSTATUS
process_urb_get_frame(pvpdo_dev_t vpdo, PURB urb)
{
	struct _URB_GET_CURRENT_FRAME_NUMBER	*urb_get = &urb->UrbGetCurrentFrameNumber;
	UNREFERENCED_PARAMETER(vpdo);

	urb_get->FrameNumber = 0;
	return STATUS_SUCCESS;
}

static NTSTATUS
submit_urbr_irp(pvpdo_dev_t vpdo, PIRP irp)
{
	struct urb_req	*urbr;
	NTSTATUS	status;

	urbr = create_urbr(vpdo, irp, 0);
	if (urbr == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;
	status = submit_urbr(vpdo, urbr);
	if (NT_ERROR(status))
		free_urbr(urbr);
	return status;
}

static NTSTATUS
process_irp_urb_req(pvpdo_dev_t vpdo, PIRP irp, PURB urb)
{
	if (urb == NULL) {
		DBGE(DBG_IOCTL, "process_irp_urb_req: null urb\n");
		return STATUS_INVALID_PARAMETER;
	}

	DBGI(DBG_IOCTL, "process_irp_urb_req: function: %s\n", dbg_urbfunc(urb->UrbHeader.Function));

	switch (urb->UrbHeader.Function) {
	case URB_FUNCTION_ABORT_PIPE:
		return vhci_ioctl_abort_pipe(vpdo, urb->UrbPipeRequest.PipeHandle);
	case URB_FUNCTION_GET_CURRENT_FRAME_NUMBER:
		return process_urb_get_frame(vpdo, urb);
	case URB_FUNCTION_GET_STATUS_FROM_DEVICE:
	case URB_FUNCTION_GET_STATUS_FROM_INTERFACE:
	case URB_FUNCTION_GET_STATUS_FROM_ENDPOINT:
	case URB_FUNCTION_GET_STATUS_FROM_OTHER:
	case URB_FUNCTION_SELECT_CONFIGURATION:
	case URB_FUNCTION_ISOCH_TRANSFER:
	case URB_FUNCTION_CLASS_DEVICE:
	case URB_FUNCTION_CLASS_INTERFACE:
	case URB_FUNCTION_CLASS_ENDPOINT:
	case URB_FUNCTION_CLASS_OTHER:
	case URB_FUNCTION_VENDOR_DEVICE:
	case URB_FUNCTION_VENDOR_INTERFACE:
	case URB_FUNCTION_VENDOR_ENDPOINT:
	case URB_FUNCTION_VENDOR_OTHER:
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
	case URB_FUNCTION_SELECT_INTERFACE:
	case URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL:
	case URB_FUNCTION_CONTROL_TRANSFER:
	case URB_FUNCTION_CONTROL_TRANSFER_EX:
		return submit_urbr_irp(vpdo, irp);
	default:
		DBGW(DBG_IOCTL, "process_irp_urb_req: unhandled function: %s: len: %d\n",
			dbg_urbfunc(urb->UrbHeader.Function), urb->UrbHeader.Length);
		return STATUS_INVALID_PARAMETER;
	}
}

static NTSTATUS
setup_topology_address(pvpdo_dev_t vpdo, PIO_STACK_LOCATION irpStack)
{
	PUSB_TOPOLOGY_ADDRESS	topoaddr;

	topoaddr = (PUSB_TOPOLOGY_ADDRESS)irpStack->Parameters.Others.Argument1;
	topoaddr->RootHubPortNumber = (USHORT)vpdo->port;
	return STATUS_SUCCESS;
}

NTSTATUS
vhci_internal_ioctl(__in PDEVICE_OBJECT devobj, __in PIRP Irp)
{
	PIO_STACK_LOCATION      irpStack;
	NTSTATUS		status;
	pvpdo_dev_t	vpdo;
	ULONG			ioctl_code;

	DBGI(DBG_GENERAL | DBG_IOCTL, "vhci_internal_ioctl: Enter\n");

	irpStack = IoGetCurrentIrpStackLocation(Irp);
	ioctl_code = irpStack->Parameters.DeviceIoControl.IoControlCode;

	DBGI(DBG_IOCTL, "ioctl code: %s\n", dbg_vhci_ioctl_code(ioctl_code));

	if (!IS_DEVOBJ_VPDO(devobj)) {
		DBGW(DBG_IOCTL, "internal ioctl only for vpdo is allowed\n");
		Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	vpdo = (pvpdo_dev_t)devobj->DeviceExtension;

	if (!vpdo->plugged) {
		DBGW(DBG_IOCTL, "device is not connected\n");
		Irp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_DEVICE_NOT_CONNECTED;
	}

	switch (ioctl_code) {
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		status = process_irp_urb_req(vpdo, Irp, (PURB)irpStack->Parameters.Others.Argument1);
		break;
	case IOCTL_INTERNAL_USB_GET_PORT_STATUS:
		status = STATUS_SUCCESS;
		*(unsigned long *)irpStack->Parameters.Others.Argument1 = USBD_PORT_ENABLED | USBD_PORT_CONNECTED;
		break;
	case IOCTL_INTERNAL_USB_RESET_PORT:
		status = submit_urbr_irp(vpdo, Irp);
		break;
	case IOCTL_INTERNAL_USB_GET_TOPOLOGY_ADDRESS:
		status = setup_topology_address(vpdo, irpStack);
		break;
	default:
		DBGE(DBG_IOCTL, "unhandled internal ioctl: %s\n", dbg_vhci_ioctl_code(ioctl_code));
		status = STATUS_INVALID_PARAMETER;
		break;
	}

	if (status != STATUS_PENDING) {
		Irp->IoStatus.Information = 0;
		Irp->IoStatus.Status = status;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
	}

	DBGI(DBG_GENERAL | DBG_IOCTL, "vhci_internal_ioctl: Leave: %s\n", dbg_ntstatus(status));
	return status;
}

static PAGEABLE NTSTATUS
get_nodeconn_info(pvhub_dev_t vhub, PVOID buffer, ULONG inlen, PULONG poutlen)
{
	PUSB_NODE_CONNECTION_INFORMATION	conninfo = (PUSB_NODE_CONNECTION_INFORMATION)buffer;
	pvpdo_dev_t	vpdo;
	NTSTATUS	status;

	if (inlen < sizeof(USB_NODE_CONNECTION_INFORMATION)) {
		*poutlen = sizeof(USB_NODE_CONNECTION_INFORMATION);
		return STATUS_BUFFER_TOO_SMALL;
	}
	if (conninfo->ConnectionIndex > vhub->n_max_ports)
		return STATUS_NO_SUCH_DEVICE;
	vpdo = vhub_find_vpdo(vhub, conninfo->ConnectionIndex);
	status = vpdo_get_nodeconn_info(vpdo, conninfo, poutlen);
	if (vpdo != NULL)
		vdev_del_ref((pvdev_t)vpdo);
	return status;
}

static PAGEABLE NTSTATUS
get_nodeconn_info_ex(pvhub_dev_t vhub, PVOID buffer, ULONG inlen, PULONG poutlen)
{
	PUSB_NODE_CONNECTION_INFORMATION_EX	conninfo = (PUSB_NODE_CONNECTION_INFORMATION_EX)buffer;
	pvpdo_dev_t	vpdo;
	NTSTATUS	status;

	if (inlen < sizeof(PUSB_NODE_CONNECTION_INFORMATION_EX)) {
		*poutlen = sizeof(PUSB_NODE_CONNECTION_INFORMATION_EX);
		return STATUS_BUFFER_TOO_SMALL;
	}
	if (conninfo->ConnectionIndex > vhub->n_max_ports)
		return STATUS_NO_SUCH_DEVICE;
	vpdo = vhub_find_vpdo(vhub, conninfo->ConnectionIndex);
	status = vpdo_get_nodeconn_info_ex(vpdo, conninfo, poutlen);
	if (vpdo != NULL)
		vdev_del_ref((pvdev_t)vpdo);
	return status;
}

static PAGEABLE NTSTATUS
get_nodeconn_info_ex_v2(pvhub_dev_t vhub, PVOID buffer, ULONG inlen, PULONG poutlen)
{
	PUSB_NODE_CONNECTION_INFORMATION_EX_V2	conninfo = (PUSB_NODE_CONNECTION_INFORMATION_EX_V2)buffer;
	pvpdo_dev_t	vpdo;
	NTSTATUS	status;

	if (inlen < sizeof(PUSB_NODE_CONNECTION_INFORMATION_EX_V2)) {
		*poutlen = sizeof(PUSB_NODE_CONNECTION_INFORMATION_EX_V2);
		return STATUS_BUFFER_TOO_SMALL;
	}
	if (conninfo->ConnectionIndex > vhub->n_max_ports)
		return STATUS_NO_SUCH_DEVICE;
	vpdo = vhub_find_vpdo(vhub, conninfo->ConnectionIndex);
	status = vpdo_get_nodeconn_info_ex_v2(vpdo, conninfo, poutlen);
	if (vpdo != NULL)
		vdev_del_ref((pvdev_t)vpdo);
	return status;
}

static PAGEABLE NTSTATUS
get_descriptor_from_nodeconn(pvhub_dev_t vhub, PVOID buffer, ULONG inlen, PULONG poutlen)
{
	PUSB_DESCRIPTOR_REQUEST	dsc_req = (PUSB_DESCRIPTOR_REQUEST)buffer;
	pvpdo_dev_t	vpdo;
	NTSTATUS	status;

	if (inlen < sizeof(USB_DESCRIPTOR_REQUEST)) {
		*poutlen = sizeof(USB_DESCRIPTOR_REQUEST);
		return STATUS_BUFFER_TOO_SMALL;
	}

	vpdo = vhub_find_vpdo(vhub, dsc_req->ConnectionIndex);
	if (vpdo == NULL)
		return STATUS_NO_SUCH_DEVICE;

	status = vpdo_get_dsc_from_nodeconn(vpdo, dsc_req, poutlen);
	vdev_del_ref((pvdev_t)vpdo);
	return status;
}

static PAGEABLE NTSTATUS
get_hub_information_ex(pvhub_dev_t vhub, PVOID buffer, PULONG poutlen)
{
	PUSB_HUB_INFORMATION_EX	pinfo = (PUSB_HUB_INFORMATION_EX)buffer;

	if (*poutlen < sizeof(USB_HUB_INFORMATION_EX))
		return STATUS_BUFFER_TOO_SMALL;
	return vhub_get_information_ex(vhub, pinfo);
}

static PAGEABLE NTSTATUS
get_hub_capabilities_ex(pvhub_dev_t vhub, PVOID buffer, PULONG poutlen)
{
	PUSB_HUB_CAPABILITIES_EX	pinfo = (PUSB_HUB_CAPABILITIES_EX)buffer;

	if (*poutlen < sizeof(USB_HUB_CAPABILITIES_EX))
		return STATUS_BUFFER_TOO_SMALL;
	return vhub_get_capabilities_ex(vhub, pinfo);
}

static PAGEABLE NTSTATUS
get_port_connector_properties(pvhub_dev_t vhub, PVOID buffer, ULONG inlen, PULONG poutlen)
{
	PUSB_PORT_CONNECTOR_PROPERTIES	pinfo = (PUSB_PORT_CONNECTOR_PROPERTIES)buffer;

	if (inlen < sizeof(USB_PORT_CONNECTOR_PROPERTIES))
		return STATUS_BUFFER_TOO_SMALL;
	return vhub_get_port_connector_properties(vhub, pinfo, poutlen);
}

static PAGEABLE NTSTATUS
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
			status = vhci_get_ports_status((ioctl_usbip_vhci_get_ports_status*)buffer, vhci->vhub);
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
		status = vhci_get_controller_name(vhci, buffer, inlen, poutlen);
		break;
	case IOCTL_USB_GET_ROOT_HUB_NAME:
		status = hpdo_get_roothub_name(vhci->hpdo, buffer, inlen, poutlen);
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

static PAGEABLE NTSTATUS
vhci_ioctl_vhub(pvhub_dev_t vhub, ULONG ioctl_code, PVOID buffer, ULONG inlen, ULONG *poutlen)
{
	NTSTATUS	status = STATUS_INVALID_DEVICE_REQUEST;

	switch (ioctl_code) {
	case IOCTL_USB_GET_ROOT_HUB_NAME:
		status = hpdo_get_roothub_name(vhub->vhci->hpdo, buffer, inlen, poutlen);
		break;
	case IOCTL_USB_GET_NODE_CONNECTION_INFORMATION:
		status = get_nodeconn_info(vhub, buffer, inlen, poutlen);
		break;
	case IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX:
		status = get_nodeconn_info_ex(vhub, buffer, inlen, poutlen);
		break;
	case IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2:
		status = get_nodeconn_info_ex_v2(vhub, buffer, inlen, poutlen);
		break;
	case IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION:
		status = get_descriptor_from_nodeconn(vhub, buffer, inlen, poutlen);
		break;
	case IOCTL_USB_GET_HUB_INFORMATION_EX:
		status = get_hub_information_ex(vhub, buffer, poutlen);
		break;
	case IOCTL_USB_GET_HUB_CAPABILITIES_EX:
		status = get_hub_capabilities_ex(vhub, buffer, poutlen);
		break;
	case IOCTL_USB_GET_PORT_CONNECTOR_PROPERTIES:
		status = get_port_connector_properties(vhub, buffer, inlen, poutlen);
		break;
	default:
		DBGE(DBG_IOCTL, "unhandled vhub ioctl: %s\n", dbg_vhci_ioctl_code(ioctl_code));
		break;
	}

	return status;
}

PAGEABLE NTSTATUS
vhci_ioctl(__in PDEVICE_OBJECT devobj, __in PIRP irp)
{
	pvdev_t	vdev = DEVOBJ_TO_VDEV(devobj);
	PIO_STACK_LOCATION	irpstack;
	ULONG		ioctl_code;
	PVOID		buffer;
	ULONG		inlen, outlen;
	NTSTATUS	status = STATUS_INVALID_DEVICE_REQUEST;

	PAGED_CODE();

	irpstack = IoGetCurrentIrpStackLocation(irp);
	ioctl_code = irpstack->Parameters.DeviceIoControl.IoControlCode;

	DBGI(DBG_GENERAL | DBG_IOCTL, "vhci_ioctl(%s): Enter: code:%s, irp:%p\n",
		dbg_vdev_type(DEVOBJ_VDEV_TYPE(devobj)), dbg_vhci_ioctl_code(ioctl_code), irp);

	// Check to see whether the bus is removed
	if (vdev->DevicePnPState == Deleted) {
		status = STATUS_NO_SUCH_DEVICE;
		goto END;
	}

	buffer = irp->AssociatedIrp.SystemBuffer;
	inlen = irpstack->Parameters.DeviceIoControl.InputBufferLength;
	outlen = irpstack->Parameters.DeviceIoControl.OutputBufferLength;

	switch (DEVOBJ_VDEV_TYPE(devobj)) {
	case VDEV_VHCI:
		status = vhci_ioctl_vhci(DEVOBJ_TO_VHCI(devobj), irpstack, ioctl_code, buffer, inlen, &outlen);
		break;
	case VDEV_VHUB:
		status = vhci_ioctl_vhub(DEVOBJ_TO_VHUB(devobj), ioctl_code, buffer, inlen, &outlen);
		break;
	default:
		DBGW(DBG_IOCTL, "ioctl for %s is not allowed\n", dbg_vdev_type(DEVOBJ_VDEV_TYPE(devobj)));
		outlen = 0;
		break;
	}

	irp->IoStatus.Information = outlen;
END:
	irp->IoStatus.Status = status;
	IoCompleteRequest(irp, IO_NO_INCREMENT);

	DBGI(DBG_GENERAL | DBG_IOCTL, "vhci_ioctl: Leave: irp:%p, status:%s\n", irp, dbg_ntstatus(status));

	return status;
}
