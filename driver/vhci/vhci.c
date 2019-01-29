#include "vhci.h"

#include <usbdi.h>

#include "globals.h"
#include "usbreq.h"
#include "vhci_pnp.h"

//
// Global Debug Level
//

GLOBALS Globals;

NPAGED_LOOKASIDE_LIST g_lookaside;

PAGEABLE __drv_dispatchType(IRP_MJ_READ)
DRIVER_DISPATCH vhci_read;

PAGEABLE __drv_dispatchType(IRP_MJ_WRITE)
DRIVER_DISPATCH vhci_write;

PAGEABLE __drv_dispatchType(IRP_MJ_DEVICE_CONTROL)
DRIVER_DISPATCH vhci_ioctl;

PAGEABLE __drv_dispatchType(IRP_MJ_INTERNAL_DEVICE_CONTROL)
DRIVER_DISPATCH vhci_internal_ioctl;

PAGEABLE __drv_dispatchType(IRP_MJ_PNP)
DRIVER_DISPATCH vhci_pnp;

__drv_dispatchType(IRP_MJ_POWER)
DRIVER_DISPATCH vhci_power;

PAGEABLE __drv_dispatchType(IRP_MJ_SYSTEM_CONTROL)
DRIVER_DISPATCH vhci_system_control;

PAGEABLE DRIVER_ADD_DEVICE vhci_add_device;

static PAGEABLE VOID
vhci_driverUnload(__in PDRIVER_OBJECT drvobj)
{
	UNREFERENCED_PARAMETER(drvobj);

	PAGED_CODE();

	DBGI(DBG_GENERAL, "Unload\n");

	ExDeleteNPagedLookasideList(&g_lookaside);

	//
	// All the device objects should be gone.
	//

	ASSERT(NULL == drvobj->DeviceObject);

	//
	// Here we free all the resources allocated in the DriverEntry
	//

	if (Globals.RegistryPath.Buffer)
		ExFreePool(Globals.RegistryPath.Buffer);
}

