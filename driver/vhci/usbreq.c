#include "vhci.h"

#include "usbip_proto.h"
#include "usbreq.h"

extern NTSTATUS
store_urbr(PIRP irp, struct urb_req *urbr);

#ifdef DBG

const char *
dbg_urbr(struct urb_req *urbr)
{
	static char	buf[128];

	if (urbr == NULL)
		return "[null]";
<<<<<<< HEAD
	libdrv_snprintf(buf, 128, "[seq:%u]", urbr->seq_num);
=======
	dbg_snprintf(buf, 128, "[seq:%u]", urbr->seq_num);
>>>>>>> 10d26c6... vhci, notify a usbip server of urb cancellation
	return buf;
}

#endif

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

struct urb_req *
<<<<<<< HEAD
find_sent_urbr(pvpdo_dev_t vpdo, struct usbip_header *hdr)
=======
find_sent_urbr(pusbip_vpdo_dev_t vpdo, struct usbip_header *hdr)
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
{
	KIRQL		oldirql;
	PLIST_ENTRY	le;

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);
	for (le = vpdo->head_urbr_sent.Flink; le != &vpdo->head_urbr_sent; le = le->Flink) {
		struct urb_req	*urbr;
		urbr = CONTAINING_RECORD(le, struct urb_req, list_state);
		if (urbr->seq_num == hdr->base.seqnum) {
<<<<<<< HEAD
			RemoveEntryListInit(&urbr->list_all);
			RemoveEntryListInit(&urbr->list_state);
=======
			RemoveEntryList(le);
			RemoveEntryList(&urbr->list_all);
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
			KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
			return urbr;
		}
	}
	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

	return NULL;
}

struct urb_req *
<<<<<<< HEAD
find_pending_urbr(pvpdo_dev_t vpdo)
=======
find_pending_urbr(pusbip_vpdo_dev_t vpdo)
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
{
	struct urb_req	*urbr;

	if (IsListEmpty(&vpdo->head_urbr_pending))
		return NULL;

	urbr = CONTAINING_RECORD(vpdo->head_urbr_pending.Flink, struct urb_req, list_state);
	urbr->seq_num = ++(vpdo->seq_num);
<<<<<<< HEAD
	RemoveEntryListInit(&urbr->list_state);
=======
	RemoveEntryList(&urbr->list_state);
	InitializeListHead(&urbr->list_state);
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
	return urbr;
}

<<<<<<< HEAD
static void
<<<<<<< HEAD
submit_urbr_unlink(pvpdo_dev_t vpdo, unsigned long seq_num_unlink)
=======
remove_cancelled_urbr(pusbip_vpdo_dev_t vpdo, PIRP irp)
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
{
	struct urb_req	*urbr_unlink;

	urbr_unlink = create_urbr(vpdo, NULL, seq_num_unlink);
	if (urbr_unlink != NULL) {
		NTSTATUS	status = submit_urbr(vpdo, urbr_unlink);
		if (NT_ERROR(status)) {
			DBGI(DBG_GENERAL, "failed to submit unlink urb: %s\n", dbg_urbr(urbr_unlink));
			free_urbr(urbr_unlink);
		}
	}
}

