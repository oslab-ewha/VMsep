#include "vhci_driver.h"

#include "usbip_proto.h"
#include "vhci_urbr.tmh"
#include "vhci_urbr.h"

PVOID
get_buf(PVOID buf, PMDL bufMDL)
{
	if (buf == NULL) {
		if (bufMDL != NULL)
			buf = MmGetSystemAddressForMdlSafe(bufMDL, LowPagePriority);
		if (buf == NULL) {
			TRE(READ, "No transfer buffer\n");
		}
	}
	return buf;
}

struct usbip_header *
get_hdr_from_req_read(WDFREQUEST req_read)
{
	struct usbip_header *hdr;
	NTSTATUS	status;

	status = WdfRequestRetrieveOutputBuffer(req_read, sizeof(struct usbip_header), &hdr, NULL);
	if (NT_ERROR(status)) {
		return NULL;
	}
	return hdr;
}

PVOID
get_data_from_req_read(WDFREQUEST req_read, ULONG length)
{
	PVOID	data;
	NTSTATUS	status;

	status = WdfRequestRetrieveOutputBuffer(req_read, length, &data, NULL);
	if (NT_ERROR(status)) {
		return NULL;
	}
	return data;
}

ULONG
get_read_payload_length(WDFREQUEST req_read)
{
	WDF_REQUEST_PARAMETERS	params;

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(req_read, &params);

	return (ULONG)(params.Parameters.Read.Length - sizeof(struct usbip_header));
}

void
build_setup_packet(usb_cspkt_t *csp, unsigned char direct_in, unsigned char type, unsigned char recip, unsigned char request)
{
	csp->bmRequestType.B = 0;
	csp->bmRequestType.Type = type;
	if (direct_in)
		csp->bmRequestType.Dir = BMREQUEST_DEVICE_TO_HOST;
	csp->bmRequestType.Recipient = recip;
	csp->bRequest = request;
}

purb_req_t
find_sent_urbr(pctx_vusb_t vusb, struct usbip_header *hdr)
{
	PLIST_ENTRY	le;

	WdfWaitLockAcquire(vusb->lock, NULL);
	for (le = vusb->head_urbr_sent.Flink; le != &vusb->head_urbr_sent; le = le->Flink) {
		purb_req_t	urbr;
		urbr = CONTAINING_RECORD(le, urb_req_t, list_state);
		if (urbr->seq_num == hdr->base.seqnum) {
			RemoveEntryListInit(&urbr->list_all);
			RemoveEntryListInit(&urbr->list_state);
			WdfWaitLockRelease(vusb->lock);
			return urbr;
		}
	}
	WdfWaitLockRelease(vusb->lock);

	return NULL;
}

static PURB
get_urb_from_req(WDFREQUEST req)
{
	WDF_REQUEST_PARAMETERS	params;

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(req, &params);
	if (params.Parameters.DeviceIoControl.IoControlCode == IOCTL_INTERNAL_USB_SUBMIT_URB)
		return (PURB)params.Parameters.Others.Arg1;
	return NULL;
}

purb_req_t
create_urbr(pctx_ep_t ep, WDFREQUEST req, ULONG seq_num_unlink)
{
	WDFMEMORY	hmem;
	purb_req_t	urbr;
	NTSTATUS	status;

	status = WdfMemoryCreateFromLookaside(ep->vusb->lookaside_urbr, &hmem);
	if (NT_ERROR(status)) {
		TRE(URBR, "failed to allocate memory for urbr: %!STATUS!", status);
		return NULL;
	}

	urbr = TO_URBR(hmem);
	RtlZeroMemory(urbr, sizeof(urb_req_t));
	urbr->hmem = hmem;
	urbr->ep = ep;
	urbr->req = req;
	if (req != NULL) {
		urbr->urb = get_urb_from_req(req);
		WdfRequestSetInformation(req, (ULONG_PTR)urbr);
	}

	urbr->seq_num_unlink = seq_num_unlink;
	InitializeListHead(&urbr->list_all);
	InitializeListHead(&urbr->list_state);

	return urbr;
}

static void
free_urbr(purb_req_t urbr)
{
	ASSERT(IsListEmpty(&urbr->list_all));
	ASSERT(IsListEmpty(&urbr->list_state));
	WdfObjectDelete(urbr->hmem);
}