static PAGEABLE NTSTATUS
vhci_create(__in PDEVICE_OBJECT devobj, __in PIRP Irp)
{
<<<<<<< HEAD
	pvdev_t	vdev = DEVOBJ_TO_VDEV(devobj);

	PAGED_CODE();

	DBGI(DBG_GENERAL, "vhci_create(%s): Enter\n", dbg_vdev_type(vdev->type));

	// Check to see whether the bus is removed
	if (vdev->DevicePnPState == Deleted) {
		DBGW(DBG_GENERAL, "vhci_create(%s): no such device\n", dbg_vdev_type(vdev->type));

=======
	pusbip_vhub_dev_t	vhub;
	pdev_common_t		devcom;

	PAGED_CODE();

	devcom = (pdev_common_t)devobj->DeviceExtension;

	if (!devcom->is_vhub) {
		Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	vhub = (pusbip_vhub_dev_t)devobj->DeviceExtension;

	inc_io_vhub(vhub);

	// Check to see whether the bus is removed
	if (vhub->common.DevicePnPState == Deleted) {
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
		Irp->IoStatus.Status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_NO_SUCH_DEVICE;
	}

	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
<<<<<<< HEAD

	DBGI(DBG_GENERAL, "vhci_create(%s): Leave\n", dbg_vdev_type(vdev->type));

	return STATUS_SUCCESS;
}

static PAGEABLE void
cleanup_vpdo(pvhci_dev_t vhci, PIRP irp)
{
	PIO_STACK_LOCATION  irpstack;
	pvpdo_dev_t	vpdo;

	irpstack = IoGetCurrentIrpStackLocation(irp);
	vpdo = irpstack->FileObject->FsContext;
	if (vpdo) {
		vpdo->fo = NULL;
		irpstack->FileObject->FsContext = NULL;
		if (vpdo->plugged)
			vhci_unplug_port(vhci, vpdo->port);
=======
	dec_io_vhub(vhub);
	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS
vhci_cleanup(__in PDEVICE_OBJECT devobj, __in PIRP irp)
{
	PIO_STACK_LOCATION  irpstack;
	NTSTATUS            status;
	pusbip_vhub_dev_t	vhub;
	pusbip_vpdo_dev_t	vpdo;
	pdev_common_t		devcom;

	PAGED_CODE();

	DBGI(DBG_GENERAL, "vhci_cleanup: Enter\n");

	devcom = (pdev_common_t)devobj->DeviceExtension;

	// We only allow create/close requests for the vhub.
	if (!devcom->is_vhub) {
		DBGW(DBG_GENERAL, "Bus_Cleanup: Invalid request\n");
		irp->IoStatus.Status = status = STATUS_INVALID_DEVICE_REQUEST;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return status;
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
	}
}

static PAGEABLE NTSTATUS
vhci_cleanup(__in PDEVICE_OBJECT devobj, __in PIRP irp)
{
	pvdev_t	vdev = DEVOBJ_TO_VDEV(devobj);

<<<<<<< HEAD
	PAGED_CODE();

	DBGI(DBG_GENERAL, "vhci_cleanup(%s): Enter\n", dbg_vdev_type(vdev->type));

	// Check to see whether the bus is removed
	if (vdev->DevicePnPState == Deleted) {
		DBGW(DBG_GENERAL, "vhci_cleanup(%s): no such device\n", dbg_vdev_type(vdev->type));
		irp->IoStatus.Status = STATUS_NO_SUCH_DEVICE;
=======
	vhub = (pusbip_vhub_dev_t)devobj->DeviceExtension;

	inc_io_vhub(vhub);

	// Check to see whether the bus is removed
	if (vhub->common.DevicePnPState == Deleted) {
		DBGW(DBG_GENERAL, "vhci_cleanup: No such device\n");
		irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return STATUS_NO_SUCH_DEVICE;
	}
<<<<<<< HEAD
	if (IS_DEVOBJ_VHCI(devobj)) {
		cleanup_vpdo(DEVOBJ_TO_VHCI(devobj), irp);
=======
	irpstack = IoGetCurrentIrpStackLocation(irp);
	vpdo = irpstack->FileObject->FsContext;
	if (vpdo) {
		vpdo->fo = NULL;
		irpstack->FileObject->FsContext = NULL;
		if (vpdo->Present)
<<<<<<< HEAD
			vhci_unplug_dev(vpdo->SerialNo, vhub);
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
=======
			vhci_unplug_dev(vpdo->port, vhub);
>>>>>>> 393ac6a... vhci, let a webcam with IAD be detected as COMPOSITE
	}

	irp->IoStatus.Information = 0;
	irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
<<<<<<< HEAD

	DBGI(DBG_GENERAL, "vhci_cleanup(%s): Leave\n", dbg_vdev_type(vdev->type));
=======
	dec_io_vhub(vhub);

	DBGI(DBG_GENERAL, "vhci_cleanup: Leave\n");
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo

	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS
vhci_close(__in PDEVICE_OBJECT devobj, __in PIRP Irp)
{
<<<<<<< HEAD
	pvdev_t	vdev = DEVOBJ_TO_VDEV(devobj);
=======
	pusbip_vhub_dev_t	vhub;
	pdev_common_t		devcom;
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
	NTSTATUS	status;

	PAGED_CODE();

<<<<<<< HEAD
	// Check to see whether the bus is removed
	if (vdev->DevicePnPState == Deleted) {
=======
	devcom = (pdev_common_t)devobj->DeviceExtension;

	// We only allow create/close requests for vhci itself.

	if (!devcom->is_vhub) {
		Irp->IoStatus.Status = status = STATUS_INVALID_DEVICE_REQUEST;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	vhub = (pusbip_vhub_dev_t)devobj->DeviceExtension;

	inc_io_vhub(vhub);

	// Check to see whether the bus is removed
	if (vhub->common.DevicePnPState == Deleted) {
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}
	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
<<<<<<< HEAD

	return STATUS_SUCCESS;
=======
	dec_io_vhub(vhub);
	return status;
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
}

PAGEABLE NTSTATUS
DriverEntry(__in PDRIVER_OBJECT drvobj, __in PUNICODE_STRING RegistryPath)
{
	DBGI(DBG_GENERAL, "DriverEntry: Enter\n");

	ExInitializeNPagedLookasideList(&g_lookaside, NULL,NULL, 0, sizeof(struct urb_req), 'USBV', 0);

	// Save the RegistryPath for WMI.
	Globals.RegistryPath.MaximumLength = RegistryPath->Length + sizeof(UNICODE_NULL);
	Globals.RegistryPath.Length = RegistryPath->Length;
	Globals.RegistryPath.Buffer = ExAllocatePoolWithTag(PagedPool, Globals.RegistryPath.MaximumLength, USBIP_VHCI_POOL_TAG);

	if (!Globals.RegistryPath.Buffer) {
		ExDeleteNPagedLookasideList(&g_lookaside);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	DBGI(DBG_GENERAL, "RegistryPath %p\r\n", RegistryPath);

	RtlCopyUnicodeString(&Globals.RegistryPath, RegistryPath);

	// Set entry points into the driver
	drvobj->MajorFunction[IRP_MJ_CREATE] = vhci_create;
	drvobj->MajorFunction[IRP_MJ_CLEANUP] = vhci_cleanup;
	drvobj->MajorFunction[IRP_MJ_CLOSE] = vhci_close;
	drvobj->MajorFunction[IRP_MJ_READ] = vhci_read;
	drvobj->MajorFunction[IRP_MJ_WRITE] = vhci_write;
	drvobj->MajorFunction[IRP_MJ_PNP] = vhci_pnp;
	drvobj->MajorFunction[IRP_MJ_POWER] = vhci_power;
	drvobj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = vhci_ioctl;
	drvobj->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = vhci_internal_ioctl;
	drvobj->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = vhci_system_control;
	drvobj->DriverUnload = vhci_driverUnload;
	drvobj->DriverExtension->AddDevice = vhci_add_device;

	DBGI(DBG_GENERAL, "DriverEntry: Leave\n");

	return STATUS_SUCCESS;
<<<<<<< HEAD
=======
}

VOID
inc_io_vhub(__in pusbip_vhub_dev_t vhub)
{
	LONG	result;

	result = InterlockedIncrement(&vhub->OutstandingIO);

	ASSERT(result > 0);

	// Need to clear StopEvent (when OutstandingIO bumps from 1 to 2)
	if (result == 2) {
		//
		// We need to clear the event
		//
		KeClearEvent(&vhub->StopEvent);
	}
}

VOID
dec_io_vhub(__in pusbip_vhub_dev_t vhub)
{
	LONG	result;

	result = InterlockedDecrement(&vhub->OutstandingIO);
	ASSERT(result >= 0);

	if (result == 1) {
		// Set the stop event. Note that when this happens
		// (i.e. a transition from 2 to 1), the type of requests we
		// want to be processed are already held instead of being
		// passed away, so that we can't "miss" a request that
		// will appear between the decrement and the moment when
		// the value is actually used.

		KeSetEvent (&vhub->StopEvent, IO_NO_INCREMENT, FALSE);
	}

	if (result == 0) {
		// The count is 1-biased, so it can be zero only if an
		// extra decrement is done when a remove Irp is received		//
		ASSERT(vhub->common.DevicePnPState == Deleted);

		// Set the remove event, so the device object can be deleted
		KeSetEvent(&vhub->RemoveEvent, IO_NO_INCREMENT, FALSE);
	}
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
}