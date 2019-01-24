#include "vhci.h"

#include <usbdi.h>
#include <wdmguid.h>
#include <usbbusif.h>

#include "usbip_vhci_api.h"
#include "vhci_pnp.h"
#include "usbip_proto.h"

#define USBIP_DEVICE_DESC	L"USB Device Over IP"
#define USBIP_DEVICE_LOCINFO	L"on USB/IP VHCI"

#define FDO_FROM_PDO(pdoData)	((PFDO_DEVICE_DATA) (pdoData)->ParentFdo->DeviceExtension)

extern PAGEABLE NTSTATUS
Bus_DestroyPdo(PDEVICE_OBJECT Device, PPDO_DEVICE_DATA PdoData);

#ifdef DBG
static const char *
dbg_GUID(GUID *guid)
{
	static char	buf[2048];
	int	i;

	for (i = 0; i < sizeof(GUID); i++) {
		RtlStringCchPrintfA(buf + 2 * i, 3, "%02X", ((unsigned char *)guid)[i]);
	}
	return buf;
}
#endif

static PAGEABLE NTSTATUS
Bus_GetDeviceCapabilities(__in PDEVICE_OBJECT DeviceObject, __in PDEVICE_CAPABILITIES DeviceCapabilities)
{
	IO_STATUS_BLOCK		ioStatus;
	PIO_STACK_LOCATION	irpStack;
	KEVENT		pnpEvent;
	PDEVICE_OBJECT	targetObject;
	PIRP		pnpIrp;
	NTSTATUS	status;

	PAGED_CODE();

	//
	// Initialize the capabilities that we will send down
	//
	RtlZeroMemory(DeviceCapabilities, sizeof(DEVICE_CAPABILITIES));
	DeviceCapabilities->Size = sizeof(DEVICE_CAPABILITIES);
	DeviceCapabilities->Version = 1;
	DeviceCapabilities->Address = (ULONG)-1;
	DeviceCapabilities->UINumber = (ULONG)-1;

	KeInitializeEvent(&pnpEvent, NotificationEvent, FALSE);

	targetObject = IoGetAttachedDeviceReference(DeviceObject);

	//
	// Build an Irp
	//
	pnpIrp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, targetObject, NULL, 0, NULL, &pnpEvent, &ioStatus);
	if (pnpIrp == NULL) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto GetDeviceCapabilitiesExit;
	}

	//
	// Pnp Irps all begin life as STATUS_NOT_SUPPORTED;
	//
	pnpIrp->IoStatus.Status = STATUS_NOT_SUPPORTED;
	irpStack = IoGetNextIrpStackLocation(pnpIrp);

	//
	// Set the top of stack
	//
	RtlZeroMemory(irpStack, sizeof(IO_STACK_LOCATION));
	irpStack->MajorFunction = IRP_MJ_PNP;
	irpStack->MinorFunction = IRP_MN_QUERY_CAPABILITIES;
	irpStack->Parameters.DeviceCapabilities.Capabilities = DeviceCapabilities;

	status = IoCallDriver(targetObject, pnpIrp);
	if (status == STATUS_PENDING) {
		//
		// Block until the irp comes back.
		// Important thing to note here is when you allocate
		// the memory for an event in the stack you must do a
		// KernelMode wait instead of UserMode to prevent
		// the stack from getting paged out.
		//
		KeWaitForSingleObject(
			&pnpEvent,
			Executive,
			KernelMode,
			FALSE,
			NULL
		);
		status = ioStatus.Status;
	}

GetDeviceCapabilitiesExit:
	ObDereferenceObject(targetObject);
	return status;
}

