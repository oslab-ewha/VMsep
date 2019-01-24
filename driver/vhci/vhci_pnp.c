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
Bus_PDO_PnP(__in PDEVICE_OBJECT DeviceObject, __in PIRP Irp,
	__in PIO_STACK_LOCATION IrpStack, __in PPDO_DEVICE_DATA DeviceData);

extern PAGEABLE NTSTATUS
Bus_WmiRegistration(__in PFDO_DEVICE_DATA FdoData);

extern PAGEABLE NTSTATUS
Bus_WmiDeRegistration(__in PFDO_DEVICE_DATA FdoData);

PAGEABLE NTSTATUS
Bus_AddDevice(__in PDRIVER_OBJECT DriverObject, __in PDEVICE_OBJECT PhysicalDeviceObject)
{
	PDEVICE_OBJECT      deviceObject = NULL;
	PFDO_DEVICE_DATA    deviceData = NULL;
	PWCHAR              deviceName = NULL;
	ULONG		nameLength;
	NTSTATUS	status;

	PAGED_CODE ();

	DBGI(DBG_GENERAL | DBG_PNP, "Add Device: 0x%p\n", PhysicalDeviceObject);

	status = IoCreateDevice (
				DriverObject,               // our driver object
				sizeof (FDO_DEVICE_DATA),   // device object extension size
				NULL,                       // FDOs do not have names
				FILE_DEVICE_BUS_EXTENDER,   // We are a bus
				FILE_DEVICE_SECURE_OPEN,    //
				TRUE,                       // our FDO is exclusive
				&deviceObject);             // The device object created

	if (!NT_SUCCESS(status)) {
		goto End;
	}

	deviceData = (PFDO_DEVICE_DATA)deviceObject->DeviceExtension;
	RtlZeroMemory (deviceData, sizeof(FDO_DEVICE_DATA));

	// Set the initial state of the FDO
	INITIALIZE_PNP_STATE(deviceData);

	deviceData->common.IsFDO = TRUE;
	deviceData->common.Self = deviceObject;

	ExInitializeFastMutex (&deviceData->Mutex);

	InitializeListHead (&deviceData->ListOfPDOs);

	// Set the PDO for use with PlugPlay functions
	deviceData->UnderlyingPDO = PhysicalDeviceObject;

	// Set the initial powerstate of the FDO
	deviceData->common.DevicePowerState = PowerDeviceUnspecified;
	deviceData->common.SystemPowerState = PowerSystemWorking;

	// Biased to 1. Transition to zero during remove device
	// means IO is finished. Transition to 1 means the device
	// can be stopped.
	deviceData->OutstandingIO = 1;

	// Initialize the remove event to Not-Signaled.  This event
	// will be set when the OutstandingIO will become 0.
	KeInitializeEvent(&deviceData->RemoveEvent, SynchronizationEvent, FALSE);

	// Initialize the stop event to Signaled:
	// there are no Irps that prevent the device from being
	// stopped. This event will be set when the OutstandingIO
	// will become 0.
	//
	KeInitializeEvent(&deviceData->StopEvent, SynchronizationEvent, TRUE);

	deviceObject->Flags |= DO_POWER_PAGABLE|DO_BUFFERED_IO;

	// Tell the Plug & Play system that this device will need a
	// device interface.
	status = IoRegisterDeviceInterface(PhysicalDeviceObject,
		(LPGUID) &GUID_DEVINTERFACE_VHCI_USBIP, NULL, &deviceData->InterfaceName);

	if (!NT_SUCCESS (status)) {
		DBGE(DBG_PNP, "AddDevice: IoRegisterDeviceInterface failed (%x)", status);
		goto End;
	}

	// Attach our FDO to the device stack.
	// The return value of IoAttachDeviceToDeviceStack is the top of the
	// attachment chain.  This is where all the IRPs should be routed.
	deviceData->NextLowerDriver = IoAttachDeviceToDeviceStack(deviceObject, PhysicalDeviceObject);

	if (NULL == deviceData->NextLowerDriver) {
		status = STATUS_NO_SUCH_DEVICE;
		goto End;
	}
#if DBG
	// We will demonstrate here the step to retrieve the name of the PDO
	status = IoGetDeviceProperty(PhysicalDeviceObject, DevicePropertyPhysicalDeviceObjectName,
			0, NULL, &nameLength);

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

	status = IoGetDeviceProperty(PhysicalDeviceObject, DevicePropertyPhysicalDeviceObjectName,
				nameLength, deviceName, &nameLength);

	if (!NT_SUCCESS (status)) {
		DBGE(DBG_PNP, "AddDevice:IoGDP(2) failed (0x%x)", status);
		goto End;
	}

	DBGI(DBG_PNP, "AddDevice: %p to %p->%p (%ws) \n", deviceObject, deviceData->NextLowerDriver, PhysicalDeviceObject, deviceName);
#endif

	// We are done with initializing, so let's indicate that and return.
	// This should be the final step in the AddDevice process.
	//
	deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

End:
	if (deviceName) {
		ExFreePool(deviceName);
	}

	if (!NT_SUCCESS(status) && deviceObject) {
		if (deviceData && deviceData->NextLowerDriver) {
			IoDetachDevice(deviceData->NextLowerDriver);
		}
		IoDeleteDevice (deviceObject);
	}

	return status;
}

