#include "vhci.h"

<<<<<<< HEAD
#include <wdmguid.h>

#include "vhci_pnp.h"
#include "vhci_irp.h"

extern NTSTATUS
pnp_query_id(pvdev_t vdev, PIRP irp, PIO_STACK_LOCATION irpstack);

extern NTSTATUS
pnp_query_device_text(pvdev_t vdev, PIRP irp, PIO_STACK_LOCATION irpstack);

extern NTSTATUS
pnp_query_interface(pvdev_t vdev, PIRP irp, PIO_STACK_LOCATION irpstack);

extern NTSTATUS
pnp_query_dev_relations(pvdev_t vdev, PIRP irp, PIO_STACK_LOCATION irpstack);

extern NTSTATUS
pnp_query_capabilities(pvdev_t vdev, PIRP irp, PIO_STACK_LOCATION irpstack);

extern NTSTATUS
pnp_start_device(pvdev_t vdev, PIRP irp);

extern NTSTATUS
pnp_remove_device(pvdev_t vdev, PIRP irp);

extern BOOLEAN
process_pnp_vpdo(pvpdo_dev_t vpdo, PIRP irp, PIO_STACK_LOCATION irpstack);

extern NTSTATUS
pnp_query_resource_requirements(pvdev_t vdev, PIRP irp);

extern NTSTATUS
pnp_query_resources(pvdev_t vdev, PIRP irp);

extern NTSTATUS
pnp_filter_resource_requirements(pvdev_t vdev, PIRP irp);

#define IRP_PASS_DOWN_OR_SUCCESS(vdev, irp)			\
	do {							\
		if (IS_FDO((vdev)->type)) {			\
			irp->IoStatus.Status = STATUS_SUCCESS;	\
			return irp_pass_down((vdev)->devobj_lower, irp);	\
		}						\
		else						\
			return irp_success(irp);		\
	} while (0)

static PAGEABLE NTSTATUS
pnp_query_stop_device(pvdev_t vdev, PIRP irp)
{
	SET_NEW_PNP_STATE(vdev, StopPending);
	IRP_PASS_DOWN_OR_SUCCESS(vdev, irp);
}