static PAGEABLE NTSTATUS
Bus_PDO_QueryDeviceCaps(__in PPDO_DEVICE_DATA DeviceData, __in PIRP Irp)
{

	PIO_STACK_LOCATION	stack;
	PDEVICE_CAPABILITIES	deviceCapabilities;
	DEVICE_CAPABILITIES	parentCapabilities;
	NTSTATUS		status;

	PAGED_CODE ();

	stack = IoGetCurrentIrpStackLocation(Irp);

	deviceCapabilities = stack->Parameters.DeviceCapabilities.Capabilities;

	//
	// Set the capabilities.
	//
	if (deviceCapabilities->Version != 1 || deviceCapabilities->Size < sizeof(DEVICE_CAPABILITIES)) {
		return STATUS_UNSUCCESSFUL;
	}

	//
	// Get the device capabilities of the parent
	//
	status = Bus_GetDeviceCapabilities(FDO_FROM_PDO(DeviceData)->NextLowerDriver, &parentCapabilities);
	if (!NT_SUCCESS(status)) {
		DBGI(DBG_PNP, "QueryDeviceCaps failed\n");
		return status;
	}

	//
	// The entries in the DeviceState array are based on the capabilities
	// of the parent devnode. These entries signify the highest-powered
	// state that the device can support for the corresponding system
	// state. A driver can specify a lower (less-powered) state than the
	// bus driver.  For eg: Suppose the USBIP bus controller supports
	// D0, D2, and D3; and the USBIP Device supports D0, D1, D2, and D3.
	// Following the above rule, the device cannot specify D1 as one of
	// it's power state. A driver can make the rules more restrictive
	// but cannot loosen them.
	// First copy the parent's S to D state mapping
	//
	RtlCopyMemory(deviceCapabilities->DeviceState, parentCapabilities.DeviceState, (PowerSystemShutdown + 1) * sizeof(DEVICE_POWER_STATE));

	//
	// Adjust the caps to what your device supports.
	// Our device just supports D0 and D3.
	//
	deviceCapabilities->DeviceState[PowerSystemWorking] = PowerDeviceD0;

	if (deviceCapabilities->DeviceState[PowerSystemSleeping1] != PowerDeviceD0)
		deviceCapabilities->DeviceState[PowerSystemSleeping1] = PowerDeviceD1;

	if (deviceCapabilities->DeviceState[PowerSystemSleeping2] != PowerDeviceD0)
		deviceCapabilities->DeviceState[PowerSystemSleeping2] = PowerDeviceD3;

	if (deviceCapabilities->DeviceState[PowerSystemSleeping3] != PowerDeviceD0)
		deviceCapabilities->DeviceState[PowerSystemSleeping3] = PowerDeviceD3;

	// We can wake the system from D1
	deviceCapabilities->DeviceWake = PowerDeviceD1;

	//
	// Specifies whether the device hardware supports the D1 and D2
	// power state. Set these bits explicitly.
	//
	deviceCapabilities->DeviceD1 = TRUE; // Yes we can
	deviceCapabilities->DeviceD2 = FALSE;

	//
	// Specifies whether the device can respond to an external wake
	// signal while in the D0, D1, D2, and D3 state.
	// Set these bits explicitly.
	//
	deviceCapabilities->WakeFromD0 = FALSE;
	deviceCapabilities->WakeFromD1 = TRUE; //Yes we can
	deviceCapabilities->WakeFromD2 = FALSE;
	deviceCapabilities->WakeFromD3 = FALSE;

	// We have no latencies
	deviceCapabilities->D1Latency = 0;
	deviceCapabilities->D2Latency = 0;
	deviceCapabilities->D3Latency = 0;

	// Ejection supported
	deviceCapabilities->EjectSupported = FALSE;

	//
	// This flag specifies whether the device's hardware is disabled.
	// The PnP Manager only checks this bit right after the device is
	// enumerated. Once the device is started, this bit is ignored.
	//
	deviceCapabilities->HardwareDisabled = FALSE;

	//
	// Our simulated device can be physically removed.
	//
	deviceCapabilities->Removable = TRUE;
	//
	// Setting it to TRUE prevents the warning dialog from appearing
	// whenever the device is surprise removed.
	//
	deviceCapabilities->SurpriseRemovalOK = TRUE;

	// We don't support system-wide unique IDs.
	deviceCapabilities->UniqueID = FALSE;

	//
	// Specify whether the Device Manager should suppress all
	// installation pop-ups except required pop-ups such as
	// "no compatible drivers found."
	//
	deviceCapabilities->SilentInstall = FALSE;

	//
	// Specifies an address indicating where the device is located
	// on its underlying bus. The interpretation of this number is
	// bus-specific. If the address is unknown or the bus driver
	// does not support an address, the bus driver leaves this
	// member at its default value of 0xFFFFFFFF. In this example
	// the location address is same as instance id.
	//
	deviceCapabilities->Address = DeviceData->SerialNo;

	//
	// UINumber specifies a number associated with the device that can
	// be displayed in the user interface.
	//
	deviceCapabilities->UINumber = DeviceData->SerialNo;

	return STATUS_SUCCESS;
}

