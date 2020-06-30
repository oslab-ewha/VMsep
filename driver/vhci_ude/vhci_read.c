#include "vhci_driver.h"
#include "vhci_read.tmh"

#include "vhci_urbr.h"

extern NTSTATUS
store_urbr_partial(WDFREQUEST req_read, purb_req_t urbr);

static purb_req_t
find_pending_urbr(pctx_vusb_t vusb)
{
	purb_req_t	urbr;

	if (IsListEmpty(&vusb->head_urbr_pending))
		return NULL;

	urbr = CONTAINING_RECORD(vusb->head_urbr_pending.Flink, urb_req_t, list_state);
	urbr->seq_num = ++(vusb->seq_num);
	RemoveEntryListInit(&urbr->list_state);
	return urbr;
}

static VOID
req_read_cancelled(WDFREQUEST req_read)
{
	pctx_vusb_t	vusb;

	TRD(READ, "a pending read req cancelled");

	vusb = *TO_PVUSB(WdfRequestGetFileObject(req_read));
	WdfWaitLockAcquire(vusb->lock, NULL);
	if (vusb->pending_req_read == req_read) {
		vusb->pending_req_read = NULL;
	}
	WdfWaitLockRelease(vusb->lock);

	WdfRequestComplete(req_read, STATUS_CANCELLED);
}

static NTSTATUS
read_vusb(pctx_vusb_t vusb, WDFREQUEST req)
{
	purb_req_t	urbr;
	NTSTATUS status;

	TRD(READ, "Enter");

	WdfWaitLockAcquire(vusb->lock, NULL);

	if (vusb->pending_req_read) {
		WdfWaitLockRelease(vusb->lock);
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	if (vusb->urbr_sent_partial != NULL) {
		urbr = vusb->urbr_sent_partial;

		WdfWaitLockRelease(vusb->lock);

		status = store_urbr_partial(req, urbr);

		WdfWaitLockAcquire(vusb->lock, NULL);
		vusb->len_sent_partial = 0;
	}
	else {
		urbr = find_pending_urbr(vusb);
		if (urbr == NULL) {
			vusb->pending_req_read = req;

			WdfRequestMarkCancelable(req, req_read_cancelled);
			WdfWaitLockRelease(vusb->lock);

			return STATUS_PENDING;
		}
		vusb->urbr_sent_partial = urbr;
		WdfWaitLockRelease(vusb->lock);

		status = store_urbr(req, urbr);

		WdfWaitLockAcquire(vusb->lock, NULL);
	}

	if (status != STATUS_SUCCESS) {
		RemoveEntryListInit(&urbr->list_all);
		WdfWaitLockRelease(vusb->lock);

		complete_urbr(urbr, status);
	}
	else {
		if (vusb->len_sent_partial == 0) {
			InsertTailList(&vusb->head_urbr_sent, &urbr->list_state);
			vusb->urbr_sent_partial = NULL;
		}
		WdfWaitLockRelease(vusb->lock);
	}
	return status;
}

VOID
io_read(_In_ WDFQUEUE queue, _In_ WDFREQUEST req, _In_ size_t len)
{
	pctx_vusb_t	vusb;
	NTSTATUS	status;

	UNREFERENCED_PARAMETER(queue);

	TRD(READ, "Enter: len: %u", (ULONG)len);

	vusb = *TO_PVUSB(WdfRequestGetFileObject(req));
	if (vusb->invalid) {
		TRD(READ, "vusb disconnected: port: %u", vusb->port);
		status = STATUS_DEVICE_NOT_CONNECTED;
	}
	else
		status = read_vusb(vusb, req);

	if (status != STATUS_PENDING) {
		WdfRequestComplete(req, status);
	}

	TRD(READ, "Leave: %!STATUS!", status);
}