static PAGEABLE void
Bus_RemoveFdo(__in PFDO_DEVICE_DATA FdoData)
{
	PAGED_CODE();

	// Stop all access to the device, fail any outstanding I/O to the device,
	// and free all the resources associated with the device.

	// Disable the device interface and free the buffer
	if (FdoData->InterfaceName.Buffer != NULL) {
		IoSetDeviceInterfaceState(&FdoData->InterfaceName, FALSE);

		ExFreePool(FdoData->InterfaceName.Buffer);
		RtlZeroMemory(&FdoData->InterfaceName, sizeof(UNICODE_STRING));
	}

	// Inform WMI to remove this DeviceObject from its
	// list of providers.
	Bus_WmiDeRegistration(FdoData);
}

PAGEABLE NTSTATUS
Bus_DestroyPdo(PDEVICE_OBJECT Device, __in PPDO_DEVICE_DATA PdoData)
{
	PAGED_CODE();

	// VHCI does not queue any irps at this time so we have nothing to do.
	//
	// Free any resources.

	//FIXME
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
{
	POWER_STATE	powerState;
	NTSTATUS	status;

	PAGED_CODE();

	UNREFERENCED_PARAMETER(Irp);

	// Check the function driver source to learn
	// about parsing resource list.

	// Enable device interface. If the return status is
	// STATUS_OBJECT_NAME_EXISTS means we are enabling the interface
	// that was already enabled, which could happen if the device
	// is stopped and restarted for resource rebalancing.
	status = IoSetDeviceInterfaceState(&FdoData->InterfaceName, TRUE);
	if (!NT_SUCCESS(status)) {
		DBGE(DBG_PNP, "IoSetDeviceInterfaceState failed: 0x%x\n", status);
		return status;
	}

	// Set the device power state to fully on. Also if this Start
	// is due to resource rebalance, you should restore the device
	// to the state it was before you stopped the device and relinquished
	// resources.

	FdoData->common.DevicePowerState = PowerDeviceD0;
	powerState.DeviceState = PowerDeviceD0;
	PoSetPowerState(FdoData->common.Self, DevicePowerState, powerState);

	SET_NEW_PNP_STATE(FdoData, Started);

	// Register with WMI
	status = Bus_WmiRegistration(FdoData);
	if (!NT_SUCCESS(status)) {
		DBGE(DBG_PNP, "StartFdo: Bus_WmiRegistration failed (%x)\n", status);
	}

	return status;
}

static NTSTATUS
Bus_CompletionRoutine(__in PDEVICE_OBJECT DeviceObject, __in PIRP Irp, __in PVOID Context)
{
	UNREFERENCED_PARAMETER(DeviceObject);

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
pnp_cancel_remove_device(pvdev_t vdev, PIRP irp)
{
	if (vdev->DevicePnPState == RemovePending) {
		RESTORE_PREVIOUS_PNP_STATE(vdev);
=======
Bus_SendIrpSynchronously(__in PDEVICE_OBJECT DeviceObject, __in PIRP Irp)
{
	KEVENT		event;
	NTSTATUS	status;

	PAGED_CODE();

	KeInitializeEvent(&event, NotificationEvent, FALSE);

	IoCopyCurrentIrpStackLocationToNext(Irp);

	IoSetCompletionRoutine(Irp, Bus_CompletionRoutine, &event, TRUE, TRUE, TRUE);

	status = IoCallDriver(DeviceObject, Irp);

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
pnp_surprise_removal(pvdev_t vdev, PIRP irp)
{
	SET_NEW_PNP_STATE(vdev, SurpriseRemovePending);
	IRP_PASS_DOWN_OR_SUCCESS(vdev, irp);
=======
Bus_FDO_PnP(__in PDEVICE_OBJECT DeviceObject, __in PIRP Irp, __in PIO_STACK_LOCATION IrpStack, __in PFDO_DEVICE_DATA DeviceData)
{
	PPDO_DEVICE_DATA	pdoData;
	PDEVICE_RELATIONS	relations, oldRelations;
	ULONG			length, prevcount, numPdosPresent;
	PLIST_ENTRY		entry, listHead, nextEntry;
	NTSTATUS		status;

	PAGED_CODE ();

	Bus_IncIoCount(DeviceData);

	switch (IrpStack->MinorFunction) {
	case IRP_MN_START_DEVICE:
		// Send the Irp down and wait for it to come back.
		// Do not touch the hardware until then.
		//
		status = Bus_SendIrpSynchronously (DeviceData->NextLowerDriver, Irp);
		if (NT_SUCCESS(status)) {
			// Initialize your device with the resources provided
			// by the PnP manager to your device.
			status = Bus_StartFdo (DeviceData, Irp);
		}

		// We must now complete the IRP, since we stopped it in the
		// completion routine with MORE_PROCESSING_REQUIRED.
		Irp->IoStatus.Status = status;
		IoCompleteRequest (Irp, IO_NO_INCREMENT);
		Bus_DecIoCount (DeviceData);
		return status;
	case IRP_MN_QUERY_STOP_DEVICE:
		// The PnP manager is trying to stop the device
		// for resource rebalancing. Fail this now if you
		// cannot stop the device in response to STOP_DEVICE.

		SET_NEW_PNP_STATE(DeviceData, StopPending);
		Irp->IoStatus.Status = STATUS_SUCCESS; // You must not fail the IRP.
		break;
	case IRP_MN_CANCEL_STOP_DEVICE:
		// The PnP Manager sends this IRP, at some point after an
		// IRP_MN_QUERY_STOP_DEVICE, to inform the drivers for a
		// device that the device will not be stopped for
		// resource reconfiguration.
		//
		// First check to see whether you have received cancel-stop
		// without first receiving a query-stop. This could happen if
		//  someone above us fails a query-stop and passes down the subsequent
		// cancel-stop.
		if (StopPending == DeviceData->common.DevicePnPState) {
			// We did receive a query-stop, so restore.
			RESTORE_PREVIOUS_PNP_STATE(DeviceData);
			ASSERT(DeviceData->common.DevicePnPState == Started);
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
		
		Bus_DecIoCount(DeviceData);

		KeWaitForSingleObject(&DeviceData->StopEvent,
				Executive, // Waiting reason of a driver
				KernelMode, // Waiting in kernel mode
				FALSE, // No allert
				NULL); // No timeout

		// Increment the counter back because this IRP has to
		// be sent down to the lower stack.
		Bus_IncIoCount (DeviceData);

		// Free resources given by start device.
		SET_NEW_PNP_STATE(DeviceData, Stopped);

		// We don't need a completion routine so fire and forget.
		//
		// Set the current stack location to the next stack location and
		// call the next device object.

		Irp->IoStatus.Status = STATUS_SUCCESS;
		break;
	case IRP_MN_QUERY_REMOVE_DEVICE:
		// If we were to fail this call then we would need to complete the
		// IRP here.  Since we are not, set the status to SUCCESS and
		// call the next driver.

		SET_NEW_PNP_STATE(DeviceData, RemovePending);

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

		if (RemovePending == DeviceData->common.DevicePnPState) {
			// We did receive a query-remove, so restore.
			RESTORE_PREVIOUS_PNP_STATE(DeviceData);
		}
		Irp->IoStatus.Status = STATUS_SUCCESS;// You must not fail the IRP.
		break;
	case IRP_MN_SURPRISE_REMOVAL:
		// The device has been unexpectedly removed from the machine
		// and is no longer available for I/O. Bus_RemoveFdo clears
		// all the resources, frees the interface and de-registers
		// with WMI, but it doesn't delete the FDO. That's done
		// later in Remove device query.

		SET_NEW_PNP_STATE(DeviceData, SurpriseRemovePending);
		Bus_RemoveFdo(DeviceData);

		ExAcquireFastMutex (&DeviceData->Mutex);

		listHead = &DeviceData->ListOfPDOs;

		for (entry = listHead->Flink,nextEntry = entry->Flink; entry != listHead; entry = nextEntry,nextEntry = entry->Flink) {
			pdoData = CONTAINING_RECORD (entry, PDO_DEVICE_DATA, Link);
			RemoveEntryList (&pdoData->Link);
			InitializeListHead (&pdoData->Link);
			pdoData->ParentFdo  = NULL;
			pdoData->ReportedMissing = TRUE;
		}

		ExReleaseFastMutex (&DeviceData->Mutex);

		Irp->IoStatus.Status = STATUS_SUCCESS; // You must not fail the IRP.
		break;
	case IRP_MN_REMOVE_DEVICE:
		// The Plug & Play system has dictated the removal of this device.
		// We have no choice but to detach and delete the device object.

		// Check the state flag to see whether you are surprise removed
		if (DeviceData->common.DevicePnPState != SurpriseRemovePending) {
			Bus_RemoveFdo(DeviceData);
		}

		SET_NEW_PNP_STATE(DeviceData, Deleted);

		// Wait for all outstanding requests to complete.
		// We need two decrements here, one for the increment in
		// the beginning of this function, the other for the 1-biased value of
		// OutstandingIO.

		Bus_DecIoCount(DeviceData);

		// The requestCount is at least one here (is 1-biased)

		Bus_DecIoCount(DeviceData);

		KeWaitForSingleObject(&DeviceData->RemoveEvent, Executive, KernelMode, FALSE, NULL);

		// Typically the system removes all the  children before
		// removing the parent FDO. If for any reason child Pdos are
		// still present we will destroy them explicitly, with one exception -
		// we will not delete the PDOs that are in SurpriseRemovePending state.

		ExAcquireFastMutex (&DeviceData->Mutex);

		listHead = &DeviceData->ListOfPDOs;

		for (entry = listHead->Flink,nextEntry = entry->Flink; entry != listHead; entry = nextEntry,nextEntry = entry->Flink) {
			pdoData = CONTAINING_RECORD (entry, PDO_DEVICE_DATA, Link);
			RemoveEntryList (&pdoData->Link);
			if (SurpriseRemovePending == pdoData->common.DevicePnPState) {
				// We will reinitialize the list head so that we
				// wouldn't barf when we try to delink this PDO from
				// the parent's PDOs list, when the system finally
				// removes the PDO. Let's also not forget to set the
				// ReportedMissing flag to cause the deletion of the PDO.
				DBGI(DBG_PNP, "\tFound a surprise removed device: 0x%p\n", pdoData->common.Self);
				InitializeListHead (&pdoData->Link);
				pdoData->ParentFdo  = NULL;
				pdoData->ReportedMissing = TRUE;
				continue;
			}
			DeviceData->NumPDOs--;
			Bus_DestroyPdo (pdoData->common.Self, pdoData);
		}

		ExReleaseFastMutex (&DeviceData->Mutex);

		// We need to send the remove down the stack before we detach,
		// but we don't need to wait for the completion of this operation
		// (and to register a completion routine).
		Irp->IoStatus.Status = STATUS_SUCCESS;
		IoSkipCurrentIrpStackLocation (Irp);
		status = IoCallDriver (DeviceData->NextLowerDriver, Irp);

		// Detach from the underlying devices.
		IoDetachDevice (DeviceData->NextLowerDriver);

		DBGI(DBG_PNP, "\tDeleting FDO: 0x%p\n", DeviceObject);

		IoDeleteDevice (DeviceObject);

		return status;
	case IRP_MN_QUERY_DEVICE_RELATIONS:
		DBGI(DBG_PNP, "\tQueryDeviceRelation Type: %s\n", dbg_dev_relation(IrpStack->Parameters.QueryDeviceRelations.Type));

		if (BusRelations != IrpStack->Parameters.QueryDeviceRelations.Type) {
			// We don't support any other Device Relations
			break;
		}

		// Tell the plug and play system about all the PDOs.
		//
		// There might also be device relations below and above this FDO,
		// so, be sure to propagate the relations from the upper drivers.
		//
		// No Completion routine is needed so long as the status is preset
		// to success.  (PDOs complete plug and play irps with the current
		// IoStatus.Status and IoStatus.Information as the default.)

		ExAcquireFastMutex (&DeviceData->Mutex);

		oldRelations = (PDEVICE_RELATIONS) Irp->IoStatus.Information;
		if (oldRelations) {
			prevcount = oldRelations->Count;
			if (!DeviceData->NumPDOs) {
				// There is a device relations struct already present and we have
				// nothing to add to it, so just call IoSkip and IoCall
				ExReleaseFastMutex (&DeviceData->Mutex);
				break;
			}
		}
		else  {
			prevcount = 0;
		}

		// Calculate the number of PDOs actually present on the bus
		numPdosPresent = 0;
		for (entry = DeviceData->ListOfPDOs.Flink; entry != &DeviceData->ListOfPDOs; entry = entry->Flink) {
			pdoData = CONTAINING_RECORD (entry, PDO_DEVICE_DATA, Link);
			if (pdoData->Present)
				numPdosPresent++;
		}

		// Need to allocate a new relations structure and add our
		// PDOs to it.
		length = sizeof(DEVICE_RELATIONS) + ((numPdosPresent + prevcount) * sizeof (PDEVICE_OBJECT)) -1;

		relations = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(PagedPool, length, USBIP_VHCI_POOL_TAG);

		if (relations == NULL) {
			// Fail the IRP
			ExReleaseFastMutex(&DeviceData->Mutex);
			Irp->IoStatus.Status = status = STATUS_INSUFFICIENT_RESOURCES;
			IoCompleteRequest(Irp, IO_NO_INCREMENT);
			Bus_DecIoCount(DeviceData);
			return status;
		}

		// Copy in the device objects so far
		if (prevcount) {
			RtlCopyMemory(relations->Objects, oldRelations->Objects, prevcount * sizeof (PDEVICE_OBJECT));
		}

		relations->Count = prevcount + numPdosPresent;

		// For each PDO present on this bus add a pointer to the device relations
		// buffer, being sure to take out a reference to that object.
		// The Plug & Play system will dereference the object when it is done
		// with it and free the device relations buffer.
		for (entry = DeviceData->ListOfPDOs.Flink; entry != &DeviceData->ListOfPDOs; entry = entry->Flink) {
			pdoData = CONTAINING_RECORD (entry, PDO_DEVICE_DATA, Link);
			if (pdoData->Present) {
				relations->Objects[prevcount] = pdoData->common.Self;
				ObReferenceObject(pdoData->common.Self);
				prevcount++;
			} else {
				pdoData->ReportedMissing = TRUE;
			}
		}

		DBGI(DBG_PNP, "# of PDOS: present: %d, reported: %d\n", DeviceData->NumPDOs, relations->Count);

		// Replace the relations structure in the IRP with the new
		// one.
		if (oldRelations) {
			ExFreePool(oldRelations);
		}
		Irp->IoStatus.Information = (ULONG_PTR) relations;

		ExReleaseFastMutex(&DeviceData->Mutex);

		// Set up and pass the IRP further down the stack
		Irp->IoStatus.Status = STATUS_SUCCESS;
		break;
	default:
		// In the default case we merely call the next driver.
		// We must not modify Irp->IoStatus.Status or complete the IRP.
		break;
	}

	IoSkipCurrentIrpStackLocation(Irp);
	status = IoCallDriver(DeviceData->NextLowerDriver, Irp);
	Bus_DecIoCount(DeviceData);

	return status;
>>>>>>> 48e1018... Beautify VHCI driver code
}

static PAGEABLE NTSTATUS
pnp_query_bus_information(PIRP irp)
{
<<<<<<< HEAD
	PPNP_BUS_INFORMATION busInfo;

	PAGED_CODE();

	busInfo = ExAllocatePoolWithTag(PagedPool, sizeof(PNP_BUS_INFORMATION), USBIP_VHCI_POOL_TAG);
=======
	PCOMMON_DEVICE_DATA	commonData;
	PIO_STACK_LOCATION	irpStack;
	NTSTATUS	status;

	PAGED_CODE();

	DBGI(DBG_GENERAL | DBG_PNP, "Bus_PnP: Enter\n");

	irpStack = IoGetCurrentIrpStackLocation(Irp);
	ASSERT(IRP_MJ_PNP == irpStack->MajorFunction);

	commonData = (PCOMMON_DEVICE_DATA)DeviceObject->DeviceExtension;

	// If the device has been removed, the driver should
	// not pass the IRP down to the next lower driver.
	if (commonData->DevicePnPState == Deleted) {
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	if (commonData->IsFDO) {
		DBGI(DBG_PNP, "FDO: minor: %s, IRP:0x%p\n", dbg_pnp_minor(irpStack->MinorFunction), Irp);

		// Request is for the bus FDO
		status = Bus_FDO_PnP(DeviceObject, Irp, irpStack, (PFDO_DEVICE_DATA)commonData);
	}
	else {
		DBGI(DBG_PNP, "PDO: minor: %s, IRP: 0x%p\n", dbg_pnp_minor(irpStack->MinorFunction), Irp);

		// Request is for the child PDO.
		status = Bus_PDO_PnP(DeviceObject, Irp, irpStack, (PPDO_DEVICE_DATA)commonData);
	}

	DBGI(DBG_GENERAL | DBG_PNP, "Bus_PnP: Leave: %s\n", dbg_ntstatus(status));

	return status;
}

static void
complete_pending_read_irp(PPDO_DEVICE_DATA pdodata)
{
	KIRQL	oldirql;
	PIRP	irp;
>>>>>>> 48e1018... Beautify VHCI driver code

	if (busInfo == NULL) {
		return STATUS_INSUFFICIENT_RESOURCES;
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
complete_pending_irp(PPDO_DEVICE_DATA pdodata)
{
	int	count=0;
	KIRQL oldirql;

	//FIXME
	DBGI(DBG_PNP, "finish pending irp\n");
	KeRaiseIrql(DISPATCH_LEVEL, &oldirql);
	do {
		struct urb_req	*urbr;
		PIRP	irp;
		PLIST_ENTRY	le;
		KIRQL	oldirql2;

		KeAcquireSpinLockAtDpcLevel(&pdodata->lock_urbr);
		if (IsListEmpty(&pdodata->head_urbr)) {
			pdodata->urbr_sent_partial = NULL;
			pdodata->len_sent_partial = 0;
			InitializeListHead(&pdodata->head_urbr_sent);
			InitializeListHead(&pdodata->head_urbr_pending);

			KeReleaseSpinLock(&pdodata->lock_urbr, oldirql);
			break;
		}

		le = RemoveHeadList(&pdodata->head_urbr);
		urbr = CONTAINING_RECORD(le, struct urb_req, list_all);
		/* FIMXE event */
		irp = urbr->irp;

		if (count > 2) {
			LARGE_INTEGER	interval;

			KeReleaseSpinLock(&pdodata->lock_urbr, oldirql);
			DBGI(DBG_PNP, "sleep 50ms, let pnp manager send irp");
			interval.QuadPart = -500000;
			KeDelayExecutionThread(KernelMode, FALSE, &interval);
			KeRaiseIrql(DISPATCH_LEVEL, &oldirql);
		} else {
			KeReleaseSpinLock(&pdodata->lock_urbr, DISPATCH_LEVEL);
		}

		ExFreeToNPagedLookasideList(&g_lookaside, urbr);
		irp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
		IoSetCancelRoutine(irp, NULL);
		KeRaiseIrql(DISPATCH_LEVEL, &oldirql2);
		IoCompleteRequest (irp, IO_NO_INCREMENT);
		KeLowerIrql(oldirql2);
		count++;
	} while (1);
}

PAGEABLE void
bus_init_pdo(__out PDEVICE_OBJECT pdo, PFDO_DEVICE_DATA fdodata)
{
	PPDO_DEVICE_DATA	pdodata;

	PAGED_CODE ();

	pdodata = (PPDO_DEVICE_DATA)  pdo->DeviceExtension;

	DBGI(DBG_PNP, "pdo 0x%p, extension 0x%p\n", pdo, pdodata);

	// Initialize the rest
	pdodata->common.IsFDO = FALSE;
	pdodata->common.Self = pdo;
 
	pdodata->ParentFdo = fdodata->common.Self;

	pdodata->Present = TRUE; // attached to the bus
	pdodata->ReportedMissing = FALSE; // not yet reported missing

	INITIALIZE_PNP_STATE(pdodata);

	// PDO's usually start their life at D3
	pdodata->common.DevicePowerState = PowerDeviceD3;
	pdodata->common.SystemPowerState = PowerSystemWorking;

	InitializeListHead(&pdodata->head_urbr);
	InitializeListHead(&pdodata->head_urbr_pending);
	InitializeListHead(&pdodata->head_urbr_sent);
	KeInitializeSpinLock(&pdodata->lock_urbr);

	pdo->Flags |= DO_POWER_PAGABLE|DO_DIRECT_IO;

	ExAcquireFastMutex (&fdodata->Mutex);
	InsertTailList(&fdodata->ListOfPDOs, &pdodata->Link);
	fdodata->NumPDOs++;
	ExReleaseFastMutex (&fdodata->Mutex);
	// This should be the last step in initialization.
	pdo->Flags &= ~DO_DEVICE_INITIALIZING;
}

PAGEABLE NTSTATUS
bus_get_ports_status(ioctl_usbip_vhci_get_ports_status *st, PFDO_DEVICE_DATA  fdodata, ULONG *info)
{
	PPDO_DEVICE_DATA	pdodata;
	PLIST_ENTRY		entry;

	PAGED_CODE();

	DBGI(DBG_PNP, "get ports status\n");

	RtlZeroMemory(st, sizeof(*st));
	ExAcquireFastMutex (&fdodata->Mutex);

	for (entry = fdodata->ListOfPDOs.Flink; entry != &fdodata->ListOfPDOs; entry = entry->Flink) {
		pdodata = CONTAINING_RECORD (entry, PDO_DEVICE_DATA, Link);
		if (pdodata->SerialNo > 127 || pdodata->SerialNo == 0){
			DBGE(DBG_PNP, "strange error");
		}
		if (st->u.max_used_port < (char)pdodata->SerialNo)
			st->u.max_used_port = (char)pdodata->SerialNo;
		st->u.port_status[pdodata->SerialNo] = 1;
	}
	ExReleaseFastMutex(&fdodata->Mutex);
	*info = sizeof(*st);
	return STATUS_SUCCESS;
>>>>>>> 48e1018... Beautify VHCI driver code
}

PAGEABLE NTSTATUS
vhci_pnp(__in PDEVICE_OBJECT devobj, __in PIRP irp)
{
	pvdev_t		vdev = DEVOBJ_TO_VDEV(devobj);
	PIO_STACK_LOCATION	irpstack;
	NTSTATUS	status;

	PAGED_CODE();

	irpstack = IoGetCurrentIrpStackLocation(irp);

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

<<<<<<< HEAD
END:
	DBGI(DBG_GENERAL | DBG_PNP, "vhci_pnp(%s): Leave: irp:%p, status:%s\n", dbg_vdev_type(DEVOBJ_VDEV_TYPE(devobj)), irp, dbg_ntstatus(status));

	return status;
=======
	return STATUS_INVALID_PARAMETER;
}

PAGEABLE NTSTATUS
Bus_EjectDevice(PUSBIP_VHCI_EJECT_HARDWARE Eject, PFDO_DEVICE_DATA FdoData)
{
	PPDO_DEVICE_DATA	pdoData;
	PLIST_ENTRY		entry;
	BOOLEAN			found = FALSE, ejectAll;

	PAGED_CODE ();

	ejectAll = (0 == Eject->SerialNo);

	ExAcquireFastMutex (&FdoData->Mutex);

	if (ejectAll) {
		DBGI(DBG_PNP, "Ejecting all the pdos!\n");
	} else {
		DBGI(DBG_PNP, "Ejecting %d\n", Eject->SerialNo);
	}

	if (FdoData->NumPDOs == 0) {
		// Somebody in user space isn't playing nice!!!
		DBGW(DBG_PNP, "No devices to eject!\n");
		ExReleaseFastMutex (&FdoData->Mutex);
		return STATUS_NO_SUCH_DEVICE;
	}

	// Scan the list to find matching PDOs
	for (entry = FdoData->ListOfPDOs.Flink; entry != &FdoData->ListOfPDOs; entry = entry->Flink) {
		pdoData = CONTAINING_RECORD (entry, PDO_DEVICE_DATA, Link);

		DBGI(DBG_PNP, "found device %d\n", pdoData->SerialNo);

		if (ejectAll || Eject->SerialNo == pdoData->SerialNo) {
			DBGI(DBG_PNP, "Ejected %d\n", pdoData->SerialNo);
			found = TRUE;
			IoRequestDeviceEject(pdoData->common.Self);
			if (!ejectAll) {
				break;
			}
		}
	}
	ExReleaseFastMutex (&FdoData->Mutex);

	if (found) {
		return STATUS_SUCCESS;
	}

	DBGW(DBG_PNP, "Device %d is not present\n", Eject->SerialNo);

	return STATUS_INVALID_PARAMETER;
>>>>>>> 48e1018... Beautify VHCI driver code
}