static NTSTATUS
setup_pdo_device_id(PPDO_DEVICE_DATA DeviceData, PIRP irp)
{
	PWCHAR	id_dev;

	id_dev = ExAllocatePoolWithTag(PagedPool, 22 * sizeof(wchar_t), USBIP_VHCI_POOL_TAG);
	if (id_dev == NULL) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	RtlStringCchPrintfW(id_dev, 22, L"USB\\Vid_%04hx&Pid_%04hx", DeviceData->vendor, DeviceData->product);
	irp->IoStatus.Information = (ULONG_PTR)id_dev;
	return STATUS_SUCCESS;
}

static NTSTATUS
setup_pdo_inst_id(PPDO_DEVICE_DATA DeviceData, PIRP irp)
{
	PWCHAR	id_inst;

	id_inst = ExAllocatePoolWithTag(PagedPool, 5 * sizeof(wchar_t), USBIP_VHCI_POOL_TAG);
	if (id_inst == NULL) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	RtlStringCchPrintfW(id_inst, 5, L"%04hx", DeviceData->SerialNo);
	irp->IoStatus.Information = (ULONG_PTR)id_inst;
	return STATUS_SUCCESS;
}

static NTSTATUS
setup_pdo_hw_ids(PPDO_DEVICE_DATA DeviceData, PIRP irp)
{
	PWCHAR	ids_hw;

	ids_hw = ExAllocatePoolWithTag(PagedPool, 54 * sizeof(wchar_t), USBIP_VHCI_POOL_TAG);
	if (ids_hw == NULL) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	RtlStringCchPrintfW(ids_hw, 31, L"USB\\Vid_%04hx&Pid_%04hx&Rev_%04hx", DeviceData->vendor, DeviceData->product, DeviceData->revision);
	RtlStringCchPrintfW(ids_hw + 31, 22, L"USB\\Vid_%04hx&Pid_%04hx", DeviceData->vendor, DeviceData->product);
	ids_hw[53] = L'\0';
	irp->IoStatus.Information = (ULONG_PTR)ids_hw;
	return STATUS_SUCCESS;
}

