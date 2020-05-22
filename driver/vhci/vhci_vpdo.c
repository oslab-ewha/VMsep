#include "vhci.h"

#include "vhci_dev.h"
#include "usbreq.h"

PAGEABLE NTSTATUS
destroy_vpdo(pvpdo_dev_t vpdo)
{
	PAGED_CODE();

	if (vpdo->winstid != NULL)
		ExFreePoolWithTag(vpdo->winstid, USBIP_VHCI_POOL_TAG);

	// VHCI does not queue any irps at this time so we have nothing to do.
	// Free any resources.

	//FIXME
	if (vpdo->fo) {
		vpdo->fo->FsContext = NULL;
		vpdo->fo = NULL;
	}
	DBGI(DBG_VPDO, "Deleting vpdo: port: %u, 0x%p\n", vpdo->port, vpdo);
	IoDeleteDevice(vpdo->common.Self);
	return STATUS_SUCCESS;
}

PAGEABLE void
complete_pending_read_irp(pvpdo_dev_t vpdo)
{
	KIRQL	oldirql;
	PIRP	irp;

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);
	irp = vpdo->pending_read_irp;
	vpdo->pending_read_irp = NULL;
	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

	if (irp != NULL) {
		// We got pending_read_irp before submit_urbr
		BOOLEAN valid_irp;
		IoAcquireCancelSpinLock(&oldirql);
		valid_irp = IoSetCancelRoutine(irp, NULL) != NULL;
		IoReleaseCancelSpinLock(oldirql);
		if (valid_irp) {
			irp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
			irp->IoStatus.Information = 0;
			IoCompleteRequest(irp, IO_NO_INCREMENT);
		}
	}
}

PAGEABLE void
complete_pending_irp(pvpdo_dev_t vpdo)
{
	KIRQL	oldirql;
	BOOLEAN	valid_irp;

	DBGI(DBG_VPDO, "finish pending irp\n");

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);
	while (!IsListEmpty(&vpdo->head_urbr)) {
		struct urb_req	*urbr;
		PIRP	irp;

		urbr = CONTAINING_RECORD(vpdo->head_urbr.Flink, struct urb_req, list_all);
		RemoveEntryListInit(&urbr->list_all);
		RemoveEntryListInit(&urbr->list_state);
		/* FIMXE event */
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

		irp = urbr->irp;
		free_urbr(urbr);
		if (irp != NULL) {
			// urbr irps have cancel routine
			IoAcquireCancelSpinLock(&oldirql);
			valid_irp = IoSetCancelRoutine(irp, NULL) != NULL;
			IoReleaseCancelSpinLock(oldirql);
			if (valid_irp) {
				irp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
				irp->IoStatus.Information = 0;
				IoCompleteRequest(irp, IO_NO_INCREMENT);
			}
		}

		KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);
	}

	vpdo->urbr_sent_partial = NULL; // sure?
	vpdo->len_sent_partial = 0;
	InitializeListHead(&vpdo->head_urbr_sent);
	InitializeListHead(&vpdo->head_urbr_pending);

	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
}

PAGEABLE NTSTATUS
vpdo_select_config(pvpdo_dev_t vpdo, struct _URB_SELECT_CONFIGURATION *urb_selc)
{
	PUSB_CONFIGURATION_DESCRIPTOR	dsc_conf = urb_selc->ConfigurationDescriptor;
	PUSB_CONFIGURATION_DESCRIPTOR	dsc_conf_new = NULL;
	NTSTATUS	status;

	if (dsc_conf == NULL) {
		DBGI(DBG_VPDO, "going to unconfigured state\n");
		if (vpdo->dsc_conf != NULL) {
			ExFreePoolWithTag(vpdo->dsc_conf, USBIP_VHCI_POOL_TAG);
			vpdo->dsc_conf = NULL;
		}
		return STATUS_SUCCESS;
	}

	if (vpdo->dsc_conf == NULL || vpdo->dsc_conf->wTotalLength != dsc_conf->wTotalLength) {
		dsc_conf_new = ExAllocatePoolWithTag(NonPagedPool, dsc_conf->wTotalLength, USBIP_VHCI_POOL_TAG);
		if (dsc_conf_new == NULL) {
			DBGE(DBG_WRITE, "failed to allocate configuration descriptor: out of memory\n");
			return STATUS_UNSUCCESSFUL;
		}
	}
	else {
		dsc_conf_new = NULL;
	}
	if (dsc_conf_new != NULL && vpdo->dsc_conf != NULL) {
		ExFreePoolWithTag(vpdo->dsc_conf, USBIP_VHCI_POOL_TAG);
		vpdo->dsc_conf = dsc_conf_new;
	}
	RtlCopyMemory(vpdo->dsc_conf, dsc_conf, dsc_conf->wTotalLength);

	status = setup_config(vpdo->dsc_conf, &urb_selc->Interface, (PUCHAR)urb_selc + urb_selc->Hdr.Length, vpdo->speed);
	if (NT_SUCCESS(status)) {
		/* assign meaningless value, handle value is not used */
		urb_selc->ConfigurationHandle = (USBD_CONFIGURATION_HANDLE)0x12345678;
	}

	return status;
}

