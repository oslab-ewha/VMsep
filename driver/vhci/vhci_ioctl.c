#include "vhci.h"

#include "vhci_dev.h"

<<<<<<< HEAD
extern NTSTATUS
<<<<<<< HEAD
vhci_ioctl_vhci(pvhci_dev_t vhci, PIO_STACK_LOCATION irpstack, ULONG ioctl_code, PVOID buffer, ULONG inlen, ULONG *poutlen);
extern  NTSTATUS
vhci_ioctl_vhub(pvhub_dev_t vhub, PIRP irp, ULONG ioctl_code, PVOID buffer, ULONG inlen, ULONG *poutlen);

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
=======
submit_urbr(pusbip_vpdo_dev_t vpdo, PIRP irp);

=======
>>>>>>> 10d26c6... vhci, notify a usbip server of urb cancellation
extern PAGEABLE NTSTATUS
vhci_plugin_dev(ioctl_usbip_vhci_plugin *plugin, pusbip_vhub_dev_t vhub, PFILE_OBJECT fo);

extern PAGEABLE NTSTATUS
vhci_get_ports_status(ioctl_usbip_vhci_get_ports_status *st, pusbip_vhub_dev_t vhub, ULONG *info);

extern PAGEABLE NTSTATUS
vhci_eject_device(PUSBIP_VHCI_EJECT_HARDWARE Eject, pusbip_vhub_dev_t vhub);

static NTSTATUS
process_urb_reset_pipe(pusbip_vpdo_dev_t vpdo)
{
	UNREFERENCED_PARAMETER(vpdo);

	////TODO need to check
	DBGI(DBG_IOCTL, "reset_pipe:\n");
	return STATUS_SUCCESS;
}

static NTSTATUS
process_urb_abort_pipe(pusbip_vpdo_dev_t vpdo, PURB urb)
{
	UNREFERENCED_PARAMETER(vpdo);
	UNREFERENCED_PARAMETER(urb);

	////TODO need to check
	DBGI(DBG_IOCTL, "abort_pipe: %x\n", urb->UrbPipeRequest.PipeHandle);
	return STATUS_SUCCESS;
}

static NTSTATUS
process_urb_get_frame(pusbip_vpdo_dev_t vpdo, PURB urb)
{
	struct _URB_GET_CURRENT_FRAME_NUMBER	*urb_get = &urb->UrbGetCurrentFrameNumber;
	UNREFERENCED_PARAMETER(vpdo);

	urb_get->FrameNumber = 0;
	return STATUS_SUCCESS;
}

static NTSTATUS
submit_urbr_irp(pusbip_vpdo_dev_t vpdo, PIRP irp)
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
process_irp_urb_req(pusbip_vpdo_dev_t vpdo, PIRP irp, PURB urb)
{
	if (urb == NULL) {
		DBGE(DBG_IOCTL, "process_irp_urb_req: null urb\n");
		return STATUS_INVALID_PARAMETER;
	}

	DBGI(DBG_IOCTL, "process_irp_urb_req: function: %s\n", dbg_urbfunc(urb->UrbHeader.Function));

	switch (urb->UrbHeader.Function) {
	case URB_FUNCTION_RESET_PIPE:
		return process_urb_reset_pipe(vpdo);
	case URB_FUNCTION_ABORT_PIPE:
		return process_urb_abort_pipe(vpdo, urb);
	case URB_FUNCTION_GET_CURRENT_FRAME_NUMBER:
		return process_urb_get_frame(vpdo, urb);
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
		return submit_urbr_irp(vpdo, irp);
	default:
		DBGW(DBG_IOCTL, "process_irp_urb_req: unhandled function: %s: len: %d\n",
			dbg_urbfunc(urb->UrbHeader.Function), urb->UrbHeader.Length);
		return STATUS_INVALID_PARAMETER;
	}
}

