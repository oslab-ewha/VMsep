#include "vhci.h"

#include "usbip_vhci_api.h"
#include "vhci_pnp.h"
#include "vhci_irp.h"
#include "usbreq.h"

extern NTSTATUS start_vhci(pvhci_dev_t vhci);
extern void remove_vhci(pvhci_dev_t vhci);

extern phpdo_dev_t
create_hpdo(pvhci_dev_t vhci);

extern NTSTATUS
vhci_get_bus_relations(pvhci_dev_t vhci, PDEVICE_RELATIONS *pdev_relations);
extern PAGEABLE void vhub_remove_all_vpdos(pvhub_dev_t vhub);

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

PAGEABLE NTSTATUS
vhci_add_vhci(__in PDRIVER_OBJECT drvobj, __in PDEVICE_OBJECT pdo, phpdo_dev_t hpdo)
{
	PDEVICE_OBJECT	devobj;
	pvhci_dev_t	vhci = NULL;
	NTSTATUS	status;

	PAGED_CODE();

	DBGI(DBG_GENERAL | DBG_PNP, "adding vhci fdo: pdo: 0x%p\n", pdo);

	status = IoCreateDevice(drvobj, sizeof(vhci_dev_t), NULL,
		FILE_DEVICE_BUS_EXTENDER, FILE_DEVICE_SECURE_OPEN, TRUE, &devobj);

	if (!NT_SUCCESS(status)) {
		DBGE(DBG_PNP, "failed to IoCreateDevice: %s\n", dbg_ntstatus(status));
		return status;
	}

	vhci = (pvhci_dev_t)devobj->DeviceExtension;
	RtlZeroMemory(vhci, sizeof(vhci_dev_t));

	// Set the initial state of the vhub
	INITIALIZE_PNP_STATE(vhci);

	vhci->common.type = VDEV_VHCI;
	vhci->common.Self = devobj;

	// Set the vpdo for use with PlugPlay functions
	vhci->UnderlyingPDO = pdo;

	// Set the initial powerstate of the vhub
	vhci->common.DevicePowerState = PowerDeviceUnspecified;
	vhci->common.SystemPowerState = PowerSystemWorking;

	devobj->Flags |= DO_POWER_PAGABLE | DO_BUFFERED_IO;

	// Tell the Plug & Play system that this device will need a
	// device interface.
	status = IoRegisterDeviceInterface(pdo, (LPGUID)&GUID_DEVINTERFACE_VHCI_USBIP, NULL, &vhci->DevIntfVhci);
	if (!NT_SUCCESS(status)) {
		DBGE(DBG_PNP, "failed to register vhci device interface: %s\n", dbg_ntstatus(status));
		IoDeleteDevice(devobj);
		return status;
	}
	status = IoRegisterDeviceInterface(pdo, (LPGUID)&GUID_DEVINTERFACE_USB_HOST_CONTROLLER, NULL, &vhci->DevIntfUSBHC);
	if (!NT_SUCCESS(status)) {
		DBGE(DBG_PNP, "failed to register USB Host controller device interface: %s\n", dbg_ntstatus(status));
		RtlFreeUnicodeString(&vhci->DevIntfVhci);
		IoDeleteDevice(devobj);
		return status;
	}

	// Attach our vhub to the device stack.
	// The return value of IoAttachDeviceToDeviceStack is the top of the
	// attachment chain.  This is where all the IRPs should be routed.
	vhci->devobj_lower = IoAttachDeviceToDeviceStack(devobj, pdo);
	if (vhci->devobj_lower == NULL) {
		DBGE(DBG_PNP, "failed to attach device stack\n");
		RtlFreeUnicodeString(&vhci->DevIntfVhci);
		RtlFreeUnicodeString(&vhci->DevIntfUSBHC);
		IoDeleteDevice(devobj);
		return STATUS_NO_SUCH_DEVICE;
	}

	vhci->hpdo = hpdo;
	if (vhci->hpdo == NULL) {
		vhci->hpdo = create_hpdo(vhci);
	}

	DBGI(DBG_PNP, "vhci fdo added: vhci:%p\n", vhci);

	// We are done with initializing, so let's indicate that and return.
	// This should be the final step in the AddDevice process.
	devobj->Flags &= ~DO_DEVICE_INITIALIZING;

	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS
pnp_vhci_query_dev_relations(pvhci_dev_t vhci, PIRP irp, PIO_STACK_LOCATION irpstack)
{
	PDEVICE_RELATIONS	dev_relations;
	NTSTATUS	status;

	DBGI(DBG_PNP, "vhci:query dev relations: %s\n", dbg_dev_relation(irpstack->Parameters.QueryDeviceRelations.Type));

	if (irpstack->Parameters.QueryDeviceRelations.Type != BusRelations) {
		DBGI(DBG_PNP, "query_dev_relations: skip: %s\n", dbg_dev_relation(irpstack->Parameters.QueryDeviceRelations.Type));
		return irp->IoStatus.Status;
	}

	dev_relations = (PDEVICE_RELATIONS)irp->IoStatus.Information;
	status = vhci_get_bus_relations(vhci, &dev_relations);
	if (NT_SUCCESS(status)) {
		irp->IoStatus.Information = (ULONG_PTR)dev_relations;
	}
	irp->IoStatus.Status = status;
	return status;
}

PAGEABLE NTSTATUS
vhci_pnp_vhci(pvhci_dev_t vhci, PIRP irp, PIO_STACK_LOCATION irpstack)
{
	NTSTATUS	status = STATUS_SUCCESS;

	PAGED_CODE();

	switch (irpstack->MinorFunction) {
	case IRP_MN_START_DEVICE:
		// Send the Irp down and wait for it to come back.
		// Do not touch the hardware until then.
		status = vhci_send_irp_synchronously(vhci->devobj_lower, irp);
		if (NT_SUCCESS(status)) {
			// Initialize your device with the resources provided
			// by the PnP manager to your device.
			status = start_vhci(vhci);
		}
		irp->IoStatus.Status = status;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return status;
	case IRP_MN_QUERY_STOP_DEVICE:
		// The PnP manager is trying to stop the device
		// for resource rebalancing. Fail this now if you
		// cannot stop the device in response to STOP_DEVICE.

		SET_NEW_PNP_STATE(vhci, StopPending);
		irp->IoStatus.Status = STATUS_SUCCESS;
		break;
	case IRP_MN_CANCEL_STOP_DEVICE:
		// The PnP Manager sends this IRP, at some point after an
		// IRP_MN_QUERY_STOP_DEVICE, to inform the drivers for a
		// device that the device will not be stopped for
		// resource reconfiguration.

		// First check to see whether you have received cancel-stop
		// without first receiving a query-stop. This could happen if
		// someone above us fails a query-stop and passes down the subsequent
		// cancel-stop.
		if (vhci->common.DevicePnPState == StopPending) {
			// We did receive a query-stop, so restore.
			RESTORE_PREVIOUS_PNP_STATE(vhci);
			ASSERT(vhci->common.DevicePnPState == Started);
		}
		irp->IoStatus.Status = STATUS_SUCCESS; // We must not fail the IRP.
		break;
	case IRP_MN_STOP_DEVICE:
		// Stop device means that the resources given during Start device
		// are now revoked. Note: You must not fail this Irp.
		// But before you relieve resources make sure there are no I/O in
		// progress. Wait for the existing ones to be finished.
		// To do that, first we will decrement this very operation.
		// When the counter goes to 1, Stop event is set.

		// Free resources given by start device.
		SET_NEW_PNP_STATE(vhci, Stopped);

		// We don't need a completion routine so fire and forget.
		// Set the current stack location to the next stack location and
		// call the next device object.

		irp->IoStatus.Status = STATUS_SUCCESS;
		break;
	case IRP_MN_QUERY_REMOVE_DEVICE:
		// If we were to fail this call then we would need to complete the
		// IRP here.  Since we are not, set the status to SUCCESS and
		// call the next driver.

		SET_NEW_PNP_STATE(vhci, RemovePending);

		irp->IoStatus.Status = STATUS_SUCCESS;
		break;
	case IRP_MN_CANCEL_REMOVE_DEVICE:
		// If we were to fail this call then we would need to complete the
		// IRP here.  Since we are not, set the status to SUCCESS and
		// call the next driver.

		// First check to see whether you have received cancel-remove
		// without first receiving a query-remove. This could happen if
		// someone above us fails a query-remove and passes down the
		// subsequent cancel-remove.

		if (vhci->common.DevicePnPState == RemovePending) {
			// We did receive a query-remove, so restore.
			RESTORE_PREVIOUS_PNP_STATE(vhci);
		}
		irp->IoStatus.Status = STATUS_SUCCESS;// You must not fail the IRP.
		break;
	case IRP_MN_REMOVE_DEVICE:
		status = irp_pass_down(vhci->devobj_lower, irp);
		if (NT_SUCCESS(status))
			remove_vhci(vhci);
		return status;
	case IRP_MN_QUERY_DEVICE_RELATIONS:
		status = pnp_vhci_query_dev_relations(vhci, irp, irpstack);
		break;
	default:
		// In the default case we merely call the next driver.
		// We must not modify Irp->IoStatus.Status or complete the IRP.
		break;
	}

	IoSkipCurrentIrpStackLocation(irp);
	status = IoCallDriver(vhci->devobj_lower, irp);

	return status;
}