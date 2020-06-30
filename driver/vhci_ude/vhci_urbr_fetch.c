#include "vhci_driver.h"
#include "vhci_urbr_fetch.tmh"

#include "vhci_urbr.h"
#include "usbd_helper.h"

extern NTSTATUS
fetch_urbr_status(PURB urb, struct usbip_header *hdr);
extern NTSTATUS
fetch_urbr_dscr(PURB urb, struct usbip_header *hdr);
extern NTSTATUS
fetch_urbr_control_transfer(PURB urb, struct usbip_header *hdr);
extern NTSTATUS
fetch_urbr_control_transfer_ex(PURB urb, struct usbip_header *hdr);
extern NTSTATUS
fetch_urbr_vendor_or_class(PURB urb, struct usbip_header *hdr);
extern NTSTATUS
fetch_urbr_bulk_or_interrupt(PURB urb, struct usbip_header *hdr);
extern NTSTATUS
fetch_urbr_iso(PURB urb, struct usbip_header *hdr);

NTSTATUS
copy_to_transfer_buffer(PVOID buf_dst, PMDL bufMDL, int dst_len, PVOID src, int src_len)
{
	PVOID	buf;

	if (dst_len < src_len) {
		TRE(WRITE, "too small buffer: dest: %d, src: %d\n", dst_len, src_len);
		return STATUS_INVALID_PARAMETER;
	}
	buf = get_buf(buf_dst, bufMDL);
	if (buf == NULL)
		return STATUS_INVALID_PARAMETER;

	RtlCopyMemory(buf, src, src_len);
	return STATUS_SUCCESS;
}

static NTSTATUS
fetch_urbr_urb(PURB urb, struct usbip_header *hdr)
{
	NTSTATUS	status;

	switch (urb->UrbHeader.Function) {
	case URB_FUNCTION_GET_STATUS_FROM_DEVICE:
	case URB_FUNCTION_GET_STATUS_FROM_INTERFACE:
	case URB_FUNCTION_GET_STATUS_FROM_ENDPOINT:
	case URB_FUNCTION_GET_STATUS_FROM_OTHER:
		status = fetch_urbr_status(urb, hdr);
		break;
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
		status = fetch_urbr_dscr(urb, hdr);
		break;
	case URB_FUNCTION_CONTROL_TRANSFER:
		status = fetch_urbr_control_transfer(urb, hdr);
		break;
	case URB_FUNCTION_CONTROL_TRANSFER_EX:
		status = fetch_urbr_control_transfer_ex(urb, hdr);
		break;
	case URB_FUNCTION_CLASS_DEVICE:
	case URB_FUNCTION_CLASS_INTERFACE:
	case URB_FUNCTION_CLASS_ENDPOINT:
	case URB_FUNCTION_CLASS_OTHER:
	case URB_FUNCTION_VENDOR_DEVICE:
	case URB_FUNCTION_VENDOR_INTERFACE:
	case URB_FUNCTION_VENDOR_ENDPOINT:
	case URB_FUNCTION_VENDOR_OTHER:
		status = fetch_urbr_vendor_or_class(urb, hdr);
		break;
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		status = fetch_urbr_bulk_or_interrupt(urb, hdr);
		break;
	case URB_FUNCTION_ISOCH_TRANSFER:
		status = fetch_urbr_iso(urb, hdr);
		break;
#if 0
	case URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL:
		status = STATUS_SUCCESS;
		break;
#endif
	default:
		TRW(WRITE, "not supported func: %!URBFUNC!", urb->UrbHeader.Function);
		status = STATUS_INVALID_PARAMETER;
		break;
	}

	if (status == STATUS_SUCCESS)
		urb->UrbHeader.Status = to_usbd_status(hdr->u.ret_submit.status);

	return status;
}

static NTSTATUS
fetch_urbr_error(purb_req_t urbr, struct usbip_header *hdr)
{
	if (urbr->req != NULL && urbr->urb != NULL) {
		PURB	urb = urbr->urb;

		urb->UrbHeader.Status = to_usbd_status(hdr->u.ret_submit.status);
		if (urb->UrbHeader.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER) {
			urb->UrbBulkOrInterruptTransfer.TransferBufferLength = hdr->u.ret_submit.actual_length;
		}
		TRW(WRITE, "%!URBFUNC!: wrong status: %s", urb->UrbHeader.Function, dbg_usbd_status(urb->UrbHeader.Status));
	}
	return STATUS_UNSUCCESSFUL;
}

NTSTATUS
fetch_urbr(purb_req_t urbr, struct usbip_header *hdr)
{
	if (hdr->u.ret_submit.status != 0)
		return fetch_urbr_error(urbr, hdr);

	if (urbr->urb == NULL)
		return STATUS_SUCCESS;

	return fetch_urbr_urb(urbr->urb, hdr);
}