PAGEABLE NTSTATUS
vpdo_select_interface(pvpdo_dev_t vpdo, PUSBD_INTERFACE_INFORMATION info_intf)
{
	NTSTATUS	status;

	if (vpdo->dsc_conf == NULL) {
		DBGW(DBG_WRITE, "failed to select interface: empty configuration descriptor\n");
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	status = setup_intf(info_intf, vpdo->dsc_conf, vpdo->speed);
	if (NT_SUCCESS(status)) {
		vpdo->current_intf_num = info_intf->InterfaceNumber;
		vpdo->current_intf_alt = info_intf->AlternateSetting;
	}
	return status;
}

static PAGEABLE void
copy_pipe_info(USB_PIPE_INFO *pinfos, PUSB_CONFIGURATION_DESCRIPTOR dsc_conf, PUSB_INTERFACE_DESCRIPTOR dsc_intf)
{
	PVOID	start;
	int	i;

	for (i = 0, start = dsc_intf; i < dsc_intf->bNumEndpoints; i++) {
		PUSB_ENDPOINT_DESCRIPTOR	dsc_ep;

		dsc_ep = dsc_next_ep(dsc_conf, start);
		RtlCopyMemory(&pinfos[i].EndpointDescriptor, dsc_ep, sizeof(USB_ENDPOINT_DESCRIPTOR));
		pinfos[i].ScheduleOffset = 0;///TODO
		start = dsc_ep;
	}
}

PAGEABLE NTSTATUS
vpdo_get_nodeconn_info(pvpdo_dev_t vpdo, PUSB_NODE_CONNECTION_INFORMATION conninfo, PULONG poutlen)
{
	PUSB_INTERFACE_DESCRIPTOR	dsc_intf = NULL;
	ULONG	outlen;
	NTSTATUS	status = STATUS_INVALID_PARAMETER;

	conninfo->DeviceAddress = (USHORT)conninfo->ConnectionIndex;
	conninfo->NumberOfOpenPipes = 0;
	conninfo->DeviceIsHub = FALSE;

	if (vpdo == NULL) {
		conninfo->ConnectionStatus = NoDeviceConnected;
		conninfo->LowSpeed = FALSE;
		outlen = sizeof(USB_NODE_CONNECTION_INFORMATION);
		status = STATUS_SUCCESS;
	}
	else {
		if (vpdo->dsc_dev == NULL)
			return STATUS_INVALID_PARAMETER;

		conninfo->ConnectionStatus = DeviceConnected;

		RtlCopyMemory(&conninfo->DeviceDescriptor, vpdo->dsc_dev, sizeof(USB_DEVICE_DESCRIPTOR));

		if (vpdo->dsc_conf != NULL)
			conninfo->CurrentConfigurationValue = vpdo->dsc_conf->bConfigurationValue;
		conninfo->LowSpeed = (vpdo->speed == USB_SPEED_LOW || vpdo->speed == USB_SPEED_FULL) ? TRUE : FALSE;

		dsc_intf = dsc_find_intf(vpdo->dsc_conf, vpdo->current_intf_num, vpdo->current_intf_alt);
		if (dsc_intf != NULL)
			conninfo->NumberOfOpenPipes = dsc_intf->bNumEndpoints;

		outlen = sizeof(USB_NODE_CONNECTION_INFORMATION) + sizeof(USB_PIPE_INFO) * conninfo->NumberOfOpenPipes;
		if (*poutlen < outlen) {
			status = STATUS_BUFFER_TOO_SMALL;
		}
		else {
			if (conninfo->NumberOfOpenPipes > 0)
				copy_pipe_info(conninfo->PipeList, vpdo->dsc_conf, dsc_intf);
			status = STATUS_SUCCESS;
		}
	}
	*poutlen = outlen;

	return status;
}

PAGEABLE NTSTATUS
vpdo_get_nodeconn_info_ex(pvpdo_dev_t vpdo, PUSB_NODE_CONNECTION_INFORMATION_EX conninfo, PULONG poutlen)
{
	PUSB_INTERFACE_DESCRIPTOR	dsc_intf = NULL;
	ULONG	outlen;
	NTSTATUS	status = STATUS_INVALID_PARAMETER;

	conninfo->DeviceAddress = (USHORT)conninfo->ConnectionIndex;
	conninfo->NumberOfOpenPipes = 0;
	conninfo->DeviceIsHub = FALSE;

	if (vpdo == NULL) {
		conninfo->ConnectionStatus = NoDeviceConnected;
		conninfo->Speed = UsbFullSpeed;
		outlen = sizeof(USB_NODE_CONNECTION_INFORMATION);
		status = STATUS_SUCCESS;
	}
	else {
		if (vpdo->dsc_dev == NULL)
			return STATUS_INVALID_PARAMETER;

		conninfo->ConnectionStatus = DeviceConnected;

		RtlCopyMemory(&conninfo->DeviceDescriptor, vpdo->dsc_dev, sizeof(USB_DEVICE_DESCRIPTOR));

		if (vpdo->dsc_conf != NULL)
			conninfo->CurrentConfigurationValue = vpdo->dsc_conf->bConfigurationValue;
		conninfo->Speed = vpdo->speed;

		dsc_intf = dsc_find_intf(vpdo->dsc_conf, vpdo->current_intf_num, vpdo->current_intf_alt);
		if (dsc_intf != NULL)
			conninfo->NumberOfOpenPipes = dsc_intf->bNumEndpoints;

		outlen = sizeof(USB_NODE_CONNECTION_INFORMATION) + sizeof(USB_PIPE_INFO) * conninfo->NumberOfOpenPipes;
		if (*poutlen < outlen) {
			status = STATUS_BUFFER_TOO_SMALL;
		}
		else {
			if (conninfo->NumberOfOpenPipes > 0)
				copy_pipe_info(conninfo->PipeList, vpdo->dsc_conf, dsc_intf);
			status = STATUS_SUCCESS;
		}
	}
	*poutlen = outlen;

	return status;
}

PAGEABLE NTSTATUS
vpdo_get_nodeconn_info_ex_v2(pvpdo_dev_t vpdo, PUSB_NODE_CONNECTION_INFORMATION_EX_V2 conninfo, PULONG poutlen)
{
	UNREFERENCED_PARAMETER(vpdo);

	conninfo->SupportedUsbProtocols.ul = 0xffffffff;
	if (conninfo->SupportedUsbProtocols.Usb300)
		conninfo->SupportedUsbProtocols.Usb300 = FALSE;
	conninfo->Flags.ul = 0xffffffff;
	conninfo->Flags.DeviceIsOperatingAtSuperSpeedOrHigher = FALSE;
	conninfo->Flags.DeviceIsSuperSpeedCapableOrHigher = FALSE;
	conninfo->Flags.DeviceIsOperatingAtSuperSpeedPlusOrHigher = FALSE;
	conninfo->Flags.DeviceIsSuperSpeedPlusCapableOrHigher = FALSE;

	*poutlen = sizeof(PUSB_NODE_CONNECTION_INFORMATION_EX_V2);

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
vpdo_get_dsc_from_nodeconn(pvpdo_dev_t vpdo, PUSB_DESCRIPTOR_REQUEST dsc_req, PULONG psize)
{
	usb_cspkt_t	*csp = (usb_cspkt_t *)&dsc_req->SetupPacket;
	PVOID		dsc_data = NULL;
	ULONG		dsc_len = 0;
	NTSTATUS	status = STATUS_INVALID_PARAMETER;

	switch (csp->wValue.HiByte) {
	case USB_DEVICE_DESCRIPTOR_TYPE:
		dsc_data = vpdo->dsc_dev;
		if (dsc_data != NULL)
			dsc_len = sizeof(USB_DEVICE_DESCRIPTOR);
		break;
	case USB_CONFIGURATION_DESCRIPTOR_TYPE:
		dsc_data = vpdo->dsc_conf;
		if (dsc_data != NULL)
			dsc_len = vpdo->dsc_conf->wTotalLength;
		break;
	default:
		DBGE(DBG_GENERAL, "unhandled descriptor type: %s\n", dbg_usb_descriptor_type(csp->wValue.HiByte));
		break;
	}

	if (dsc_data != NULL) {
		ULONG	outlen = sizeof(USB_DESCRIPTOR_REQUEST) + dsc_len;
		if (*psize < outlen)
			status = STATUS_BUFFER_TOO_SMALL;
		else {
			RtlCopyMemory(dsc_req->Data, dsc_data, dsc_len);
			status = STATUS_SUCCESS;
		}
		*psize = outlen;
	}

	return status;
}

/*
 * need to cache a descriptor?
 * Currently, device descriptor & full configuration descriptor are cached in vpdo.
 */
static BOOLEAN
need_caching_dsc(pvpdo_dev_t vpdo, struct _URB_CONTROL_DESCRIPTOR_REQUEST* urb_cdr, PUSB_COMMON_DESCRIPTOR dsc)
{
	switch (urb_cdr->DescriptorType) {
	case USB_DEVICE_DESCRIPTOR_TYPE:
		if (vpdo->dsc_dev != NULL)
			return FALSE;
		break;
	case USB_CONFIGURATION_DESCRIPTOR_TYPE:
		if (vpdo->dsc_conf == NULL) {
			PUSB_CONFIGURATION_DESCRIPTOR	dsc_conf = (PUSB_CONFIGURATION_DESCRIPTOR)dsc;
			if (dsc_conf->wTotalLength != urb_cdr->TransferBufferLength) {
				DBGI(DBG_WRITE, "ignore non-full configuration descriptor\n");
				return FALSE;
			}
		}
		else
			return FALSE;
		break;
	case USB_STRING_DESCRIPTOR_TYPE:
		/* string descrptor will be fetched on demand */
		return FALSE;
	default:
		return FALSE;
	}
	return TRUE;
}

void
try_to_cache_descriptor(pvpdo_dev_t vpdo, struct _URB_CONTROL_DESCRIPTOR_REQUEST* urb_cdr, PUSB_COMMON_DESCRIPTOR dsc)
{
	PUSB_COMMON_DESCRIPTOR	dsc_new;

	if (!need_caching_dsc(vpdo, urb_cdr, dsc))
		return;

	dsc_new = ExAllocatePoolWithTag(PagedPool, urb_cdr->TransferBufferLength, USBIP_VHCI_POOL_TAG);
	if (dsc_new == NULL) {
		DBGE(DBG_WRITE, "invalid format descriptor: too small(%u < %hhu)\n", urb_cdr->TransferBufferLength, dsc->bLength);
		return;
	}
	RtlCopyMemory(dsc_new, dsc, urb_cdr->TransferBufferLength);

	switch (urb_cdr->DescriptorType) {
	case USB_DEVICE_DESCRIPTOR_TYPE:
		vpdo->dsc_dev = (PUSB_DEVICE_DESCRIPTOR)dsc_new;
		break;
	case USB_CONFIGURATION_DESCRIPTOR_TYPE:
		vpdo->dsc_conf = (PUSB_CONFIGURATION_DESCRIPTOR)dsc_new;
		break;
	default:
		ExFreePoolWithTag(dsc_new, USBIP_VHCI_POOL_TAG);
		break;
	}
}