static PAGEABLE NTSTATUS
pnp_cancel_stop_device(pvdev_t vdev, PIRP irp)
{
	if (vdev->DevicePnPState == StopPending) {
		// We did receive a query-stop, so restore.
		RESTORE_PREVIOUS_PNP_STATE(vdev);
		ASSERT(vdev->DevicePnPState == Started);
=======
#include "usbip_vhci_api.h"
#include "vhci_pnp.h"
#include "usbreq.h"

#define INITIALIZE_PNP_STATE(_Data_)    \
        (_Data_)->common.DevicePnPState =  NotStarted;\
        (_Data_)->common.PreviousPnPState = NotStarted;

extern PAGEABLE NTSTATUS
vhci_pnp_vpdo(PDEVICE_OBJECT devobj, PIRP Irp, PIO_STACK_LOCATION IrpStack, pusbip_vpdo_dev_t vpdo);

extern PAGEABLE NTSTATUS
reg_wmi(__in pusbip_vhub_dev_t vhub);

extern PAGEABLE NTSTATUS
dereg_wmi(__in pusbip_vhub_dev_t vhub);

PAGEABLE NTSTATUS
vhci_add_device(__in PDRIVER_OBJECT drvobj, __in PDEVICE_OBJECT devobj_lower)
{
	PDEVICE_OBJECT		devobj;
	pusbip_vhub_dev_t	vhub = NULL;
	PWCHAR		deviceName = NULL;
	ULONG		nameLength;
	NTSTATUS	status;

	PAGED_CODE();

	DBGI(DBG_GENERAL | DBG_PNP, "Add Device: 0x%p\n", devobj_lower);

	status = IoCreateDevice(drvobj, sizeof(usbip_vhub_dev_t), NULL,
		FILE_DEVICE_BUS_EXTENDER, FILE_DEVICE_SECURE_OPEN, TRUE, &devobj);

	if (!NT_SUCCESS(status)) {
		goto End;
	}

	vhub = (pusbip_vhub_dev_t)devobj->DeviceExtension;
	RtlZeroMemory(vhub, sizeof(usbip_vhub_dev_t));

	// Set the initial state of the vhub
	INITIALIZE_PNP_STATE(vhub);

	vhub->common.is_vhub = TRUE;
	vhub->common.Self = devobj;

	ExInitializeFastMutex(&vhub->Mutex);

	InitializeListHead(&vhub->head_vpdo);

	// Set the vpdo for use with PlugPlay functions
	vhub->UnderlyingPDO = devobj_lower;

	// Set the initial powerstate of the vhub
	vhub->common.DevicePowerState = PowerDeviceUnspecified;
	vhub->common.SystemPowerState = PowerSystemWorking;

	// Biased to 1. Transition to zero during remove device
	// means IO is finished. Transition to 1 means the device
	// can be stopped.
	vhub->OutstandingIO = 1;

	// Initialize the remove event to Not-Signaled.  This event
	// will be set when the OutstandingIO will become 0.
	KeInitializeEvent(&vhub->RemoveEvent, SynchronizationEvent, FALSE);

	// Initialize the stop event to Signaled:
	// there are no Irps that prevent the device from being
	// stopped. This event will be set when the OutstandingIO
	// will become 0.
	KeInitializeEvent(&vhub->StopEvent, SynchronizationEvent, TRUE);

	devobj->Flags |= DO_POWER_PAGABLE|DO_BUFFERED_IO;

	// Tell the Plug & Play system that this device will need a
	// device interface.
	status = IoRegisterDeviceInterface(devobj_lower, (LPGUID) &GUID_DEVINTERFACE_VHCI_USBIP, NULL, &vhub->InterfaceName);

	if (!NT_SUCCESS (status)) {
		DBGE(DBG_PNP, "AddDevice: IoRegisterDeviceInterface failed (%x)", status);
		goto End;
	}

	// Attach our vhub to the device stack.
	// The return value of IoAttachDeviceToDeviceStack is the top of the
	// attachment chain.  This is where all the IRPs should be routed.
	vhub->NextLowerDriver = IoAttachDeviceToDeviceStack(devobj, devobj_lower);

	if (vhub->NextLowerDriver == NULL) {
		status = STATUS_NO_SUCH_DEVICE;
		goto End;
	}
#if DBG
	// We will demonstrate here the step to retrieve the name of the vpdo
	status = IoGetDeviceProperty(devobj_lower, DevicePropertyPhysicalDeviceObjectName, 0, NULL, &nameLength);

	if (status != STATUS_BUFFER_TOO_SMALL) {
		DBGE(DBG_PNP, "AddDevice:IoGDP failed (0x%x)\n", status);
		goto End;
	}

	deviceName = ExAllocatePoolWithTag (NonPagedPool, nameLength, USBIP_VHCI_POOL_TAG);

	if (NULL == deviceName) {
		DBGE(DBG_PNP, "AddDevice: no memory to alloc for deviceName(0x%x)\n", nameLength);
		status =  STATUS_INSUFFICIENT_RESOURCES;
		goto End;
	}

	status = IoGetDeviceProperty(devobj_lower, DevicePropertyPhysicalDeviceObjectName,
				nameLength, deviceName, &nameLength);

	if (!NT_SUCCESS (status)) {
		DBGE(DBG_PNP, "AddDevice:IoGDP(2) failed (0x%x)", status);
		goto End;
	}

	DBGI(DBG_PNP, "AddDevice: %p to %p->%p (%ws) \n", vhub, vhub->NextLowerDriver, devobj_lower, deviceName);
#endif

	// We are done with initializing, so let's indicate that and return.
	// This should be the final step in the AddDevice process.
	devobj->Flags &= ~DO_DEVICE_INITIALIZING;

End:
	if (deviceName) {
		ExFreePool(deviceName);
	}

	if (!NT_SUCCESS(status) && devobj) {
		if (vhub && vhub->NextLowerDriver) {
			IoDetachDevice(vhub->NextLowerDriver);
		}
		IoDeleteDevice (devobj);
	}

	return status;
}

static PAGEABLE void
invalidate_vhub(pusbip_vhub_dev_t vhub)
{
	PAGED_CODE();

	// Stop all access to the device, fail any outstanding I/O to the device,
	// and free all the resources associated with the device.

	// Disable the device interface and free the buffer
	if (vhub->InterfaceName.Buffer != NULL) {
		IoSetDeviceInterfaceState(&vhub->InterfaceName, FALSE);

		ExFreePool(vhub->InterfaceName.Buffer);
		RtlZeroMemory(&vhub->InterfaceName, sizeof(UNICODE_STRING));
	}

	// Inform WMI to remove this DeviceObject from its list of providers.
	dereg_wmi(vhub);
}

PAGEABLE NTSTATUS
destroy_vpdo(pusbip_vpdo_dev_t vpdo)
{
	PAGED_CODE();

	// VHCI does not queue any irps at this time so we have nothing to do.
	// Free any resources.

	//FIXME
<<<<<<< HEAD
	if (PdoData->fo) {
		PdoData->fo->FsContext = NULL;
		PdoData->fo = NULL;
>>>>>>> 48e1018... Beautify VHCI driver code
	}
	IRP_PASS_DOWN_OR_SUCCESS(vdev, irp);
}

static PAGEABLE NTSTATUS
<<<<<<< HEAD
pnp_stop_device(pvdev_t vdev, PIRP irp)
{
	SET_NEW_PNP_STATE(vdev, Stopped);
	IRP_PASS_DOWN_OR_SUCCESS(vdev, irp);
}

static PAGEABLE NTSTATUS
pnp_query_remove_device(pvdev_t vdev, PIRP irp)
{
	switch (vdev->type) {
	case VDEV_VPDO:
		/* vpdo cannot be removed */
		vhub_mark_unplugged_vpdo(VHUB_FROM_VPDO((pvpdo_dev_t)vdev), (pvpdo_dev_t)vdev);
		break;
	default:
		break;
=======
Bus_StartFdo(__in PFDO_DEVICE_DATA FdoData, __in PIRP Irp)
=======
	if (vpdo->fo) {
		vpdo->fo->FsContext = NULL;
		vpdo->fo = NULL;
	}
	DBGI(DBG_PNP, "Deleting vpdo: 0x%p\n", vpdo);
	IoDeleteDevice(vpdo->common.Self);
	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS
start_vhub(pusbip_vhub_dev_t vhub)
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
{
	POWER_STATE	powerState;
	NTSTATUS	status;

	PAGED_CODE();

	// Check the function driver source to learn
	// about parsing resource list.

	// Enable device interface. If the return status is
	// STATUS_OBJECT_NAME_EXISTS means we are enabling the interface
	// that was already enabled, which could happen if the device
	// is stopped and restarted for resource rebalancing.
	status = IoSetDeviceInterfaceState(&vhub->InterfaceName, TRUE);
	if (!NT_SUCCESS(status)) {
		DBGE(DBG_PNP, "IoSetDeviceInterfaceState failed: 0x%x\n", status);
		return status;
	}

	// Set the device power state to fully on. Also if this Start
	// is due to resource rebalance, you should restore the device
	// to the state it was before you stopped the device and relinquished
	// resources.

	vhub->common.DevicePowerState = PowerDeviceD0;
	powerState.DeviceState = PowerDeviceD0;
	PoSetPowerState(vhub->common.Self, DevicePowerState, powerState);

	SET_NEW_PNP_STATE(vhub, Started);

	// Register with WMI
	status = reg_wmi(vhub);
	if (!NT_SUCCESS(status)) {
		DBGE(DBG_PNP, "start_vhub: reg_wmi failed (%x)\n", status);
	}

	return status;
}

static NTSTATUS
vhci_completion_routine(__in PDEVICE_OBJECT devobj, __in PIRP Irp, __in PVOID Context)
{
	UNREFERENCED_PARAMETER(devobj);

	// If the lower driver didn't return STATUS_PENDING, we don't need to
	// set the event because we won't be waiting on it.
	// This optimization avoids grabbing the dispatcher lock and improves perf.
	if (Irp->PendingReturned == TRUE) {
		KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
>>>>>>> 48e1018... Beautify VHCI driver code
	}
	SET_NEW_PNP_STATE(vdev, RemovePending);
	IRP_PASS_DOWN_OR_SUCCESS(vdev, irp);
}

static PAGEABLE NTSTATUS
<<<<<<< HEAD
<<<<<<< HEAD
pnp_cancel_remove_device(pvdev_t vdev, PIRP irp)
{
	if (vdev->DevicePnPState == RemovePending) {
		RESTORE_PREVIOUS_PNP_STATE(vdev);
=======
Bus_SendIrpSynchronously(__in PDEVICE_OBJECT DeviceObject, __in PIRP Irp)
=======
vhci_send_irp_synchronously(__in PDEVICE_OBJECT devobj, __in PIRP Irp)
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
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
>>>>>>> 48e1018... Beautify VHCI driver code
	}
	IRP_PASS_DOWN_OR_SUCCESS(vdev, irp);
}

static PAGEABLE NTSTATUS
<<<<<<< HEAD
<<<<<<< HEAD
pnp_surprise_removal(pvdev_t vdev, PIRP irp)
{
	SET_NEW_PNP_STATE(vdev, SurpriseRemovePending);
	IRP_PASS_DOWN_OR_SUCCESS(vdev, irp);
=======
Bus_FDO_PnP(__in PDEVICE_OBJECT DeviceObject, __in PIRP Irp, __in PIO_STACK_LOCATION IrpStack, __in PFDO_DEVICE_DATA DeviceData)
=======
vhci_pnp_vhub(PDEVICE_OBJECT devobj, PIRP Irp, PIO_STACK_LOCATION IrpStack, pusbip_vhub_dev_t vhub)
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
{
	pusbip_vpdo_dev_t	vpdo;
	PDEVICE_RELATIONS	relations, oldRelations;
	ULONG			length, prevcount, n_vpdos_cur;
	PLIST_ENTRY		entry, listHead, nextEntry;
	NTSTATUS		status;

	PAGED_CODE ();

	inc_io_vhub(vhub);

	switch (IrpStack->MinorFunction) {
	case IRP_MN_START_DEVICE:
		// Send the Irp down and wait for it to come back.
		// Do not touch the hardware until then.
		status = vhci_send_irp_synchronously(vhub->NextLowerDriver, Irp);
		if (NT_SUCCESS(status)) {
			// Initialize your device with the resources provided
			// by the PnP manager to your device.
			status = start_vhub(vhub);
		}

		// We must now complete the IRP, since we stopped it in the
		// completion routine with MORE_PROCESSING_REQUIRED.
		Irp->IoStatus.Status = status;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		dec_io_vhub(vhub);
		return status;
	case IRP_MN_QUERY_STOP_DEVICE:
		// The PnP manager is trying to stop the device
		// for resource rebalancing. Fail this now if you
		// cannot stop the device in response to STOP_DEVICE.

		SET_NEW_PNP_STATE(vhub, StopPending);
		Irp->IoStatus.Status = STATUS_SUCCESS; // You must not fail the IRP.
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
		if (StopPending == vhub->common.DevicePnPState) {
			// We did receive a query-stop, so restore.
			RESTORE_PREVIOUS_PNP_STATE(vhub);
			ASSERT(vhub->common.DevicePnPState == Started);
		}
		Irp->IoStatus.Status = STATUS_SUCCESS; // We must not fail the IRP.
		break;
	case IRP_MN_STOP_DEVICE:
		// Stop device means that the resources given during Start device
		// are now revoked. Note: You must not fail this Irp.
		// But before you relieve resources make sure there are no I/O in
		// progress. Wait for the existing ones to be finished.
		// To do that, first we will decrement this very operation.
		// When the counter goes to 1, Stop event is set.
		
		dec_io_vhub(vhub);

		KeWaitForSingleObject(&vhub->StopEvent, Executive, KernelMode, FALSE, NULL);

		// Increment the counter back because this IRP has to
		// be sent down to the lower stack.
		inc_io_vhub(vhub);

		// Free resources given by start device.
		SET_NEW_PNP_STATE(vhub, Stopped);

		// We don't need a completion routine so fire and forget.
		// Set the current stack location to the next stack location and
		// call the next device object.

		Irp->IoStatus.Status = STATUS_SUCCESS;
		break;
	case IRP_MN_QUERY_REMOVE_DEVICE:
		// If we were to fail this call then we would need to complete the
		// IRP here.  Since we are not, set the status to SUCCESS and
		// call the next driver.

		SET_NEW_PNP_STATE(vhub, RemovePending);

		Irp->IoStatus.Status = STATUS_SUCCESS;
		break;
	case IRP_MN_CANCEL_REMOVE_DEVICE:
		// If we were to fail this call then we would need to complete the
		// IRP here.  Since we are not, set the status to SUCCESS and
		// call the next driver.

		// First check to see whether you have received cancel-remove
		// without first receiving a query-remove. This could happen if
		// someone above us fails a query-remove and passes down the
		// subsequent cancel-remove.

		if (RemovePending == vhub->common.DevicePnPState) {
			// We did receive a query-remove, so restore.
			RESTORE_PREVIOUS_PNP_STATE(vhub);
		}
		Irp->IoStatus.Status = STATUS_SUCCESS;// You must not fail the IRP.
		break;
	case IRP_MN_SURPRISE_REMOVAL:
		// The device has been unexpectedly removed from the machine
		// and is no longer available for I/O. invalidate_vhub clears
		// all the resources, frees the interface and de-registers
		// with WMI, but it doesn't delete the vhub. That's done
		// later in Remove device query.

		SET_NEW_PNP_STATE(vhub, SurpriseRemovePending);
		invalidate_vhub(vhub);

		ExAcquireFastMutex(&vhub->Mutex);

		listHead = &vhub->head_vpdo;

		for (entry = listHead->Flink,nextEntry = entry->Flink; entry != listHead; entry = nextEntry,nextEntry = entry->Flink) {
			vpdo = CONTAINING_RECORD(entry, usbip_vpdo_dev_t, Link);
			RemoveEntryList(&vpdo->Link);
			InitializeListHead(&vpdo->Link);
			vpdo->vhub = NULL;
			vpdo->ReportedMissing = TRUE;
		}

		ExReleaseFastMutex(&vhub->Mutex);

		Irp->IoStatus.Status = STATUS_SUCCESS; // You must not fail the IRP.
		break;
	case IRP_MN_REMOVE_DEVICE:
		// The Plug & Play system has dictated the removal of this device.
		// We have no choice but to detach and delete the device object.

		// Check the state flag to see whether you are surprise removed
		if (vhub->common.DevicePnPState != SurpriseRemovePending) {
			invalidate_vhub(vhub);
		}

		SET_NEW_PNP_STATE(vhub, Deleted);

		// Wait for all outstanding requests to complete.
		// We need two decrements here, one for the increment in
		// the beginning of this function, the other for the 1-biased value of
		// OutstandingIO.

		dec_io_vhub(vhub);

		// The requestCount is at least one here (is 1-biased)

		dec_io_vhub(vhub);

		KeWaitForSingleObject(&vhub->RemoveEvent, Executive, KernelMode, FALSE, NULL);

		// Typically the system removes all the  children before
		// removing the parent vhub. If for any reason child vpdo's are
		// still present we will destroy them explicitly, with one exception -
		// we will not delete the vpdos that are in SurpriseRemovePending state.

		ExAcquireFastMutex(&vhub->Mutex);

		listHead = &vhub->head_vpdo;

		for (entry = listHead->Flink,nextEntry = entry->Flink; entry != listHead; entry = nextEntry,nextEntry = entry->Flink) {
			vpdo = CONTAINING_RECORD(entry, usbip_vpdo_dev_t, Link);
			RemoveEntryList (&vpdo->Link);
			if (SurpriseRemovePending == vpdo->common.DevicePnPState) {
				// We will reinitialize the list head so that we
				// wouldn't barf when we try to delink this vpdo from
				// the parent's vpdo list, when the system finally
				// removes the vpdo. Let's also not forget to set the
				// ReportedMissing flag to cause the deletion of the vpdo.
				DBGI(DBG_PNP, "\tFound a surprise removed device: 0x%p\n", vpdo->common.Self);
				InitializeListHead(&vpdo->Link);
				vpdo->vhub = NULL;
				vpdo->ReportedMissing = TRUE;
				continue;
			}
			vhub->n_vpdos--;
			destroy_vpdo(vpdo);
		}

		ExReleaseFastMutex(&vhub->Mutex);

		// We need to send the remove down the stack before we detach,
		// but we don't need to wait for the completion of this operation
		// (and to register a completion routine).
		Irp->IoStatus.Status = STATUS_SUCCESS;
		IoSkipCurrentIrpStackLocation(Irp);
		status = IoCallDriver(vhub->NextLowerDriver, Irp);

		// Detach from the underlying devices.
		IoDetachDevice(vhub->NextLowerDriver);

		DBGI(DBG_PNP, "Deleting vhub device object: 0x%p\n", devobj);

		IoDeleteDevice(devobj);

		return status;
	case IRP_MN_QUERY_DEVICE_RELATIONS:
		DBGI(DBG_PNP, "\tQueryDeviceRelation Type: %s\n", dbg_dev_relation(IrpStack->Parameters.QueryDeviceRelations.Type));

		if (BusRelations != IrpStack->Parameters.QueryDeviceRelations.Type) {
			// We don't support any other Device Relations
			break;
		}

		// Tell the plug and play system about all the vpdos.

		// There might also be device relations below and above this vhub,
		// so, be sure to propagate the relations from the upper drivers.

		// No Completion routine is needed so long as the status is preset
		// to success.  (vpdos complete plug and play irps with the current
		// IoStatus.Status and IoStatus.Information as the default.)

		ExAcquireFastMutex(&vhub->Mutex);

		oldRelations = (PDEVICE_RELATIONS)Irp->IoStatus.Information;
		if (oldRelations) {
			prevcount = oldRelations->Count;
			if (vhub->n_vpdos == 0) {
				// There is a device relations struct already present and we have
				// nothing to add to it, so just call IoSkip and IoCall
				ExReleaseFastMutex(&vhub->Mutex);
				break;
			}
		}
		else  {
			prevcount = 0;
		}

		// Calculate the number of vpdos actually present on the bus
		n_vpdos_cur = 0;
		for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
			vpdo = CONTAINING_RECORD(entry, usbip_vpdo_dev_t, Link);
			if (vpdo->Present)
				n_vpdos_cur++;
		}

		// Need to allocate a new relations structure and add our vpdos to it
		length = sizeof(DEVICE_RELATIONS) + ((n_vpdos_cur + prevcount) * sizeof(PDEVICE_OBJECT)) - 1;

		relations = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(PagedPool, length, USBIP_VHCI_POOL_TAG);

		if (relations == NULL) {
			// Fail the IRP
			ExReleaseFastMutex(&vhub->Mutex);
			Irp->IoStatus.Status = status = STATUS_INSUFFICIENT_RESOURCES;
			IoCompleteRequest(Irp, IO_NO_INCREMENT);
			dec_io_vhub(vhub);
			return status;
		}

		// Copy in the device objects so far
		if (prevcount) {
			RtlCopyMemory(relations->Objects, oldRelations->Objects, prevcount * sizeof(PDEVICE_OBJECT));
		}

		relations->Count = prevcount + n_vpdos_cur;

		// For each vpdo present on this bus add a pointer to the device relations
		// buffer, being sure to take out a reference to that object.
		// The Plug & Play system will dereference the object when it is done
		// with it and free the device relations buffer.
		for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
			vpdo = CONTAINING_RECORD(entry, usbip_vpdo_dev_t, Link);
			if (vpdo->Present) {
				relations->Objects[prevcount] = vpdo->common.Self;
				ObReferenceObject(vpdo->common.Self);
				prevcount++;
			} else {
				vpdo->ReportedMissing = TRUE;
			}
		}

		DBGI(DBG_PNP, "# of vpdo's: present: %d, reported: %d\n", vhub->n_vpdos, relations->Count);

		// Replace the relations structure in the IRP with the new
		// one.
		if (oldRelations) {
			ExFreePool(oldRelations);
		}
		Irp->IoStatus.Information = (ULONG_PTR) relations;

		ExReleaseFastMutex(&vhub->Mutex);

		// Set up and pass the IRP further down the stack
		Irp->IoStatus.Status = STATUS_SUCCESS;
		break;
	default:
		// In the default case we merely call the next driver.
		// We must not modify Irp->IoStatus.Status or complete the IRP.
		break;
	}

	IoSkipCurrentIrpStackLocation(Irp);
	status = IoCallDriver(vhub->NextLowerDriver, Irp);
	dec_io_vhub(vhub);

	return status;
>>>>>>> 48e1018... Beautify VHCI driver code
}

<<<<<<< HEAD
static PAGEABLE NTSTATUS
pnp_query_bus_information(PIRP irp)
{
<<<<<<< HEAD
	PPNP_BUS_INFORMATION busInfo;

	PAGED_CODE();

	busInfo = ExAllocatePoolWithTag(PagedPool, sizeof(PNP_BUS_INFORMATION), USBIP_VHCI_POOL_TAG);
=======
	PCOMMON_DEVICE_DATA	commonData;
=======
PAGEABLE NTSTATUS
vhci_pnp(__in PDEVICE_OBJECT devobj, __in PIRP Irp)
{
	pdev_common_t		devcom;
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
	PIO_STACK_LOCATION	irpStack;
	NTSTATUS	status;

	PAGED_CODE();

	DBGI(DBG_GENERAL | DBG_PNP, "vhci_pnp: Enter\n");

	irpStack = IoGetCurrentIrpStackLocation(Irp);
	ASSERT(IRP_MJ_PNP == irpStack->MajorFunction);

	devcom = (pdev_common_t)devobj->DeviceExtension;

	// If the device has been removed, the driver should
	// not pass the IRP down to the next lower driver.
	if (devcom->DevicePnPState == Deleted) {
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	if (devcom->is_vhub) {
		DBGI(DBG_PNP, "vhub: minor: %s, IRP:0x%p\n", dbg_pnp_minor(irpStack->MinorFunction), Irp);

		// Request is for the vhub
		status = vhci_pnp_vhub(devobj, Irp, irpStack, (pusbip_vhub_dev_t)devcom);
	}
	else {
		DBGI(DBG_PNP, "vpdo: minor: %s, IRP: 0x%p\n", dbg_pnp_minor(irpStack->MinorFunction), Irp);

		// Request is for the child vpdo.
		status = vhci_pnp_vpdo(devobj, Irp, irpStack, (pusbip_vpdo_dev_t)devcom);
	}

	DBGI(DBG_GENERAL | DBG_PNP, "vhci_pnp: Leave: %s\n", dbg_ntstatus(status));

	return status;
}

static void
complete_pending_read_irp(pusbip_vpdo_dev_t vpdo)
{
	KIRQL	oldirql;
	PIRP	irp;
>>>>>>> 48e1018... Beautify VHCI driver code

<<<<<<< HEAD
	if (busInfo == NULL) {
		return STATUS_INSUFFICIENT_RESOURCES;
=======
	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);
	irp = vpdo->pending_read_irp;
	vpdo->pending_read_irp = NULL;
	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

	if (irp != NULL) {
		irp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
		IoSetCancelRoutine(irp, NULL);
		KeRaiseIrql(DISPATCH_LEVEL, &oldirql);
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		KeLowerIrql(oldirql);
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
	}
<<<<<<< HEAD

	busInfo->BusTypeGuid = GUID_BUS_TYPE_USB;

	// Some buses have a specific INTERFACE_TYPE value,
	// such as PCMCIABus, PCIBus, or PNPISABus.
	// For other buses, especially newer buses like USBIP, the bus
	// driver sets this member to PNPBus.
	busInfo->LegacyBusType = PNPBus;

	// This is an hypothetical bus
	busInfo->BusNumber = 10;
	irp->IoStatus.Information = (ULONG_PTR)busInfo;

	return irp_success(irp);
=======
}

static void
complete_pending_irp(pusbip_vpdo_dev_t vpdo)
{
	int	count = 0;
	KIRQL oldirql;

	//FIXME
	DBGI(DBG_PNP, "finish pending irp\n");
	KeRaiseIrql(DISPATCH_LEVEL, &oldirql);
	do {
		struct urb_req	*urbr;
		PIRP	irp;
		PLIST_ENTRY	le;
		KIRQL	oldirql2;

		KeAcquireSpinLockAtDpcLevel(&vpdo->lock_urbr);
		if (IsListEmpty(&vpdo->head_urbr)) {
			vpdo->urbr_sent_partial = NULL;
			vpdo->len_sent_partial = 0;
			InitializeListHead(&vpdo->head_urbr_sent);
			InitializeListHead(&vpdo->head_urbr_pending);

			KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
			break;
		}

		le = RemoveHeadList(&vpdo->head_urbr);
		urbr = CONTAINING_RECORD(le, struct urb_req, list_all);
		/* FIMXE event */
		irp = urbr->irp;

		if (count > 2) {
			LARGE_INTEGER	interval;

			KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
			DBGI(DBG_PNP, "sleep 50ms, let pnp manager send irp");
			interval.QuadPart = -500000;
			KeDelayExecutionThread(KernelMode, FALSE, &interval);
			KeRaiseIrql(DISPATCH_LEVEL, &oldirql);
		} else {
			KeReleaseSpinLock(&vpdo->lock_urbr, DISPATCH_LEVEL);
		}

		ExFreeToNPagedLookasideList(&g_lookaside, urbr);
		if (irp != NULL) {
			irp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
			IoSetCancelRoutine(irp, NULL);
			KeRaiseIrql(DISPATCH_LEVEL, &oldirql2);
			IoCompleteRequest(irp, IO_NO_INCREMENT);
			KeLowerIrql(oldirql2);
		}
		count++;
	} while (1);
}

PAGEABLE void
vhci_init_vpdo(pusbip_vpdo_dev_t vpdo)
{
	pusbip_vhub_dev_t	vhub;

	PAGED_CODE();

	DBGI(DBG_PNP, "vhci_init_vpdo: 0x%p\n", vpdo);

	vpdo->Present = TRUE; // attached to the bus
	vpdo->ReportedMissing = FALSE; // not yet reported missing

	INITIALIZE_PNP_STATE(vpdo);

	// vpdo usually starts its life at D3
	vpdo->common.DevicePowerState = PowerDeviceD3;
	vpdo->common.SystemPowerState = PowerSystemWorking;

	InitializeListHead(&vpdo->head_urbr);
	InitializeListHead(&vpdo->head_urbr_pending);
	InitializeListHead(&vpdo->head_urbr_sent);
	KeInitializeSpinLock(&vpdo->lock_urbr);

	DEVOBJ_FROM_VPDO(vpdo)->Flags |= DO_POWER_PAGABLE|DO_DIRECT_IO;

	vhub = vpdo->vhub;
	ExAcquireFastMutex(&vhub->Mutex);
	InsertTailList(&vhub->head_vpdo, &vpdo->Link);
	vhub->n_vpdos++;
	ExReleaseFastMutex(&vhub->Mutex);
	// This should be the last step in initialization.
	DEVOBJ_FROM_VPDO(vpdo)->Flags &= ~DO_DEVICE_INITIALIZING;
}

PAGEABLE NTSTATUS
vhci_get_ports_status(ioctl_usbip_vhci_get_ports_status *st, pusbip_vhub_dev_t vhub, ULONG *info)
{
	pusbip_vpdo_dev_t	vpdo;
	PLIST_ENTRY		entry;

	PAGED_CODE();

	DBGI(DBG_PNP, "get ports status\n");

	RtlZeroMemory(st, sizeof(*st));
	ExAcquireFastMutex(&vhub->Mutex);

	for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		vpdo = CONTAINING_RECORD (entry, usbip_vpdo_dev_t, Link);
		if (vpdo->port > 127 || vpdo->port == 0) {
			DBGE(DBG_PNP, "strange error");
		}
		if (st->u.max_used_port < (char)vpdo->port)
			st->u.max_used_port = (char)vpdo->port;
		st->u.port_status[vpdo->port] = 1;
	}
	ExReleaseFastMutex(&vhub->Mutex);
	*info = sizeof(*st);
	return STATUS_SUCCESS;
>>>>>>> 48e1018... Beautify VHCI driver code
}

PAGEABLE NTSTATUS
<<<<<<< HEAD
<<<<<<< HEAD
vhci_pnp(__in PDEVICE_OBJECT devobj, __in PIRP irp)
{
	pvdev_t		vdev = DEVOBJ_TO_VDEV(devobj);
	PIO_STACK_LOCATION	irpstack;
	NTSTATUS	status;
=======
vhci_unplug_dev(int addr, pusbip_vhub_dev_t vhub)
=======
vhci_unplug_dev(ULONG port, pusbip_vhub_dev_t vhub)
>>>>>>> 393ac6a... vhci, let a webcam with IAD be detected as COMPOSITE
{
	pusbip_vpdo_dev_t	vpdo;
	PLIST_ENTRY		entry;
	int	found = 0, all;
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo

	PAGED_CODE();

<<<<<<< HEAD
	irpstack = IoGetCurrentIrpStackLocation(irp);

<<<<<<< HEAD
	DBGI(DBG_GENERAL | DBG_PNP, "vhci_pnp(%s): Enter: minor:%s, irp: %p\n", dbg_vdev_type(DEVOBJ_VDEV_TYPE(devobj)), dbg_pnp_minor(irpstack->MinorFunction), irp);

	// If the device has been removed, the driver should
	// not pass the IRP down to the next lower driver.
	if (vdev->DevicePnPState == Deleted) {
		irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		goto END;
	}

	switch (irpstack->MinorFunction) {
	case IRP_MN_START_DEVICE:
		status = pnp_start_device(vdev, irp);
		break;
	case IRP_MN_QUERY_STOP_DEVICE:
		status = pnp_query_stop_device(vdev, irp);
		break;
	case IRP_MN_CANCEL_STOP_DEVICE:
		status = pnp_cancel_stop_device(vdev, irp);
		break;
	case IRP_MN_STOP_DEVICE:
		status = pnp_stop_device(vdev, irp);
		break;
	case IRP_MN_QUERY_REMOVE_DEVICE:
		status = pnp_query_remove_device(vdev, irp);
		break;
	case IRP_MN_CANCEL_REMOVE_DEVICE:
		status = pnp_cancel_remove_device(vdev, irp);
		break;
	case IRP_MN_REMOVE_DEVICE:
		status = pnp_remove_device(vdev, irp);
		break;
	case IRP_MN_SURPRISE_REMOVAL:
		status = pnp_surprise_removal(vdev, irp);
		break;
	case IRP_MN_QUERY_ID:
		status = pnp_query_id(vdev, irp, irpstack);
		break;
	case IRP_MN_QUERY_DEVICE_TEXT:
		status = pnp_query_device_text(vdev, irp, irpstack);
		break;
	case IRP_MN_QUERY_INTERFACE:
		status = pnp_query_interface(vdev, irp, irpstack);
		break;
	case IRP_MN_QUERY_DEVICE_RELATIONS:
		status = pnp_query_dev_relations(vdev, irp, irpstack);
		break;
	case IRP_MN_QUERY_CAPABILITIES:
		status = pnp_query_capabilities(vdev, irp, irpstack);
		break;
	case IRP_MN_QUERY_BUS_INFORMATION:
		status = pnp_query_bus_information(irp);
		break;
	case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
		status = pnp_query_resource_requirements(vdev, irp);
		break;
	case IRP_MN_QUERY_RESOURCES:
		status = pnp_query_resources(vdev, irp);
		break;
	case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
		status = pnp_filter_resource_requirements(vdev, irp);
		break;
	default:
		if (process_pnp_vpdo((pvpdo_dev_t)vdev, irp, irpstack))
			status = irp->IoStatus.Status;
		else
			status = irp_done(irp, irp->IoStatus.Status);
		break;
	}
=======
	if (port > 127)
		return STATUS_INVALID_PARAMETER;

	all = (0 == port);
>>>>>>> 393ac6a... vhci, let a webcam with IAD be detected as COMPOSITE

<<<<<<< HEAD
END:
	DBGI(DBG_GENERAL | DBG_PNP, "vhci_pnp(%s): Leave: irp:%p, status:%s\n", dbg_vdev_type(DEVOBJ_VDEV_TYPE(devobj)), irp, dbg_ntstatus(status));
=======
	ExAcquireFastMutex(&vhub->Mutex);

	if (all) {
		DBGI(DBG_PNP, "Plugging out all the devices!\n");
	} else {
		DBGI(DBG_PNP, "Plugging out single device: port: %u\n", port);
	}

	if (vhub->n_vpdos == 0) {
		// We got a 2nd plugout...somebody in user space isn't playing nice!!!
		DBGW(DBG_PNP, "BAD BAD BAD...2 removes!!! Send only one!\n");
		ExReleaseFastMutex(&vhub->Mutex);
		return STATUS_NO_SUCH_DEVICE;
	}

	for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		vpdo = CONTAINING_RECORD(entry, usbip_vpdo_dev_t, Link);

		DBGI(DBG_PNP, "found device: port: %d\n", vpdo->port);

		if (all || port == vpdo->port) {
			DBGI(DBG_PNP, "Plugging out: port: %u\n", vpdo->port);
			vpdo->Present = FALSE;
			complete_pending_read_irp(vpdo);
			found = 1;
			if (!all) {
				break;
			}
		}
	}

	ExReleaseFastMutex(&vhub->Mutex);

	if (found) {
		IoInvalidateDeviceRelations(vhub->UnderlyingPDO, BusRelations);

		ExAcquireFastMutex(&vhub->Mutex);

		for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
			vpdo = CONTAINING_RECORD(entry, usbip_vpdo_dev_t, Link);

			if (!vpdo->Present) {
				complete_pending_irp(vpdo);
				SET_NEW_PNP_STATE(vpdo, PNP_DEVICE_REMOVED);
				IoInvalidateDeviceState(vpdo->common.Self);
			}
		}
		ExReleaseFastMutex(&vhub->Mutex);

		DBGI(DBG_PNP, "Device %u plug out finished\n", port);
		return  STATUS_SUCCESS;
	}
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo

	return status;
=======
	return STATUS_INVALID_PARAMETER;
}

PAGEABLE NTSTATUS
vhci_eject_device(PUSBIP_VHCI_EJECT_HARDWARE Eject, pusbip_vhub_dev_t vhub)
{
	pusbip_vpdo_dev_t	vpdo;
	PLIST_ENTRY		entry;
	BOOLEAN			found = FALSE, ejectAll;

	PAGED_CODE ();

	ejectAll = (0 == Eject->port);

	ExAcquireFastMutex(&vhub->Mutex);

	if (ejectAll) {
		DBGI(DBG_PNP, "Ejecting all the vpdo's!\n");
	} else {
		DBGI(DBG_PNP, "Ejecting: port: %u\n", Eject->port);
	}

	if (vhub->n_vpdos == 0) {
		// Somebody in user space isn't playing nice!!!
		DBGW(DBG_PNP, "No devices to eject!\n");
		ExReleaseFastMutex(&vhub->Mutex);
		return STATUS_NO_SUCH_DEVICE;
	}

	// Scan the list to find matching vpdo's
	for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		vpdo = CONTAINING_RECORD(entry, usbip_vpdo_dev_t, Link);

		DBGI(DBG_PNP, "found device: %u\n", vpdo->port);

		if (ejectAll || Eject->port == vpdo->port) {
			DBGI(DBG_PNP, "Ejected: %u\n", vpdo->port);
			found = TRUE;
			IoRequestDeviceEject(vpdo->common.Self);
			if (!ejectAll) {
				break;
			}
		}
	}
	ExReleaseFastMutex(&vhub->Mutex);

	if (found) {
		return STATUS_SUCCESS;
	}

	DBGW(DBG_PNP, "Device %u is not present\n", Eject->port);

	return STATUS_INVALID_PARAMETER;
>>>>>>> 48e1018... Beautify VHCI driver code
}