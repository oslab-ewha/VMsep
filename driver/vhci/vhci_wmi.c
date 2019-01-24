#include "vhci.h"

#include <wmistr.h>

<<<<<<< HEAD
#include "vhci_dev.h"
#include "usbip_vhci_api.h"
#include "globals.h"

static WMI_SET_DATAITEM_CALLBACK	vhci_SetWmiDataItem;
static WMI_SET_DATABLOCK_CALLBACK	vhci_SetWmiDataBlock;
static WMI_QUERY_DATABLOCK_CALLBACK	vhci_QueryWmiDataBlock;
static WMI_QUERY_REGINFO_CALLBACK	vhci_QueryWmiRegInfo;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, vhci_SetWmiDataItem)
#pragma alloc_text(PAGE, vhci_SetWmiDataBlock)
#pragma alloc_text(PAGE, vhci_QueryWmiDataBlock)
#pragma alloc_text(PAGE, vhci_QueryWmiRegInfo)
=======
#include "device.h"
#include "usbip_vhci_api.h"
#include "globals.h"

static WMI_SET_DATAITEM_CALLBACK Bus_SetWmiDataItem;
static WMI_SET_DATABLOCK_CALLBACK Bus_SetWmiDataBlock;
static WMI_QUERY_DATABLOCK_CALLBACK Bus_QueryWmiDataBlock;
static WMI_QUERY_REGINFO_CALLBACK Bus_QueryWmiRegInfo;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, Bus_SetWmiDataItem)
#pragma alloc_text(PAGE, Bus_SetWmiDataBlock)
#pragma alloc_text(PAGE, Bus_QueryWmiDataBlock)
#pragma alloc_text(PAGE, Bus_QueryWmiRegInfo)
>>>>>>> 48e1018... Beautify VHCI driver code
#endif

#define MOFRESOURCENAME L"USBIPVhciWMI"

#define NUMBER_OF_WMI_GUIDS                 1
#define WMI_USBIP_BUS_DRIVER_INFORMATION  0

static WMIGUIDREGINFO USBIPBusWmiGuidList[] = {
	{ &USBIP_BUS_WMI_STD_DATA_GUID, 1, 0 } // driver information
};

PAGEABLE NTSTATUS
<<<<<<< HEAD
vhci_system_control(__in PDEVICE_OBJECT devobj, __in PIRP irp)
{
	pvhci_dev_t	vhci;
	SYSCTL_IRP_DISPOSITION	disposition;
	PIO_STACK_LOCATION	irpstack;
=======
Bus_SystemControl(__in  PDEVICE_OBJECT DeviceObject, __in PIRP Irp)
{
	PFDO_DEVICE_DATA	fdoData;
	PCOMMON_DEVICE_DATA	commonData;
	SYSCTL_IRP_DISPOSITION	disposition;
	PIO_STACK_LOCATION	stack;
>>>>>>> 48e1018... Beautify VHCI driver code
	NTSTATUS		status;

	PAGED_CODE();

<<<<<<< HEAD
	DBGI(DBG_WMI, "vhci_system_control: Enter\n");

	irpstack = IoGetCurrentIrpStackLocation(irp);

	if (!IS_DEVOBJ_VHCI(devobj)) {
		// The vpdo, just complete the request with the current status
		DBGI(DBG_WMI, "non-vhci: skip: minor:%s\n", dbg_wmi_minor(irpstack->MinorFunction));
		status = irp->IoStatus.Status;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return status;
	}

	vhci = DEVOBJ_TO_VHCI(devobj);

	DBGI(DBG_WMI, "vhci: %s\n", dbg_wmi_minor(irpstack->MinorFunction));

	if (vhci->common.DevicePnPState == Deleted) {
		irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE ;
		IoCompleteRequest (irp, IO_NO_INCREMENT);
		return status;
	}

	status = WmiSystemControl(&vhci->WmiLibInfo, devobj, irp, &disposition);
=======
	DBGI(DBG_WMI, "Bus SystemControl\r\n");

	stack = IoGetCurrentIrpStackLocation(Irp);

	commonData = (PCOMMON_DEVICE_DATA)DeviceObject->DeviceExtension;

	if (!commonData->IsFDO) {
		// The PDO, just complete the request with the current status
		DBGI(DBG_WMI, "PDO %s\n", dbg_wmi_minor(stack->MinorFunction));
		status = Irp->IoStatus.Status;
		IoCompleteRequest (Irp, IO_NO_INCREMENT);
		return status;
	}

	fdoData = (PFDO_DEVICE_DATA)DeviceObject->DeviceExtension;

	DBGI(DBG_WMI, "FDO: %s\n", dbg_wmi_minor(stack->MinorFunction));

	Bus_IncIoCount (fdoData);

	if (fdoData->common.DevicePnPState == Deleted) {
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE ;
		IoCompleteRequest (Irp, IO_NO_INCREMENT);
		Bus_DecIoCount (fdoData);
		return status;
	}

	status = WmiSystemControl(&fdoData->WmiLibInfo, DeviceObject, Irp, &disposition);
>>>>>>> 48e1018... Beautify VHCI driver code
	switch(disposition) {
	case IrpProcessed:
		// This irp has been processed and may be completed or pending.
		break;
	case IrpNotCompleted:
		// This irp has not been completed, but has been fully processed.
		// we will complete it now
<<<<<<< HEAD
		IoCompleteRequest(irp, IO_NO_INCREMENT);
=======
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
>>>>>>> 48e1018... Beautify VHCI driver code
		break;
	case IrpForward:
	case IrpNotWmi:
		// This irp is either not a WMI irp or is a WMI irp targetted
		// at a device lower in the stack.
<<<<<<< HEAD
		IoSkipCurrentIrpStackLocation(irp);
		status = IoCallDriver(vhci->common.devobj_lower, irp);
=======
		IoSkipCurrentIrpStackLocation (Irp);
		status = IoCallDriver(fdoData->NextLowerDriver, Irp);
>>>>>>> 48e1018... Beautify VHCI driver code
		break;
	default:
		// We really should never get here, but if we do just forward....
		ASSERT(FALSE);
<<<<<<< HEAD
		IoSkipCurrentIrpStackLocation(irp);
		status = IoCallDriver(vhci->common.devobj_lower, irp);
		break;
	}

	DBGI(DBG_WMI, "vhci_system_control: Leave: %s\n", dbg_ntstatus(status));

	return status;
=======
		IoSkipCurrentIrpStackLocation (Irp);
		status = IoCallDriver(fdoData->NextLowerDriver, Irp);
		break;
	}

	Bus_DecIoCount (fdoData);

	return(status);
>>>>>>> 48e1018... Beautify VHCI driver code
}