static NTSTATUS
setup_topology_address(pusbip_vpdo_dev_t vpdo, PIO_STACK_LOCATION irpStack)
{
	PUSB_TOPOLOGY_ADDRESS	topoaddr;

	topoaddr = (PUSB_TOPOLOGY_ADDRESS)irpStack->Parameters.Others.Argument1;
	topoaddr->RootHubPortNumber = (USHORT)vpdo->port;
	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
vhci_internal_ioctl(__in PDEVICE_OBJECT devobj, __in PIRP Irp)
{
	PIO_STACK_LOCATION      irpStack;
	NTSTATUS		status;
	pusbip_vpdo_dev_t	vpdo;
	pdev_common_t		devcom;
	ULONG			ioctl_code;

	devcom = (pdev_common_t)devobj->DeviceExtension;

	DBGI(DBG_GENERAL | DBG_IOCTL, "vhci_internal_ioctl: Enter\n");

	irpStack = IoGetCurrentIrpStackLocation(Irp);
	ioctl_code = irpStack->Parameters.DeviceIoControl.IoControlCode;

	DBGI(DBG_IOCTL, "ioctl code: %s\n", dbg_vhci_ioctl_code(ioctl_code));

	if (devcom->is_vhub) {
		DBGW(DBG_IOCTL, "internal ioctl for vhub is not allowed\n");
		Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	vpdo = (pusbip_vpdo_dev_t)devobj->DeviceExtension;

	if (!vpdo->Present) {
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

PAGEABLE NTSTATUS
vhci_ioctl(__in PDEVICE_OBJECT devobj, __in PIRP Irp)
{
	PIO_STACK_LOCATION	irpStack;
	NTSTATUS			status;
	ULONG				inlen, outlen;
	ULONG				info = 0;
	pusbip_vhub_dev_t		vhub;
	pdev_common_t			devcom;
	PVOID				buffer;
	ULONG				ioctl_code;

	PAGED_CODE();

	devcom = (pdev_common_t)devobj->DeviceExtension;

	DBGI(DBG_GENERAL | DBG_IOCTL, "vhci_ioctl: Enter\n");

	// We only allow create/close requests for the vhub.
	if (!devcom->is_vhub) {
		DBGE(DBG_IOCTL, "ioctl for vhub is not allowed\n");

		Irp->IoStatus.Status = status = STATUS_INVALID_DEVICE_REQUEST;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	vhub = (pusbip_vhub_dev_t)devobj->DeviceExtension;
	irpStack = IoGetCurrentIrpStackLocation(Irp);
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo

	DBGI(DBG_GENERAL | DBG_IOCTL, "vhci_ioctl(%s): Enter: code:%s, irp:%p\n",
		dbg_vdev_type(DEVOBJ_VDEV_TYPE(devobj)), dbg_vhci_ioctl_code(ioctl_code), irp);

<<<<<<< HEAD
	// Check to see whether the bus is removed
	if (vdev->DevicePnPState == Deleted) {
=======
	inc_io_vhub(vhub);

	// Check to see whether the bus is removed
	if (vhub->common.DevicePnPState == Deleted) {
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
		status = STATUS_NO_SUCH_DEVICE;
		goto END;
	}

	buffer = irp->AssociatedIrp.SystemBuffer;
	inlen = irpstack->Parameters.DeviceIoControl.InputBufferLength;
	outlen = irpstack->Parameters.DeviceIoControl.OutputBufferLength;

<<<<<<< HEAD
	switch (DEVOBJ_VDEV_TYPE(devobj)) {
	case VDEV_VHCI:
		status = vhci_ioctl_vhci(DEVOBJ_TO_VHCI(devobj), irpstack, ioctl_code, buffer, inlen, &outlen);
		break;
	case VDEV_VHUB:
		status = vhci_ioctl_vhub(DEVOBJ_TO_VHUB(devobj), irp, ioctl_code, buffer, inlen, &outlen);
=======
	switch (ioctl_code) {
	case IOCTL_USBIP_VHCI_PLUGIN_HARDWARE:
		if (sizeof(ioctl_usbip_vhci_plugin) == inlen) {
			status = vhci_plugin_dev((ioctl_usbip_vhci_plugin *)buffer, vhub, irpStack->FileObject);
		}
		break;
	case IOCTL_USBIP_VHCI_GET_PORTS_STATUS:
		if (sizeof(ioctl_usbip_vhci_get_ports_status) == outlen) {
			status = vhci_get_ports_status((ioctl_usbip_vhci_get_ports_status *)buffer, vhub, &info);
		}
		break;
	case IOCTL_USBIP_VHCI_UNPLUG_HARDWARE:
		if (sizeof(ioctl_usbip_vhci_unplug) == inlen) {
			status = vhci_unplug_dev(((ioctl_usbip_vhci_unplug *)buffer)->addr, vhub);
		}
		break;
	case IOCTL_USBIP_VHCI_EJECT_HARDWARE:
		if (inlen == sizeof(USBIP_VHCI_EJECT_HARDWARE) && ((PUSBIP_VHCI_EJECT_HARDWARE)buffer)->Size == inlen) {
			status = vhci_eject_device((PUSBIP_VHCI_EJECT_HARDWARE)buffer, vhub);
		}
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
		break;
	default:
		DBGW(DBG_IOCTL, "ioctl for %s is not allowed\n", dbg_vdev_type(DEVOBJ_VDEV_TYPE(devobj)));
		outlen = 0;
		break;
	}

	irp->IoStatus.Information = outlen;
END:
<<<<<<< HEAD
	if (status != STATUS_PENDING) {
		irp->IoStatus.Status = status;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
	}

	DBGI(DBG_GENERAL | DBG_IOCTL, "vhci_ioctl: Leave: irp:%p, status:%s\n", irp, dbg_ntstatus(status));
=======
	Irp->IoStatus.Status = status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	dec_io_vhub(vhub);

	DBGI(DBG_GENERAL | DBG_IOCTL, "vhci_ioctl: Leave: %s\n", dbg_ntstatus(status));
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo

	return status;
}