static NTSTATUS
setup_pdo_compat_ids(PPDO_DEVICE_DATA DeviceData, PIRP irp)
{
	PWCHAR	ids_compat;

	ids_compat = ExAllocatePoolWithTag(PagedPool, 86 * sizeof(wchar_t), USBIP_VHCI_POOL_TAG);
	if (ids_compat == NULL) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	RtlStringCchPrintfW(ids_compat, 33, L"USB\\Class_%02x&SubClass_%02x&Prot_%02x", DeviceData->usbclass, DeviceData->subclass, DeviceData->protocol);
	RtlStringCchPrintfW(ids_compat + 33, 25, L"USB\\Class_%02x&SubClass_%02x", DeviceData->usbclass, DeviceData->subclass);
	RtlStringCchPrintfW(ids_compat + 58, 13, L"USB\\Class_%02x", DeviceData->usbclass);
	if (DeviceData->inum > 1) {
		RtlStringCchCopyW(ids_compat + 71, 14, L"USB\\COMPOSITE");
		ids_compat[85] = L'\0';
	}
	else
		ids_compat[71] = L'\0';
	irp->IoStatus.Information = (ULONG_PTR)ids_compat;
	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS
Bus_PDO_QueryDeviceId(__in PPDO_DEVICE_DATA DeviceData, __in PIRP Irp)
{
	PIO_STACK_LOCATION	irpstack;
	NTSTATUS	status;

	PAGED_CODE();

	irpstack = IoGetCurrentIrpStackLocation(Irp);

	switch (irpstack->Parameters.QueryId.IdType) {
	case BusQueryDeviceID:
		status = setup_pdo_device_id(DeviceData, Irp);
		break;
	case BusQueryInstanceID:
		status = setup_pdo_inst_id(DeviceData, Irp);
		break;
	case BusQueryHardwareIDs:
		status = setup_pdo_hw_ids(DeviceData, Irp);
		break;
	case BusQueryCompatibleIDs:
		status = setup_pdo_compat_ids(DeviceData, Irp);
		break;
	case BusQueryContainerID:
		status = STATUS_NOT_SUPPORTED;
		break;
	default:
		DBGE(DBG_PNP, "unhandled bus query: %s\n", dbg_bus_query_id_type(irpstack->Parameters.QueryId.IdType));
		status = STATUS_NOT_SUPPORTED;
		break;
	}
	return status;
}

/*
 * The PnP Manager uses this IRP to get a device's description or location information.
 * This string is displayed in the "found new hardware" pop - up window if no INF match
 * is found for the device. Bus drivers are also encouraged to return location information
 * for their child devices, but this information is optional.
*/
static PAGEABLE NTSTATUS
Bus_PDO_QueryDeviceText(__in PPDO_DEVICE_DATA DeviceData, __in PIRP Irp)
{
	PWCHAR	buffer;
	PIO_STACK_LOCATION	stack;
	NTSTATUS	status;

	UNREFERENCED_PARAMETER(DeviceData);

	PAGED_CODE ();

	status = Irp->IoStatus.Status;

	stack = IoGetCurrentIrpStackLocation(Irp);

	switch (stack->Parameters.QueryDeviceText.DeviceTextType) {
	case DeviceTextDescription:
		if (!Irp->IoStatus.Information) {
			buffer = ExAllocatePoolWithTag(PagedPool, sizeof(USBIP_DEVICE_DESC), USBIP_VHCI_POOL_TAG);
			if (buffer == NULL ) {
				status = STATUS_INSUFFICIENT_RESOURCES;
				break;
			}

			RtlStringCchPrintfW(buffer, sizeof(USBIP_DEVICE_DESC) / sizeof(WCHAR), USBIP_DEVICE_DESC);

			Irp->IoStatus.Information = (ULONG_PTR) buffer;
			status = STATUS_SUCCESS;
		}
		break;
	case DeviceTextLocationInformation:
		if (!Irp->IoStatus.Information) {
			buffer = ExAllocatePoolWithTag(PagedPool, sizeof(USBIP_DEVICE_LOCINFO), USBIP_VHCI_POOL_TAG);
			if (buffer == NULL ) {
				status = STATUS_INSUFFICIENT_RESOURCES;
				break;
			}

			RtlStringCchPrintfW(buffer, sizeof(USBIP_DEVICE_LOCINFO) / sizeof(WCHAR), USBIP_DEVICE_LOCINFO);

			Irp->IoStatus.Information = (ULONG_PTR) buffer;
			status = STATUS_SUCCESS;
		}
		break;
	default:
		DBGI(DBG_PNP, "\tWarning Query what? %d\n", stack->Parameters.QueryDeviceText.DeviceTextType);
		status = STATUS_SUCCESS;
		break;
	}

	return status;
}

static PAGEABLE NTSTATUS
Bus_PDO_QueryResources(__in PPDO_DEVICE_DATA DeviceData, __in PIRP Irp)
{
	/* A device requires no hardware resources */
	PAGED_CODE ();

	UNREFERENCED_PARAMETER(DeviceData);

	return Irp->IoStatus.Status;
}

static PAGEABLE NTSTATUS
Bus_PDO_QueryResourceRequirements(__in PPDO_DEVICE_DATA DeviceData, __in PIRP Irp)
{
	/* A device requires no hardware resources */
	PAGED_CODE();

	UNREFERENCED_PARAMETER(DeviceData);

	return Irp->IoStatus.Status;
}

static PAGEABLE NTSTATUS
Bus_PDO_QueryDeviceRelations(__in PPDO_DEVICE_DATA DeviceData, __in PIRP Irp)
{
	PIO_STACK_LOCATION	stack;
	PDEVICE_RELATIONS	deviceRelations;
	NTSTATUS	status;

	PAGED_CODE ();

	stack = IoGetCurrentIrpStackLocation(Irp);

	switch (stack->Parameters.QueryDeviceRelations.Type) {
	case TargetDeviceRelation:
		deviceRelations = (PDEVICE_RELATIONS)Irp->IoStatus.Information;
		if (deviceRelations) {
			//
			// Only PDO can handle this request. Somebody above
			// is not playing by rule.
			//
			ASSERTMSG("Someone above is handling TagerDeviceRelation", !deviceRelations);
		}

		deviceRelations = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(PagedPool, sizeof(DEVICE_RELATIONS), USBIP_VHCI_POOL_TAG);
		if (!deviceRelations) {
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}

		//
		// There is only one PDO pointer in the structure
		// for this relation type. The PnP Manager removes
		// the reference to the PDO when the driver or application
		// un-registers for notification on the device.
		//
		deviceRelations->Count = 1;
		deviceRelations->Objects[0] = DeviceData->common.Self;
		ObReferenceObject(DeviceData->common.Self);

		status = STATUS_SUCCESS;
		Irp->IoStatus.Information = (ULONG_PTR)deviceRelations;
		break;
	case BusRelations: // Not handled by PDO
	case RemovalRelations: // // optional for PDO
	case EjectionRelations: // optional for PDO
	default:
		status = Irp->IoStatus.Status;
		break;
	}

	return status;
}

static PAGEABLE NTSTATUS
Bus_PDO_QueryBusInformation(__in PPDO_DEVICE_DATA DeviceData, __in PIRP Irp)
{
	PPNP_BUS_INFORMATION busInfo;

	PAGED_CODE ();

	UNREFERENCED_PARAMETER(DeviceData);

	busInfo = ExAllocatePoolWithTag(PagedPool, sizeof(PNP_BUS_INFORMATION), USBIP_VHCI_POOL_TAG);

	if (busInfo == NULL) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	busInfo->BusTypeGuid = GUID_BUS_TYPE_USB;

	//
	// Some buses have a specific INTERFACE_TYPE value,
	// such as PCMCIABus, PCIBus, or PNPISABus.
	// For other buses, especially newer buses like USBIP, the bus
	// driver sets this member to PNPBus.
	//
	busInfo->LegacyBusType = PNPBus;

	//
	// This is an hypothetical bus
	//
	busInfo->BusNumber = 10;
	Irp->IoStatus.Information = (ULONG_PTR)busInfo;

	return STATUS_SUCCESS;
}

BOOLEAN USB_BUSIFFN
IsDeviceHighSpeed(PVOID context)
{
	PPDO_DEVICE_DATA pdodata = context;
	DBGI(DBG_GENERAL, "IsDeviceHighSpeed called, it is %d\n", pdodata->speed);
	if (pdodata->speed == USB_SPEED_HIGH)
		return TRUE;
	return FALSE;
}

static NTSTATUS USB_BUSIFFN
QueryBusInformation(IN PVOID BusContext, IN ULONG Level, IN OUT PVOID BusInformationBuffer,
	IN OUT PULONG BusInformationBufferLength, OUT PULONG BusInformationActualLength)
{
	UNREFERENCED_PARAMETER(BusContext);
	UNREFERENCED_PARAMETER(Level);
	UNREFERENCED_PARAMETER(BusInformationBuffer);
	UNREFERENCED_PARAMETER(BusInformationBufferLength);
	UNREFERENCED_PARAMETER(BusInformationActualLength);

	DBGI(DBG_GENERAL, "QueryBusInformation called\n");
	return STATUS_UNSUCCESSFUL;
}

static NTSTATUS USB_BUSIFFN
SubmitIsoOutUrb(IN PVOID context, IN PURB urb)
{
	UNREFERENCED_PARAMETER(context);
	UNREFERENCED_PARAMETER(urb);

	DBGI(DBG_GENERAL, "SubmitIsoOutUrb called\n");
	return STATUS_UNSUCCESSFUL;
}

static NTSTATUS USB_BUSIFFN
QueryBusTime(IN PVOID context, IN OUT PULONG currentusbframe)
{
	UNREFERENCED_PARAMETER(context);
	UNREFERENCED_PARAMETER(currentusbframe);

	DBGI(DBG_GENERAL, "QueryBusTime called\n");
	return STATUS_UNSUCCESSFUL;
}

static VOID USB_BUSIFFN
GetUSBDIVersion(IN PVOID context, IN OUT PUSBD_VERSION_INFORMATION inf, IN OUT PULONG HcdCapabilities)
{
	UNREFERENCED_PARAMETER(context);

	DBGI(DBG_GENERAL, "GetUSBDIVersion called\n");

	*HcdCapabilities = 0;
	inf->USBDI_Version=0x500; /* Windows XP */
	inf->Supported_USB_Version=0x200; /* USB 2.0 */
}

static VOID
InterfaceReference(__in PVOID Context)
{
	InterlockedIncrement(&((PPDO_DEVICE_DATA)Context)->InterfaceRefCount);
}

static VOID
InterfaceDereference(__in PVOID Context)
{
	InterlockedDecrement(&((PPDO_DEVICE_DATA)Context)->InterfaceRefCount);
}

static PAGEABLE NTSTATUS
Bus_PDO_QueryInterface(__in PPDO_DEVICE_DATA DeviceData, __in PIRP Irp)
{
	PIO_STACK_LOCATION	irpStack;
	GUID			*interfaceType;
	USB_BUS_INTERFACE_USBDI_V1	*bus_intf;
	unsigned int valid_size[2] = { sizeof(USB_BUS_INTERFACE_USBDI_V0), sizeof(USB_BUS_INTERFACE_USBDI_V1) };
	unsigned short	size, version;

	PAGED_CODE();

	irpStack = IoGetCurrentIrpStackLocation(Irp);
	interfaceType = (GUID *) irpStack->Parameters.QueryInterface.InterfaceType;

	if (!IsEqualGUID(interfaceType, (PVOID)&USB_BUS_INTERFACE_USBDI_GUID)){
		DBGI(DBG_GENERAL, "Query unknown interface GUID: %s\n", dbg_GUID(interfaceType));
		return Irp->IoStatus.Status;
	}

	size = irpStack->Parameters.QueryInterface.Size;
	version = irpStack->Parameters.QueryInterface.Version;
	if (version > USB_BUSIF_USBDI_VERSION_1) {
		DBGW(DBG_GENERAL, "unsupported usbdi interface version now %d", version);
		return STATUS_INVALID_PARAMETER;
	}
	if (size < valid_size[version]) {
		DBGW(DBG_GENERAL, "unsupported usbdi interface version now %d", version);
		return STATUS_INVALID_PARAMETER;
	}

	bus_intf = (USB_BUS_INTERFACE_USBDI_V1 *)irpStack->Parameters.QueryInterface.Interface;
	bus_intf->Size = (USHORT)valid_size[version];

	switch (version) {
	case USB_BUSIF_USBDI_VERSION_1:
		bus_intf->IsDeviceHighSpeed = IsDeviceHighSpeed;
		/* passthrough */
	case USB_BUSIF_USBDI_VERSION_0:
		bus_intf->QueryBusInformation = QueryBusInformation;
		bus_intf->SubmitIsoOutUrb = SubmitIsoOutUrb;
		bus_intf->QueryBusTime = QueryBusTime;
		bus_intf->GetUSBDIVersion = GetUSBDIVersion;
		bus_intf->InterfaceReference   = InterfaceReference;
		bus_intf->InterfaceDereference = InterfaceDereference;
		bus_intf->BusContext = DeviceData;
		break;
	default:
		DBGE(DBG_GENERAL, "never go here\n");
		return STATUS_INVALID_PARAMETER;
	}

	InterfaceReference(DeviceData);
	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
Bus_PDO_PnP(__in PDEVICE_OBJECT DeviceObject, __in PIRP Irp, __in PIO_STACK_LOCATION IrpStack, __in PPDO_DEVICE_DATA DeviceData)
{
	NTSTATUS	status;

	PAGED_CODE();

	//
	// NB: Because we are a bus enumerator, we have no one to whom we could
	// defer these irps.  Therefore we do not pass them down but merely
	// return them.
	//
	switch (IrpStack->MinorFunction) {
	case IRP_MN_START_DEVICE:
		//
		// Here we do what ever initialization and ``turning on'' that is
		// required to allow others to access this device.
		// Power up the device.
		//
		DeviceData->common.DevicePowerState = PowerDeviceD0;
		SET_NEW_PNP_STATE(DeviceData, Started);
		status = IoRegisterDeviceInterface(DeviceObject, &GUID_DEVINTERFACE_USB_DEVICE, NULL, &DeviceData->usb_dev_interface);
		if (status == STATUS_SUCCESS)
			IoSetDeviceInterfaceState(&DeviceData->usb_dev_interface, TRUE);
		break;
	case IRP_MN_STOP_DEVICE:
		//
		// Here we shut down the device and give up and unmap any resources
		// we acquired for the device.
		//
		SET_NEW_PNP_STATE(DeviceData, Stopped);
		IoSetDeviceInterfaceState(&DeviceData->usb_dev_interface, FALSE);
		status = STATUS_SUCCESS;
		break;
	case IRP_MN_QUERY_STOP_DEVICE:
		//
		// No reason here why we can't stop the device.
		// If there were a reason we should speak now, because answering success
		// here may result in a stop device irp.
		//
		SET_NEW_PNP_STATE(DeviceData, StopPending);
		status = STATUS_SUCCESS;
		break;
	case IRP_MN_CANCEL_STOP_DEVICE:
		//
		// The stop was canceled.  Whatever state we set, or resources we put
		// on hold in anticipation of the forthcoming STOP device IRP should be
		// put back to normal.  Someone, in the long list of concerned parties,
		// has failed the stop device query.
		//

		//
		// First check to see whether you have received cancel-stop
		// without first receiving a query-stop. This could happen if someone
		// above us fails a query-stop and passes down the subsequent
		// cancel-stop.
		//

		if (StopPending == DeviceData->common.DevicePnPState) {
			// We did receive a query-stop, so restore.
			RESTORE_PREVIOUS_PNP_STATE(DeviceData);
		}
		status = STATUS_SUCCESS;// We must not fail this IRP.
		break;
	case IRP_MN_QUERY_REMOVE_DEVICE:
		// Check to see whether the device can be removed safely.
		// If not fail this request. This is the last opportunity
		// to do so.
		if (DeviceData->InterfaceRefCount) {
			// Somebody is still using our interface.
			// We must fail remove.
			status = STATUS_UNSUCCESSFUL;
			break;
		}

		SET_NEW_PNP_STATE(DeviceData, RemovePending);
		status = STATUS_SUCCESS;
		break;
	case IRP_MN_CANCEL_REMOVE_DEVICE:
		// Clean up a remove that did not go through.

		// First check to see whether you have received cancel-remove
		// without first receiving a query-remove. This could happen if
		// someone above us fails a query-remove and passes down the
		// subsequent cancel-remove.

		if (RemovePending == DeviceData->common.DevicePnPState) {
			// We did receive a query-remove, so restore.
			RESTORE_PREVIOUS_PNP_STATE(DeviceData);
		}
		status = STATUS_SUCCESS; // We must not fail this IRP.
		break;
	case IRP_MN_SURPRISE_REMOVAL:
		// We should stop all access to the device and relinquish all the
		// resources. Let's just mark that it happened and we will do
		// the cleanup later in IRP_MN_REMOVE_DEVICE.
		SET_NEW_PNP_STATE(DeviceData, SurpriseRemovePending);
		status = STATUS_SUCCESS;
		break;
	case IRP_MN_REMOVE_DEVICE:
		// Present is set to true when the pdo is exposed via PlugIn IOCTL.
		// It is set to FALSE when a UnPlug IOCTL is received.
		// We will delete the PDO only after we have reported to the
		// Plug and Play manager that it's missing.

		if (DeviceData->ReportedMissing) {
			PFDO_DEVICE_DATA fdoData;

			SET_NEW_PNP_STATE(DeviceData, Deleted);

			// Remove the PDO from the list and decrement the count of PDO.
			// Don't forget to synchronize access to the FDO data.
			// If the parent FDO is deleted before child PDOs, the ParentFdo
			// pointer will be NULL. This could happen if the child PDO
			// is in a SurpriseRemovePending state when the parent FDO
			// is removed.

			if (DeviceData->ParentFdo) {
				fdoData = FDO_FROM_PDO(DeviceData);
				ExAcquireFastMutex(&fdoData->Mutex);
				RemoveEntryList(&DeviceData->Link);
				fdoData->NumPDOs--;
				ExReleaseFastMutex(&fdoData->Mutex);
			}

			// Free up resources associated with PDO and delete it.
			status = Bus_DestroyPdo(DeviceObject, DeviceData);
			break;
		}
		if (DeviceData->Present) {
			// When the device is disabled, the PDO transitions from
			// RemovePending to NotStarted. We shouldn't delete
			// the PDO because a) the device is still present on the bus,
			// b) we haven't reported missing to the PnP manager.

			SET_NEW_PNP_STATE(DeviceData, NotStarted);
			status = STATUS_SUCCESS;
		}
		else {
			//ASSERT(DeviceData->Present);
			DBGE(DBG_GENERAL, "why we are not present\n");
			status = STATUS_SUCCESS;
		}
		break;
	case IRP_MN_QUERY_CAPABILITIES:
		// Return the capabilities of a device, such as whether the device
		// can be locked or ejected..etc

		status = Bus_PDO_QueryDeviceCaps(DeviceData, Irp);
		break;
	case IRP_MN_QUERY_ID:
		DBGI(DBG_PNP, "QueryId Type: %s\n", dbg_bus_query_id_type(IrpStack->Parameters.QueryId.IdType));

		status = Bus_PDO_QueryDeviceId(DeviceData, Irp);
		break;
	case IRP_MN_QUERY_DEVICE_RELATIONS:
		DBGI(DBG_PNP, "QueryDeviceRelation Type: %s\n", dbg_dev_relation(IrpStack->Parameters.QueryDeviceRelations.Type));
		status = Bus_PDO_QueryDeviceRelations(DeviceData, Irp);
		break;
	case IRP_MN_QUERY_DEVICE_TEXT:
		status = Bus_PDO_QueryDeviceText(DeviceData, Irp);
		break;
	case IRP_MN_QUERY_RESOURCES:
		status = Bus_PDO_QueryResources(DeviceData, Irp);
		break;
	case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
		status = Bus_PDO_QueryResourceRequirements(DeviceData, Irp);
		break;
	case IRP_MN_QUERY_BUS_INFORMATION:
		status = Bus_PDO_QueryBusInformation(DeviceData, Irp);
		break;
	case IRP_MN_DEVICE_USAGE_NOTIFICATION:
		// OPTIONAL for bus drivers.
		// This bus drivers any of the bus's descendants
		// (child device, child of a child device, etc.) do not
		// contain a memory file namely paging file, dump file,
		// or hibernation file. So we  fail this Irp.

		status = STATUS_UNSUCCESSFUL;
		break;
	case IRP_MN_EJECT:
		// For the device to be ejected, the device must be in the D3
		// device power state (off) and must be unlocked
		// (if the device supports locking). Any driver that returns success
		// for this IRP must wait until the device has been ejected before
		// completing the IRP.
		//
		DeviceData->Present = FALSE;

		status = STATUS_SUCCESS;
		break;
	case IRP_MN_QUERY_INTERFACE:
		// This request enables a driver to export a direct-call
		// interface to other drivers. A bus driver that exports
		// an interface must handle this request for its child
		// devices (child PDOs).

		status = Bus_PDO_QueryInterface(DeviceData, Irp);
		break;
	case IRP_MN_DEVICE_ENUMERATED:
		status = STATUS_SUCCESS;
		break;
	default:
		DBGW(DBG_PNP, "not handled: %s\n", dbg_pnp_minor(IrpStack->MinorFunction));
		//
		//Bus_KdPrint_Cont (DeviceData, BUS_DBG_PNP_TRACE,("\t Not handled\n"));
		// For PnP requests to the PDO that we do not understand we should
		// return the IRP WITHOUT setting the status or information fields.
		// These fields may have already been set by a filter (eg acpi).
		status = Irp->IoStatus.Status;
		break;
	}

	Irp->IoStatus.Status = status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return status;
}