// WMI System Call back functions
static NTSTATUS
<<<<<<< HEAD
vhci_SetWmiDataItem(__in PDEVICE_OBJECT devobj, __in PIRP irp, __in ULONG GuidIndex,
	__in ULONG InstanceIndex, __in ULONG DataItemId, __in ULONG BufferSize, __in_bcount(BufferSize) PUCHAR Buffer)
{
=======
Bus_SetWmiDataItem(__in PDEVICE_OBJECT DeviceObject, __in PIRP Irp, __in ULONG GuidIndex,
	__in ULONG InstanceIndex, __in ULONG DataItemId, __in ULONG BufferSize, __in_bcount(BufferSize) PUCHAR Buffer)
{
	PFDO_DEVICE_DATA	fdoData;
>>>>>>> 48e1018... Beautify VHCI driver code
	ULONG		requiredSize = 0;
	NTSTATUS	status;

	PAGED_CODE();

	UNREFERENCED_PARAMETER(InstanceIndex);
	UNREFERENCED_PARAMETER(Buffer);

<<<<<<< HEAD
	switch (GuidIndex) {
=======
	fdoData = (PFDO_DEVICE_DATA) DeviceObject->DeviceExtension;

	switch(GuidIndex) {
>>>>>>> 48e1018... Beautify VHCI driver code
	case WMI_USBIP_BUS_DRIVER_INFORMATION:
		if (DataItemId == 2) {
			requiredSize = sizeof(ULONG);

			if (BufferSize < requiredSize) {
				status = STATUS_BUFFER_TOO_SMALL;
				break;
			}
			status = STATUS_SUCCESS;
		}
		else {
			status = STATUS_WMI_READ_ONLY;
		}
		break;
	default:
		status = STATUS_WMI_GUID_NOT_FOUND;
	}

<<<<<<< HEAD
	status = WmiCompleteRequest(devobj, irp, status, requiredSize, IO_NO_INCREMENT);
=======
	status = WmiCompleteRequest(DeviceObject, Irp, status, requiredSize, IO_NO_INCREMENT);
>>>>>>> 48e1018... Beautify VHCI driver code

	return status;
}

static NTSTATUS
<<<<<<< HEAD
vhci_SetWmiDataBlock(__in PDEVICE_OBJECT devobj, __in PIRP irp, __in ULONG GuidIndex,
	__in ULONG InstanceIndex, __in ULONG BufferSize, __in_bcount(BufferSize) PUCHAR Buffer)
{
=======
Bus_SetWmiDataBlock(__in PDEVICE_OBJECT DeviceObject, __in PIRP Irp, __in ULONG GuidIndex,
	__in ULONG InstanceIndex, __in ULONG BufferSize, __in_bcount(BufferSize) PUCHAR Buffer)
{
	PFDO_DEVICE_DATA	fdoData;
>>>>>>> 48e1018... Beautify VHCI driver code
	ULONG		requiredSize = 0;
	NTSTATUS	status;

	PAGED_CODE();

	UNREFERENCED_PARAMETER(InstanceIndex);
	UNREFERENCED_PARAMETER(Buffer);

<<<<<<< HEAD
=======
	fdoData = (PFDO_DEVICE_DATA) DeviceObject->DeviceExtension;

>>>>>>> 48e1018... Beautify VHCI driver code
	switch(GuidIndex) {
	case WMI_USBIP_BUS_DRIVER_INFORMATION:
		requiredSize = sizeof(USBIP_BUS_WMI_STD_DATA);

		if (BufferSize < requiredSize) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		status = STATUS_SUCCESS;
		break;
	default:
		status = STATUS_WMI_GUID_NOT_FOUND;
		break;
	}

<<<<<<< HEAD
	status = WmiCompleteRequest(devobj, irp, status, requiredSize, IO_NO_INCREMENT);
=======
	status = WmiCompleteRequest(DeviceObject, Irp, status, requiredSize, IO_NO_INCREMENT);
>>>>>>> 48e1018... Beautify VHCI driver code

	return(status);
}

static NTSTATUS
<<<<<<< HEAD
vhci_QueryWmiDataBlock(__in PDEVICE_OBJECT devobj, __in PIRP irp, __in ULONG GuidIndex,
	__in ULONG InstanceIndex, __in ULONG InstanceCount, __inout PULONG InstanceLengthArray,
	__in ULONG OutBufferSize, __out_bcount(OutBufferSize) PUCHAR Buffer)
{
	pvhci_dev_t	vhci = DEVOBJ_TO_VHCI(devobj);
	ULONG		size = 0;
	NTSTATUS	status;

	UNREFERENCED_PARAMETER(InstanceIndex);
	UNREFERENCED_PARAMETER(InstanceCount);

=======
Bus_QueryWmiDataBlock(__in PDEVICE_OBJECT DeviceObject, __in PIRP Irp, __in ULONG GuidIndex,
	__in ULONG InstanceIndex, __in ULONG InstanceCount, __inout PULONG InstanceLengthArray,
	__in ULONG OutBufferSize, __out_bcount(OutBufferSize) PUCHAR Buffer)
{
	PFDO_DEVICE_DATA	fdoData;
	ULONG		size = 0;
	NTSTATUS	status;

>>>>>>> 48e1018... Beautify VHCI driver code
	PAGED_CODE();

	// Only ever registers 1 instance per guid
	ASSERT((InstanceIndex == 0) && (InstanceCount == 1));

<<<<<<< HEAD
=======
	fdoData = (PFDO_DEVICE_DATA) DeviceObject->DeviceExtension;

>>>>>>> 48e1018... Beautify VHCI driver code
	switch (GuidIndex) {
	case WMI_USBIP_BUS_DRIVER_INFORMATION:
		size = sizeof (USBIP_BUS_WMI_STD_DATA);

		if (OutBufferSize < size) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

<<<<<<< HEAD
		*(PUSBIP_BUS_WMI_STD_DATA)Buffer = vhci->StdUSBIPBusData;
=======
		*(PUSBIP_BUS_WMI_STD_DATA)Buffer = fdoData->StdUSBIPBusData;
>>>>>>> 48e1018... Beautify VHCI driver code
		*InstanceLengthArray = size;
		status = STATUS_SUCCESS;

		break;
	default:
		status = STATUS_WMI_GUID_NOT_FOUND;
	}

<<<<<<< HEAD
	status = WmiCompleteRequest(devobj, irp, status, size, IO_NO_INCREMENT);
=======
	status = WmiCompleteRequest(DeviceObject, Irp, status, size, IO_NO_INCREMENT);
>>>>>>> 48e1018... Beautify VHCI driver code

	return status;
}

static NTSTATUS
<<<<<<< HEAD
vhci_QueryWmiRegInfo(__in PDEVICE_OBJECT devobj, __out ULONG *RegFlags, __out PUNICODE_STRING InstanceName,
	__out PUNICODE_STRING *RegistryPath, __out PUNICODE_STRING MofResourceName, __out PDEVICE_OBJECT *Pdo)
{
	pvhci_dev_t	vhci = DEVOBJ_TO_VHCI(devobj);
=======
Bus_QueryWmiRegInfo(__in PDEVICE_OBJECT DeviceObject, __out ULONG *RegFlags, __out PUNICODE_STRING InstanceName,
	__out PUNICODE_STRING *RegistryPath, __out PUNICODE_STRING MofResourceName, __out PDEVICE_OBJECT *Pdo)
{
	PFDO_DEVICE_DATA fdoData;
>>>>>>> 48e1018... Beautify VHCI driver code

	PAGED_CODE();

	UNREFERENCED_PARAMETER(InstanceName);

<<<<<<< HEAD
	*RegFlags = WMIREG_FLAG_INSTANCE_PDO;
	*RegistryPath = &Globals.RegistryPath;
	*Pdo = vhci->common.pdo;
=======
	fdoData = (PFDO_DEVICE_DATA)DeviceObject->DeviceExtension;

	*RegFlags = WMIREG_FLAG_INSTANCE_PDO;
	*RegistryPath = &Globals.RegistryPath;
	*Pdo = fdoData->UnderlyingPDO;
>>>>>>> 48e1018... Beautify VHCI driver code
	RtlInitUnicodeString(MofResourceName, MOFRESOURCENAME);

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
<<<<<<< HEAD
reg_wmi(pvhci_dev_t vhci)
{
	NTSTATUS	status;

	PAGED_CODE();

	vhci->WmiLibInfo.GuidCount = sizeof(USBIPBusWmiGuidList) /
		sizeof(WMIGUIDREGINFO);
	ASSERT(NUMBER_OF_WMI_GUIDS == vhci->WmiLibInfo.GuidCount);
	vhci->WmiLibInfo.GuidList = USBIPBusWmiGuidList;
	vhci->WmiLibInfo.QueryWmiRegInfo = vhci_QueryWmiRegInfo;
	vhci->WmiLibInfo.QueryWmiDataBlock = vhci_QueryWmiDataBlock;
	vhci->WmiLibInfo.SetWmiDataBlock = vhci_SetWmiDataBlock;
	vhci->WmiLibInfo.SetWmiDataItem = vhci_SetWmiDataItem;
	vhci->WmiLibInfo.ExecuteWmiMethod = NULL;
	vhci->WmiLibInfo.WmiFunctionControl = NULL;

	// Register with WMI
	status = IoWMIRegistrationControl(TO_DEVOBJ(vhci), WMIREG_ACTION_REGISTER);

	// Initialize the Std device data structure
	vhci->StdUSBIPBusData.ErrorCount = 0;
=======
Bus_WmiRegistration(PFDO_DEVICE_DATA FdoData)
{
	NTSTATUS status;

	PAGED_CODE();

	FdoData->WmiLibInfo.GuidCount = sizeof(USBIPBusWmiGuidList) /
		sizeof(WMIGUIDREGINFO);
	ASSERT(NUMBER_OF_WMI_GUIDS == FdoData->WmiLibInfo.GuidCount);
	FdoData->WmiLibInfo.GuidList = USBIPBusWmiGuidList;
	FdoData->WmiLibInfo.QueryWmiRegInfo = Bus_QueryWmiRegInfo;
	FdoData->WmiLibInfo.QueryWmiDataBlock = Bus_QueryWmiDataBlock;
	FdoData->WmiLibInfo.SetWmiDataBlock = Bus_SetWmiDataBlock;
	FdoData->WmiLibInfo.SetWmiDataItem = Bus_SetWmiDataItem;
	FdoData->WmiLibInfo.ExecuteWmiMethod = NULL;
	FdoData->WmiLibInfo.WmiFunctionControl = NULL;

	// Register with WMI
	status = IoWMIRegistrationControl(FdoData->common.Self, WMIREG_ACTION_REGISTER);

	// Initialize the Std device data structure
	FdoData->StdUSBIPBusData.ErrorCount = 0;
>>>>>>> 48e1018... Beautify VHCI driver code

	return status;
}

PAGEABLE NTSTATUS
<<<<<<< HEAD
dereg_wmi(pvhci_dev_t vhci)
{
	PAGED_CODE();

	return IoWMIRegistrationControl(TO_DEVOBJ(vhci), WMIREG_ACTION_DEREGISTER);
=======
Bus_WmiDeRegistration(PFDO_DEVICE_DATA FdoData)
{
	PAGED_CODE();

	return IoWMIRegistrationControl(FdoData->common.Self, WMIREG_ACTION_DEREGISTER);
>>>>>>> 48e1018... Beautify VHCI driver code
}