<<<<<<< HEAD
static void
remove_cancelled_urbr(pvpdo_dev_t vpdo, struct urb_req *urbr)
{
	KIRQL	oldirql;

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	RemoveEntryListInit(&urbr->list_state);
	RemoveEntryListInit(&urbr->list_all);
	if (vpdo->urbr_sent_partial == urbr) {
		vpdo->urbr_sent_partial = NULL;
		vpdo->len_sent_partial = 0;
	}

	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

	submit_urbr_unlink(vpdo, urbr->seq_num);
=======
	KeAcquireSpinLockAtDpcLevel(&vpdo->lock_urbr);

=======
static struct urb_req *
find_urbr_with_irp(pusbip_vpdo_dev_t vpdo, PIRP irp)
{
	PLIST_ENTRY	le;

>>>>>>> 10d26c6... vhci, notify a usbip server of urb cancellation
	for (le = vpdo->head_urbr.Flink; le != &vpdo->head_urbr; le = le->Flink) {
		struct urb_req	*urbr;

		urbr = CONTAINING_RECORD(le, struct urb_req, list_all);
		if (urbr->irp == irp)
			return urbr;
	}

	return NULL;
}

static void
submit_urbr_unlink(pusbip_vpdo_dev_t vpdo, unsigned long seq_num_unlink)
{
	struct urb_req	*urbr_unlink;

	urbr_unlink = create_urbr(vpdo, NULL, seq_num_unlink);
	if (urbr_unlink != NULL) {
		NTSTATUS	status = submit_urbr(vpdo, urbr_unlink);
		if (NT_ERROR(status)) {
			DBGI(DBG_GENERAL, "failed to submit unlink urb: %s\n", dbg_urbr(urbr_unlink));
			ExFreeToNPagedLookasideList(&g_lookaside, urbr_unlink);
		}
	}
}

static void
remove_cancelled_urbr(pusbip_vpdo_dev_t vpdo, PIRP irp)
{
	struct urb_req	*urbr;
	KIRQL	oldirql = irp->CancelIrql;

	KeAcquireSpinLockAtDpcLevel(&vpdo->lock_urbr);

	urbr = find_urbr_with_irp(vpdo, irp);
	if (urbr != NULL) {
		RemoveEntryList(&urbr->list_state);
		RemoveEntryList(&urbr->list_all);
		if (vpdo->urbr_sent_partial == urbr) {
			vpdo->urbr_sent_partial = NULL;
			vpdo->len_sent_partial = 0;
		}
	}
	else {
		DBGW(DBG_URB, "no matching urbr\n");
	}

	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo

<<<<<<< HEAD
	DBGI(DBG_GENERAL, "cancelled urb destroyed: %s\n", dbg_urbr(urbr));
	free_urbr(urbr);
=======
	if (urbr != NULL) {
		submit_urbr_unlink(vpdo, urbr->seq_num);

		DBGI(DBG_GENERAL, "cancelled urb destroyed: %s\n", dbg_urbr(urbr));
		ExFreeToNPagedLookasideList(&g_lookaside, urbr);
	}
>>>>>>> 10d26c6... vhci, notify a usbip server of urb cancellation
}

static void
cancel_urbr(PDEVICE_OBJECT devobj, PIRP irp)
{
<<<<<<< HEAD
	UNREFERENCED_PARAMETER(devobj);

	pvpdo_dev_t	vpdo;
	struct urb_req	*urbr;

	vpdo = (pvpdo_dev_t)irp->Tail.Overlay.DriverContext[0];
	urbr = (struct urb_req *)irp->Tail.Overlay.DriverContext[1];

	vpdo = (pvpdo_dev_t)devobj->DeviceExtension;
	DBGI(DBG_GENERAL, "irp will be cancelled: %s\n", dbg_urbr(urbr));
	IoReleaseCancelSpinLock(irp->CancelIrql);

	remove_cancelled_urbr(vpdo, urbr);
=======
	pusbip_vpdo_dev_t	vpdo;

	vpdo = (pusbip_vpdo_dev_t)devobj->DeviceExtension;
	DBGI(DBG_GENERAL, "irp will be cancelled: %p\n", irp);

	remove_cancelled_urbr(vpdo, irp);
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo

	irp->IoStatus.Status = STATUS_CANCELLED;
	irp->IoStatus.Information = 0;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
}

<<<<<<< HEAD
<<<<<<< HEAD
struct urb_req *
create_urbr(pvpdo_dev_t vpdo, PIRP irp, unsigned long seq_num_unlink)
=======
static struct urb_req *
create_urbr(pusbip_vpdo_dev_t vpdo, PIRP irp)
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
=======
struct urb_req *
create_urbr(pusbip_vpdo_dev_t vpdo, PIRP irp, unsigned long seq_num_unlink)
>>>>>>> 10d26c6... vhci, notify a usbip server of urb cancellation
{
	struct urb_req	*urbr;

	urbr = ExAllocateFromNPagedLookasideList(&g_lookaside);
	if (urbr == NULL) {
		DBGE(DBG_URB, "create_urbr: out of memory\n");
		return NULL;
	}
	RtlZeroMemory(urbr, sizeof(*urbr));
	urbr->vpdo = vpdo;
	urbr->irp = irp;
<<<<<<< HEAD
	if (irp != NULL) {
		irp->Tail.Overlay.DriverContext[0] = vpdo;
		irp->Tail.Overlay.DriverContext[1] = urbr;
	}

	urbr->seq_num_unlink = seq_num_unlink;
	InitializeListHead(&urbr->list_all);
	InitializeListHead(&urbr->list_state);
	return urbr;
}

<<<<<<< HEAD
void
free_urbr(struct urb_req *urbr)
{
	ASSERT(IsListEmpty(&urbr->list_all));
	ASSERT(IsListEmpty(&urbr->list_state));
	ExFreeToNPagedLookasideList(&g_lookaside, urbr);
}

BOOLEAN
is_port_urbr(struct urb_req *urbr, unsigned char epaddr)
=======
static BOOLEAN
insert_pending_or_sent_urbr(pusbip_vpdo_dev_t vpdo, struct urb_req *urbr, BOOLEAN is_pending)
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
{
	PIRP	irp = urbr->irp;
	PURB	urb;
	PIO_STACK_LOCATION	irpstack;
	USBD_PIPE_HANDLE	hPipe;

	if (irp == NULL)
		return FALSE;

	irpstack = IoGetCurrentIrpStackLocation(irp);
	urb = irpstack->Parameters.Others.Argument1;
	if (urb == NULL)
		return FALSE;

	switch (urb->UrbHeader.Function) {
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		hPipe = urb->UrbBulkOrInterruptTransfer.PipeHandle;
		break;
	case URB_FUNCTION_ISOCH_TRANSFER:
		hPipe = urb->UrbIsochronousTransfer.PipeHandle;
		break;
	default:
		return FALSE;
	}
<<<<<<< HEAD

	if (PIPE2ADDR(hPipe) == epaddr)
		return TRUE;
	return FALSE;
}

NTSTATUS
submit_urbr(pvpdo_dev_t vpdo, struct urb_req *urbr)
=======
	else {
		IoMarkIrpPending(irp);
		if (is_pending)
			InsertTailList(&vpdo->head_urbr_pending, &urbr->list_state);
		else
			InsertTailList(&vpdo->head_urbr_sent, &urbr->list_state);
	}
	return TRUE;
}

