#include "vhci_driver.h"
#include "vhci_ioctl.tmh"

#include "usbip_vhci_api.h"

NTSTATUS
plugin_vusb(pctx_vhci_t vhci, WDFREQUEST req, pvhci_pluginfo_t pluginfo);
NTSTATUS
plugout_vusb(pctx_vhci_t vhci, ULONG port);

static VOID
get_ports_status(pctx_vhci_t vhci, ioctl_usbip_vhci_get_ports_status *ports_status)
{
	ULONG	i;

	TRD(IOCTL, "Enter\n");

	RtlZeroMemory(ports_status, sizeof(ioctl_usbip_vhci_get_ports_status));

	WdfWaitLockAcquire(vhci->lock, NULL);

	for (i = 0; i != vhci->n_max_ports; i++) {
		pctx_vusb_t	vusb = vhci->vusbs[i];
		if (vusb != NULL)
			ports_status->u.port_status[i] = 1;
	}

	WdfWaitLockRelease(vhci->lock);

	TRD(IOCTL, "Leave\n");
}

static NTSTATUS
ioctl_get_ports_status(WDFQUEUE queue, WDFREQUEST req)
{
	pctx_vhci_t	vhci;
	ioctl_usbip_vhci_get_ports_status	*ports_status;
	NTSTATUS	status;

	status = WdfRequestRetrieveOutputBuffer(req, sizeof(ioctl_usbip_vhci_get_ports_status), &ports_status, NULL);
	if (NT_ERROR(status))
		return status;

	vhci = *TO_PVHCI(queue);

	get_ports_status(vhci, ports_status);
	WdfRequestSetInformation(req, sizeof(ioctl_usbip_vhci_get_ports_status));

	return STATUS_SUCCESS;
}

static NTSTATUS
ioctl_plugin_vusb(WDFQUEUE queue, WDFREQUEST req, size_t inlen)
{
	pctx_vhci_t	vhci;
	pvhci_pluginfo_t	pluginfo;
	PUSHORT		pdscr_fullsize;
	size_t		len;
	NTSTATUS	status;

	if (inlen < sizeof(vhci_pluginfo_t)) {
		TRE(IOCTL, "too small input length: %lld < %lld", inlen, sizeof(vhci_pluginfo_t));
		return STATUS_INVALID_PARAMETER;
	}
	status = WdfRequestRetrieveInputBuffer(req, sizeof(vhci_pluginfo_t), &pluginfo, &len);
	if (NT_ERROR(status)) {
		TRE(IOCTL, "failed to get pluginfo buffer: %!STATUS!", status);
		return status;
	}
	pdscr_fullsize = (PUSHORT)pluginfo->dscr_conf + 1;
	if (len != sizeof(vhci_pluginfo_t) + *pdscr_fullsize - 9) {
		TRE(IOCTL, "invalid pluginfo format: %lld != %lld", len, sizeof(vhci_pluginfo_t) + *pdscr_fullsize - 9);
		return STATUS_INVALID_PARAMETER;
	}
	vhci = *TO_PVHCI(queue);
	if (pluginfo->port < 0 || (ULONG)pluginfo->port > vhci->n_max_ports)
		return STATUS_INVALID_PARAMETER;
	return plugin_vusb(vhci, req, pluginfo);
}

static NTSTATUS
ioctl_plugout_vusb(WDFQUEUE queue, WDFREQUEST req, size_t inlen)
{
	pvhci_unpluginfo_t	unpluginfo;
	pctx_vhci_t	vhci;
	ULONG		port;
	NTSTATUS	status;

	if (inlen != sizeof(ioctl_usbip_vhci_unplug)) {
		TRE(IOCTL, "invalid unplug input size: %lld < %lld", inlen, sizeof(ioctl_usbip_vhci_unplug));
		return STATUS_INVALID_PARAMETER;
	}

	status = WdfRequestRetrieveInputBuffer(req, sizeof(ioctl_usbip_vhci_unplug), &unpluginfo, NULL);
	if (NT_ERROR(status)) {
		TRE(IOCTL, "failed to get unplug buffer: %!STATUS!", status);
		return status;
	}

	port = (ULONG)unpluginfo->addr;
	vhci = *TO_PVHCI(queue);
	if (port != 0 && port >= vhci->n_max_ports)
		return STATUS_INVALID_PARAMETER;

	return plugout_vusb(vhci, port);
}

VOID
io_device_control(_In_ WDFQUEUE queue, _In_ WDFREQUEST req,
	_In_ size_t outlen, _In_ size_t inlen, _In_ ULONG ioctl_code)
{
	NTSTATUS	status = STATUS_INVALID_DEVICE_REQUEST;

	UNREFERENCED_PARAMETER(outlen);

	TRD(IOCTL, "Enter: %!IOCTL!", ioctl_code);

	switch (ioctl_code) {
	case IOCTL_USBIP_VHCI_GET_PORTS_STATUS:
		status = ioctl_get_ports_status(queue, req);
		break;
	case IOCTL_USBIP_VHCI_PLUGIN_HARDWARE:
		status = ioctl_plugin_vusb(queue, req, inlen);
		break;
	case IOCTL_USBIP_VHCI_UNPLUG_HARDWARE:
		status = ioctl_plugout_vusb(queue, req, inlen);
		break;
	default:
		if (UdecxWdfDeviceTryHandleUserIoctl((*TO_PVHCI(queue))->hdev, req)) {
			TRD(IOCTL, "Leave: handled by Udecx");
			return;
		}
		TRE(IOCTL, "unhandled IOCTL: %!IOCTL!", ioctl_code);
		break;
	}

	WdfRequestComplete(req, status);

	TRD(IOCTL, "Leave: %!STATUS!", status);
}
