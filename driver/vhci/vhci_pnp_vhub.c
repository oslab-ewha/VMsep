#include "vhci.h"

#include <devpkey.h>

#include "usbip_vhci_api.h"
#include "vhci_pnp.h"
#include "vhci_irp.h"
#include "usbreq.h"

#define MAX_HUB_PORTS		6
#define VHUB_FRIENDLY_NAME	L"usbip-win VHUB"

extern BOOLEAN start_vhub(pvhub_dev_t vhub);
extern void stop_vhub(pvhub_dev_t vhub);
extern void remove_vhub(pvhub_dev_t vhub);

extern NTSTATUS
vhub_get_bus_relations(pvhub_dev_t vhub, PDEVICE_RELATIONS *prev_relations);
extern void vhub_remove_all_vpdos(pvhub_dev_t vhub);

static NTSTATUS
vhci_completion_routine(__in PDEVICE_OBJECT devobj, __in PIRP Irp, __in PVOID Context)
{
	UNREFERENCED_PARAMETER(devobj);

	// If the lower driver didn't return STATUS_PENDING, we don't need to
	// set the event because we won't be waiting on it.
	// This optimization avoids grabbing the dispatcher lock and improves perf.
	if (Irp->PendingReturned == TRUE) {
		KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
	}
	return STATUS_MORE_PROCESSING_REQUIRED; // Keep this IRP
}

static PAGEABLE NTSTATUS
vhci_send_irp_synchronously(__in PDEVICE_OBJECT devobj, __in PIRP Irp)
{
	KEVENT		event;
	NTSTATUS	status;

	PAGED_CODE();

	KeInitializeEvent(&event, NotificationEvent, FALSE);

	IoCopyCurrentIrpStackLocationToNext(Irp);

	IoSetCompletionRoutine(Irp, vhci_completion_routine, &event, TRUE, TRUE, TRUE);

	status = IoCallDriver(devobj, Irp);

	// Wait for lower drivers to be done with the Irp.
	// Important thing to note here is when you allocate
	// the memory for an event in the stack you must do a
	// KernelMode wait instead of UserMode to prevent
	// the stack from getting paged out.
	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
		status = Irp->IoStatus.Status;
	}

	return status;
}

static PAGEABLE NTSTATUS
pnp_vhub_query_dev_relations(pvhub_dev_t vhub, PIRP irp, PIO_STACK_LOCATION irpstack)
{
	PDEVICE_RELATIONS	dev_relations;
	NTSTATUS	status;

	if (irpstack->Parameters.QueryDeviceRelations.Type != BusRelations) {
		DBGI(DBG_PNP, "vhub_query_dev_relations: skip: %s\n", dbg_dev_relation(irpstack->Parameters.QueryDeviceRelations.Type));
		return irp->IoStatus.Status;
	}

	dev_relations = (PDEVICE_RELATIONS)irp->IoStatus.Information;
	status = vhub_get_bus_relations(vhub, &dev_relations);
	if (NT_SUCCESS(status)) {
		irp->IoStatus.Information = (ULONG_PTR)dev_relations;
	}
	irp->IoStatus.Status = status;
	return status;
}

PAGEABLE NTSTATUS
vhci_pnp_vhub(pvhub_dev_t vhub, PIRP irp, PIO_STACK_LOCATION irpstack)
{
	NTSTATUS	status = STATUS_SUCCESS;

	PAGED_CODE ();

	switch (irpstack->MinorFunction) {
	case IRP_MN_START_DEVICE:
		if (!start_vhub(vhub))
			status = STATUS_UNSUCCESSFUL;
		break;
	case IRP_MN_QUERY_STOP_DEVICE:
		// The PnP manager is trying to stop the device
		// for resource rebalancing. Fail this now if you
		// cannot stop the device in response to STOP_DEVICE.

		SET_NEW_PNP_STATE(vhub, StopPending);
	case IRP_MN_CANCEL_STOP_DEVICE:
		// The PnP Manager sends this IRP, at some point after an
		// IRP_MN_QUERY_STOP_DEVICE, to inform the drivers for a
		// device that the device will not be stopped for
		// resource reconfiguration.

		// First check to see whether you have received cancel-stop
		// without first receiving a query-stop. This could happen if
		// someone above us fails a query-stop and passes down the subsequent
		// cancel-stop.
		if (StopPending == vhub->common.DevicePnPState) {
			// We did receive a query-stop, so restore.
			RESTORE_PREVIOUS_PNP_STATE(vhub);
			ASSERT(vhub->common.DevicePnPState == Started);
		}
		break;
	case IRP_MN_STOP_DEVICE:
		stop_vhub(vhub);
		status = STATUS_SUCCESS;
		break;
	case IRP_MN_QUERY_REMOVE_DEVICE:
		SET_NEW_PNP_STATE(vhub, RemovePending);
		break;
	case IRP_MN_CANCEL_REMOVE_DEVICE:
		if (vhub->common.DevicePnPState == RemovePending) {
			RESTORE_PREVIOUS_PNP_STATE(vhub);
		}
		break;
	case IRP_MN_REMOVE_DEVICE:
		remove_vhub(vhub);
		status = STATUS_SUCCESS;
		break;
	case IRP_MN_QUERY_DEVICE_RELATIONS:
		status = pnp_vhub_query_dev_relations(vhub, irp, irpstack);
		break;
	default:
		status = irp->IoStatus.Status;
		// In the default case we merely call the next driver.
		// We must not modify Irp->IoStatus.Status or complete the IRP.
		break;
	}

	irp->IoStatus.Status = status;
	IoCompleteRequest(irp, IO_NO_INCREMENT);

	return status;
}