static void
submit_urbr_unlink(pctx_ep_t ep, unsigned long seq_num_unlink)
{
	purb_req_t	urbr_unlink;

	urbr_unlink = create_urbr(ep, NULL, seq_num_unlink);
	if (urbr_unlink != NULL) {
		NTSTATUS	status = submit_urbr(urbr_unlink);
		if (NT_ERROR(status)) {
			TRD(URBR, "failed to submit unlink urb: %!URBR!", urbr_unlink);
			free_urbr(urbr_unlink);
		}
	}
}

static VOID
urbr_cancelled(_In_ WDFREQUEST req)
{
	purb_req_t	urbr = (purb_req_t)WdfRequestGetInformation(req);
	pctx_vusb_t	vusb = urbr->ep->vusb;

	WdfWaitLockAcquire(vusb->lock, NULL);
	RemoveEntryListInit(&urbr->list_state);
	RemoveEntryListInit(&urbr->list_all);
	if (vusb->urbr_sent_partial == urbr) {
		vusb->urbr_sent_partial = NULL;
		vusb->len_sent_partial = 0;
	}
	WdfWaitLockRelease(vusb->lock);

	submit_urbr_unlink(urbr->ep, urbr->seq_num);
	TRD(URBR, "cancelled urbr destroyed: %!URBR!", urbr);
	complete_urbr(urbr, STATUS_CANCELLED);
}

NTSTATUS
submit_urbr(purb_req_t urbr)
{
	pctx_vusb_t	vusb = urbr->ep->vusb;
	WDFREQUEST	req_read;
	NTSTATUS	status = STATUS_PENDING;

	WdfWaitLockAcquire(vusb->lock, NULL);

	if (vusb->urbr_sent_partial || vusb->pending_req_read == NULL) {
		if (urbr->urb != NULL) {
			WdfRequestMarkCancelable(urbr->req, urbr_cancelled);
		}
		InsertTailList(&vusb->head_urbr_pending, &urbr->list_state);
		InsertTailList(&vusb->head_urbr, &urbr->list_all);
		WdfWaitLockRelease(vusb->lock);

		TRD(URBR, "urb pending: %!URBR!", urbr);
		return STATUS_PENDING;
	}

	req_read = vusb->pending_req_read;
	vusb->urbr_sent_partial = urbr;

	urbr->seq_num = ++(vusb->seq_num);

	WdfWaitLockRelease(vusb->lock);

	status = store_urbr(req_read, urbr);

	WdfWaitLockAcquire(vusb->lock, NULL);

	if (status == STATUS_SUCCESS) {
		if (urbr->urb != NULL) {
			WdfRequestMarkCancelable(urbr->req, urbr_cancelled);
		}
		if (vusb->len_sent_partial == 0) {
			vusb->urbr_sent_partial = NULL;
			InsertTailList(&vusb->head_urbr_sent, &urbr->list_state);
		}

		InsertTailList(&vusb->head_urbr, &urbr->list_all);

		vusb->pending_req_read = NULL;
		WdfWaitLockRelease(vusb->lock);

		WdfRequestUnmarkCancelable(req_read);
		WdfRequestComplete(req_read, STATUS_SUCCESS);
		status = STATUS_PENDING;
	}
	else {
		vusb->urbr_sent_partial = NULL;
		WdfWaitLockRelease(vusb->lock);

		status = STATUS_INVALID_PARAMETER;
	}

	TRD(URBR, "urb requested: status:%s", dbg_ntstatus(status));
	return status;
}

NTSTATUS
submit_req_urb(pctx_ep_t ep, WDFREQUEST req)
{
	purb_req_t	urbr;

	urbr = create_urbr(ep, req, 0);
	if (urbr == NULL)
		return STATUS_UNSUCCESSFUL;
	return submit_urbr(urbr);
}

NTSTATUS
submit_req_select(pctx_ep_t ep, WDFREQUEST req, UCHAR is_select_conf, UCHAR conf_value, UCHAR intf_num, UCHAR alt_setting)
{
	purb_req_t	urbr;

	urbr = create_urbr(ep, req, 0);
	if (urbr == NULL)
		return STATUS_UNSUCCESSFUL;
	urbr->is_select_conf = is_select_conf;
	urbr->conf_value = conf_value;
	urbr->intf_num = intf_num;
	urbr->alt_setting = alt_setting;
	return submit_urbr(urbr);
}

void
complete_urbr(purb_req_t urbr, NTSTATUS status)
{
	WDFREQUEST	req;

	req = urbr->req;
	if (req != NULL) {
		if (urbr->urb == NULL)
			WdfRequestComplete(req, status);
		else {
			WdfRequestUnmarkCancelable(req);
			UdecxUrbCompleteWithNtStatus(req, status);
		}
	}
	free_urbr(urbr);
}