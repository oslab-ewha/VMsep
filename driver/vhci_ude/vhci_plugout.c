#include "vhci_driver.h"
#include "vhci_plugout.tmh"

#include "usbip_vhci_api.h"

static VOID
abort_pending_req_read(pctx_vusb_t vusb)
{
	WDFREQUEST	req_read_pending;

	WdfWaitLockAcquire(vusb->lock, NULL);
	req_read_pending = vusb->pending_req_read;
	vusb->pending_req_read = NULL;
	WdfWaitLockRelease(vusb->lock);

	if (req_read_pending != NULL) {
		TRD(PLUGIN, "abort read request");
		WdfRequestUnmarkCancelable(req_read_pending);
		WdfRequestComplete(req_read_pending, STATUS_DEVICE_NOT_CONNECTED);
	}
}

static VOID
abort_pending_urbr(purb_req_t urbr)
{
	TRD(PLUGIN, "abort pending urbr: %!URBR!", urbr);
	complete_urbr(urbr, STATUS_DEVICE_NOT_CONNECTED);
}

static VOID
abort_all_pending_urbrs(pctx_vusb_t vusb)
{
	WdfWaitLockAcquire(vusb->lock, NULL);

	while (!IsListEmpty(&vusb->head_urbr)) {
		purb_req_t	urbr;

		urbr = CONTAINING_RECORD(vusb->head_urbr.Flink, urb_req_t, list_all);
		RemoveEntryListInit(&urbr->list_all);
		RemoveEntryListInit(&urbr->list_state);
		WdfWaitLockRelease(vusb->lock);

		abort_pending_urbr(urbr);

		WdfWaitLockAcquire(vusb->lock, NULL);
	}

	WdfWaitLockRelease(vusb->lock);
}

static NTSTATUS
vusb_plugout(pctx_vusb_t vusb)
{
	NTSTATUS	status;

	abort_pending_req_read(vusb);
	abort_all_pending_urbrs(vusb);

	status = UdecxUsbDevicePlugOutAndDelete(vusb->ude_usbdev);
	if (NT_ERROR(status)) {
		TRD(PLUGIN, "failed to plug out: %!STATUS!", status);
		return status;
	}
	vusb->invalid = TRUE;
	return STATUS_SUCCESS;
}

static NTSTATUS
plugout_all_vusbs(pctx_vhci_t vhci)
{
	ULONG	i;

	TRD(PLUGIN, "plugging out all the devices!");

	WdfWaitLockAcquire(vhci->lock, NULL);
	for (i = 0; i < vhci->n_max_ports; i++) {
		NTSTATUS	status;
		pctx_vusb_t	vusb = vhci->vusbs[i];
		if (vusb == NULL)
			continue;
		status = vusb_plugout(vusb);
		if (NT_ERROR(status)) {
			WdfWaitLockRelease(vhci->lock);
			return STATUS_UNSUCCESSFUL;
		}

	}

	WdfWaitLockRelease(vhci->lock);

	return STATUS_SUCCESS;
}

NTSTATUS
plugout_vusb(pctx_vhci_t vhci, ULONG port)
{
	pctx_vusb_t	vusb;
	NTSTATUS	status;

	if (port == 0)
		return plugout_all_vusbs(vhci);

	TRD(IOCTL, "plugging out device: port: %u", port);

	WdfWaitLockAcquire(vhci->lock, NULL);

	vusb = vhci->vusbs[port - 1];
	if (vusb == NULL) {
		TRD(PLUGIN, "no matching vusb: port: %u", port);
		WdfWaitLockRelease(vhci->lock);
		return STATUS_NO_SUCH_DEVICE;
	}

	status = vusb_plugout(vusb);
	if (NT_ERROR(status)) {
		WdfWaitLockRelease(vhci->lock);
		return STATUS_UNSUCCESSFUL;
	}
	vhci->vusbs[port - 1] = NULL;

	WdfWaitLockRelease(vhci->lock);

	TRD(IOCTL, "completed to plug out: port: %u", port);

	return STATUS_SUCCESS;
}