PAGEABLE NTSTATUS
vhci_add_vhub(pvhci_dev_t vhci, PDEVICE_OBJECT pdo)
{
	pvhub_dev_t	vhub;
	PDEVICE_OBJECT	devobj;
	NTSTATUS	status;

	PAGED_CODE();

	DBGI(DBG_GENERAL | DBG_PNP, "adding vhub fdo: pdo: 0x%p\n", pdo);

	status = IoCreateDevice(vhci->common.Self->DriverObject, sizeof(vhub_dev_t), NULL,
		FILE_DEVICE_BUS_EXTENDER, FILE_DEVICE_SECURE_OPEN, TRUE, &devobj);

	if (!NT_SUCCESS(status)) {
		DBGE(DBG_PNP, "failed to IoCreateDevice: %s\n", dbg_ntstatus(status));
		return status;
	}

	vhub = (pvhub_dev_t)devobj->DeviceExtension;
	RtlZeroMemory(vhub, sizeof(vhub_dev_t));

	// Set the initial state of the vhub
	INITIALIZE_PNP_STATE(vhub);

	vhub->common.type = VDEV_VHUB;
	vhub->common.Self = devobj;

	ExInitializeFastMutex(&vhub->Mutex);
	InitializeListHead(&vhub->head_vpdo);

	vhub->OutstandingIO = 1;

	// Initialize the remove event to Not-Signaled.  This event
	// will be set when the OutstandingIO will become 0.
	KeInitializeEvent(&vhub->RemoveEvent, SynchronizationEvent, FALSE);

	// Initialize the stop event to Signaled:
	// there are no Irps that prevent the device from being
	// stopped. This event will be set when the OutstandingIO
	// will become 0.
	KeInitializeEvent(&vhub->StopEvent, SynchronizationEvent, TRUE);

	vhub->vhci = vhci;
	vhci->vhub = vhub;

	vhub->n_max_ports = MAX_HUB_PORTS;

	// Set the initial powerstate of the vhub
	vhub->common.DevicePowerState = PowerDeviceUnspecified;
	vhub->common.SystemPowerState = PowerSystemWorking;

	devobj->Flags |= DO_POWER_PAGABLE | DO_BUFFERED_IO;

	vhub->devobj_lower = IoAttachDeviceToDeviceStack(devobj, pdo);
	if (vhub->devobj_lower == NULL) {
		DBGE(DBG_PNP, "vhub: failed to attach device stack\n");
		IoDeleteDevice(devobj);
		return STATUS_NO_SUCH_DEVICE;
	}

	IoSetDevicePropertyData(vhub->devobj_lower, &DEVPKEY_Device_FriendlyName, LOCALE_NEUTRAL, PLUGPLAY_PROPERTY_PERSISTENT, DEVPROP_TYPE_STRING,
		sizeof(VHUB_FRIENDLY_NAME), (PVOID)VHUB_FRIENDLY_NAME);

	DBGI(DBG_PNP, "vhub fdo added: vhub: %p\n", vhub);

	// We are done with initializing, so let's indicate that and return.
	// This should be the final step in the AddDevice process.
	devobj->Flags &= ~DO_DEVICE_INITIALIZING;

	return STATUS_SUCCESS;
}