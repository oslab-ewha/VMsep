#include "vhci_driver.h"
#include "vhci_urbr_store.tmh"

#include "vhci_urbr.h"

extern NTSTATUS
store_urbr_control_transfer_partial(WDFREQUEST req_read, purb_req_t urbr);
extern NTSTATUS
store_urbr_control_transfer_ex_partial(WDFREQUEST req_read, purb_req_t urbr);
extern NTSTATUS
store_urbr_vendor_class_partial(WDFREQUEST req_read, purb_req_t urbr);
extern NTSTATUS
store_urbr_bulk_partial(WDFREQUEST req_read, purb_req_t urbr);
extern NTSTATUS
store_urbr_iso_partial(WDFREQUEST req_read, purb_req_t urbr);

extern NTSTATUS
store_urbr_get_status(WDFREQUEST req_read, purb_req_t urbr);
extern NTSTATUS
store_urbr_dscr_dev(WDFREQUEST req_read, purb_req_t urbr);
extern NTSTATUS
store_urbr_dscr_intf(WDFREQUEST req_read, purb_req_t urbr);
extern NTSTATUS
store_urbr_control_transfer(WDFREQUEST req_read, purb_req_t urbr);
extern NTSTATUS
store_urbr_control_transfer_ex(WDFREQUEST req_read, purb_req_t urbr);
extern NTSTATUS
store_urbr_vendor_class(WDFREQUEST req_read, purb_req_t urbr);
extern NTSTATUS
store_urbr_bulk(WDFREQUEST req_read, purb_req_t urbr);
extern NTSTATUS
store_urbr_iso(WDFREQUEST req_read, purb_req_t urbr);

extern NTSTATUS
store_urbr_select_config(WDFREQUEST req_read, purb_req_t urbr);
extern NTSTATUS
store_urbr_select_interface(WDFREQUEST req_read, purb_req_t urbr);

NTSTATUS
store_urbr_partial(WDFREQUEST req_read, purb_req_t urbr)
{
	PURB	urb;
	USHORT		urbfunc;
	WDF_REQUEST_PARAMETERS	params;
	NTSTATUS	status;

	TRD(READ, "Enter: urbr: %!URBR!", urbr);

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(urbr->req, &params);

	urb = (PURB)params.Parameters.Others.Arg1;
	urbfunc = urb->UrbHeader.Function;

	switch (urbfunc) {
	case URB_FUNCTION_CONTROL_TRANSFER:
		status = store_urbr_control_transfer_partial(req_read, urbr);
		break;
	case URB_FUNCTION_CONTROL_TRANSFER_EX:
		status = store_urbr_control_transfer_ex_partial(req_read, urbr);
		break;
	case URB_FUNCTION_CLASS_DEVICE:
	case URB_FUNCTION_CLASS_INTERFACE:
	case URB_FUNCTION_CLASS_ENDPOINT:
	case URB_FUNCTION_CLASS_OTHER:
	case URB_FUNCTION_VENDOR_DEVICE:
	case URB_FUNCTION_VENDOR_INTERFACE:
	case URB_FUNCTION_VENDOR_ENDPOINT:
		status = store_urbr_vendor_class_partial(req_read, urbr);
		break;
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		status = store_urbr_bulk_partial(req_read, urbr);
		break;
	case URB_FUNCTION_ISOCH_TRANSFER:
		status = store_urbr_iso_partial(req_read, urbr);
		break;
	default:
		TRW(READ, "unexpected partial urbr: %!URBFUNC!", urbfunc);
		status = STATUS_INVALID_PARAMETER;
		break;
	}

	TRD(READ, "Leave: %!STATUS!", status);

	return status;
}

static NTSTATUS
store_urbr_urb(WDFREQUEST req_read, purb_req_t urbr)
{
	USHORT		urb_func;
	NTSTATUS	status;

	urb_func = urbr->urb->UrbHeader.Function;
	TRD(READ, "urbr: %s, func:%!URBFUNC!", dbg_urbr(urbr), urb_func);

	switch (urb_func) {
	case URB_FUNCTION_GET_STATUS_FROM_DEVICE:
	case URB_FUNCTION_GET_STATUS_FROM_INTERFACE:
	case URB_FUNCTION_GET_STATUS_FROM_ENDPOINT:
	case URB_FUNCTION_GET_STATUS_FROM_OTHER:
		status = store_urbr_get_status(req_read, urbr);
		break;
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
		status = store_urbr_dscr_dev(req_read, urbr);
		break;
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
		status = store_urbr_dscr_intf(req_read, urbr);
		break;
	case URB_FUNCTION_CONTROL_TRANSFER:
		status = store_urbr_control_transfer(req_read, urbr);
		break;
	case URB_FUNCTION_CONTROL_TRANSFER_EX:
		status = store_urbr_control_transfer_ex(req_read, urbr);
		break;
	case URB_FUNCTION_CLASS_DEVICE:
	case URB_FUNCTION_CLASS_INTERFACE:
	case URB_FUNCTION_CLASS_ENDPOINT:
	case URB_FUNCTION_CLASS_OTHER:
	case URB_FUNCTION_VENDOR_DEVICE:
	case URB_FUNCTION_VENDOR_INTERFACE:
	case URB_FUNCTION_VENDOR_ENDPOINT:
		status = store_urbr_vendor_class(req_read, urbr);
		break;
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		status = store_urbr_bulk(req_read, urbr);
		break;
	case URB_FUNCTION_ISOCH_TRANSFER:
		status = store_urbr_iso(req_read, urbr);
		break;
#if 0
	case URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL:
		status = store_urb_reset_pipe(irp, urb, urbr);
		break;
#endif
	default:
		WdfRequestSetInformation(req_read, 0);
		TRE(READ, "unhandled urb function: %!URBFUNC!", urb_func);
		status = STATUS_INVALID_PARAMETER;
		break;
	}

	return status;
}

static NTSTATUS
store_urbr_select(WDFREQUEST req_read, purb_req_t urbr)
{
	if (urbr->is_select_conf)
		return store_urbr_select_config(req_read, urbr);
	else
		return store_urbr_select_interface(req_read, urbr);
}

static NTSTATUS
store_cancelled_urbr(WDFREQUEST req_read, purb_req_t urbr)
{
	struct usbip_header	*hdr;

	TRD(READ, "Enter");

	hdr = get_hdr_from_req_read(req_read);
	if (hdr == NULL)
		return STATUS_INVALID_PARAMETER;

	set_cmd_unlink_usbip_header(hdr, urbr->seq_num, urbr->ep->vusb->devid, urbr->seq_num_unlink);

	WdfRequestSetInformation(req_read, sizeof(struct usbip_header));
	return STATUS_SUCCESS;
}

NTSTATUS
store_urbr(WDFREQUEST req_read, purb_req_t urbr)
{
	TRD(READ, "urbr: %s", dbg_urbr(urbr));

	if (urbr->req == NULL) {
		return store_cancelled_urbr(req_read, urbr);
	}
	if (urbr->urb == NULL) {
		return store_urbr_select(req_read, urbr);
	}
	return store_urbr_urb(req_read, urbr);
}