NTSTATUS
submit_urbr(pusbip_vpdo_dev_t vpdo, PIRP irp)
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
=======
	urbr->seq_num_unlink = seq_num_unlink;
	return urbr;
}

NTSTATUS
submit_urbr(pusbip_vpdo_dev_t vpdo, struct urb_req *urbr)
>>>>>>> 10d26c6... vhci, notify a usbip server of urb cancellation
{
	KIRQL	oldirql;
	KIRQL	oldirql_cancel;
	PIRP	read_irp;
	NTSTATUS	status = STATUS_PENDING;

<<<<<<< HEAD
<<<<<<< HEAD
	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	if (vpdo->urbr_sent_partial || vpdo->pending_read_irp == NULL) {
		if (urbr->irp != NULL) {
			IoAcquireCancelSpinLock(&oldirql_cancel);
			IoSetCancelRoutine(urbr->irp, cancel_urbr);
			IoReleaseCancelSpinLock(oldirql_cancel);
			IoMarkIrpPending(urbr->irp);
		}
		InsertTailList(&vpdo->head_urbr_pending, &urbr->list_state);
		InsertTailList(&vpdo->head_urbr, &urbr->list_all);
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

=======
	if ((urbr = create_urbr(vpdo, irp)) == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

=======
>>>>>>> 10d26c6... vhci, notify a usbip server of urb cancellation
	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	if (vpdo->urbr_sent_partial || vpdo->pending_read_irp == NULL) {
		if (urbr->irp != NULL) {
			IoSetCancelRoutine(urbr->irp, cancel_urbr);
			IoMarkIrpPending(urbr->irp);
		}
		InsertTailList(&vpdo->head_urbr_pending, &urbr->list_state);
		InsertTailList(&vpdo->head_urbr, &urbr->list_all);
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
<<<<<<< HEAD
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
=======

>>>>>>> 10d26c6... vhci, notify a usbip server of urb cancellation
		DBGI(DBG_URB, "submit_urbr: urb pending\n");
		return STATUS_PENDING;
	}

<<<<<<< HEAD
	BOOLEAN valid_irp;
	IoAcquireCancelSpinLock(&oldirql_cancel);
	valid_irp = IoSetCancelRoutine(vpdo->pending_read_irp, NULL) != NULL;
	IoReleaseCancelSpinLock(oldirql_cancel);
	if (!valid_irp) {
		DBGI(DBG_URB, "submit_urbr: read irp was cancelled");
		status = STATUS_INVALID_PARAMETER;
		vpdo->pending_read_irp = NULL;
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
		return status;
	}

=======
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
	read_irp = vpdo->pending_read_irp;
	vpdo->urbr_sent_partial = urbr;

	urbr->seq_num = ++(vpdo->seq_num);

	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

	status = store_urbr(read_irp, urbr);

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);

	if (status == STATUS_SUCCESS) {
<<<<<<< HEAD
<<<<<<< HEAD
		if (urbr->irp != NULL) {
			IoAcquireCancelSpinLock(&oldirql_cancel);
			IoSetCancelRoutine(urbr->irp, cancel_urbr);
			IoReleaseCancelSpinLock(oldirql_cancel);
			IoMarkIrpPending(urbr->irp);
		}
		if (vpdo->len_sent_partial == 0) {
			vpdo->urbr_sent_partial = NULL;
			InsertTailList(&vpdo->head_urbr_sent, &urbr->list_state);
		}

		InsertTailList(&vpdo->head_urbr, &urbr->list_all);

		vpdo->pending_read_irp = NULL;
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

		read_irp->IoStatus.Status = STATUS_SUCCESS;
		IoCompleteRequest(read_irp, IO_NO_INCREMENT);
		status = STATUS_PENDING;
=======
=======
		if (urbr->irp != NULL) {
			IoSetCancelRoutine(urbr->irp, cancel_urbr);
			IoMarkIrpPending(urbr->irp);
		}
>>>>>>> 10d26c6... vhci, notify a usbip server of urb cancellation
		if (vpdo->len_sent_partial == 0) {
			vpdo->urbr_sent_partial = NULL;
			InsertTailList(&vpdo->head_urbr_sent, &urbr->list_state);
		}

		InsertTailList(&vpdo->head_urbr, &urbr->list_all);
		vpdo->pending_read_irp = NULL;
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

<<<<<<< HEAD
			read_irp->IoStatus.Status = STATUS_SUCCESS;
			IoCompleteRequest(read_irp, IO_NO_INCREMENT);
			status = STATUS_PENDING;
		}
		else {
			KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
			ExFreeToNPagedLookasideList(&g_lookaside, urbr);
		}
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
=======
		read_irp->IoStatus.Status = STATUS_SUCCESS;
		IoCompleteRequest(read_irp, IO_NO_INCREMENT);
		status = STATUS_PENDING;
>>>>>>> 10d26c6... vhci, notify a usbip server of urb cancellation
	}
	else {
		vpdo->urbr_sent_partial = NULL;
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

		status = STATUS_INVALID_PARAMETER;
	}
	DBGI(DBG_URB, "submit_urbr: urb requested: status:%s\n", dbg_ntstatus(status));
	return status;
}
