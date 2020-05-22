#include "vhci.h"

#include <wdmguid.h>
#include <usbdi.h>
#include <usbbusif.h>

#include "vhci_pnp.h"
#include "usbip_vhci_api.h"
#include "usbip_proto.h"

extern PAGEABLE void vhub_invalidate_unplugged_vpdos(pvhub_dev_t vhub);
extern PAGEABLE NTSTATUS vhub_unplug_vpdo(pvhub_dev_t vhub, ULONG port, BOOLEAN is_eject);

extern PAGEABLE void vhub_detach_vpdo(pvhub_dev_t vhub, pvpdo_dev_t vpdo);
extern PAGEABLE void vhub_mark_unplugged_vpdo(pvhub_dev_t vhub, pvpdo_dev_t vpdo);

// IRP_MN_DEVICE_ENUMERATED is included by default since Windows 7.
#if WINVER<0x0701
#define IRP_MN_DEVICE_ENUMERATED 0x19
#endif

#define USBIP_DEVICE_DESC	L"USB Device Over IP"
#define USBIP_DEVICE_LOCINFO	L"on USB/IP VHCI"

/* Device with zero class/subclass/protocol */
#define IS_ZERO_CLASS(vpdo)	((vpdo)->usbclass == 0x00 && (vpdo)->subclass == 0x00 && (vpdo)->protocol == 0x00 && (vpdo)->inum > 1)
/* Device with IAD(Interface Association Descriptor) */
#define IS_IAD_DEVICE(vpdo)	((vpdo)->usbclass == 0xef && (vpdo)->subclass == 0x02 && (vpdo)->protocol == 0x01)

PAGEABLE NTSTATUS
vhci_unplug_port(pvhci_dev_t vhci, ULONG port)
{
	NTSTATUS	status;

	PAGED_CODE();

	if (port > 127)
		return STATUS_INVALID_PARAMETER;

	if (port == 0)
		DBGI(DBG_PNP, "plugging out all the devices!\n");
	else
		DBGI(DBG_PNP, "plugging out device: port: %u\n", port);

	status = vhub_unplug_vpdo(vhci->vhub, port, FALSE);

	switch (status) {
	case STATUS_NO_SUCH_DEVICE:
		// We got a 2nd plugout...somebody in user space isn't playing nice!!!
		DBGW(DBG_PNP, "BAD BAD BAD...2 removes!!! Send only one!\n");
		break;
	case STATUS_SUCCESS:
		IoInvalidateDeviceRelations(vhci->UnderlyingPDO, BusRelations);
		vhub_invalidate_unplugged_vpdos(vhci->vhub);
		if (port == 0)
			DBGI(DBG_PNP, "all the devices are plugged out\n");
		else
			DBGI(DBG_PNP, "the device is plugged out: port: %u\n", port);
		break;
	default:
		break;
	}

	return status;
}

PAGEABLE NTSTATUS
vhci_eject_port(pvhci_dev_t vhci, ULONG port)
{
	pvhub_dev_t	vhub = vhci->vhub;
	NTSTATUS	status;

	PAGED_CODE ();

	if (port == 0)
		DBGI(DBG_PNP, "ejecting all the devices!\n");
	else
		DBGI(DBG_PNP, "ejecting device: port: %u\n", port);

	if (vhci->vhub->n_vpdos == 0) {
		ExReleaseFastMutex(&vhub->Mutex);
		return STATUS_NO_SUCH_DEVICE;
	}

	status = vhub_unplug_vpdo(vhub, port, TRUE);

	switch (status) {
	case STATUS_NO_SUCH_DEVICE:
		// Somebody in user space isn't playing nice!!!
		DBGW(DBG_PNP, "No devices to eject!\n");
		break;
	case STATUS_SUCCESS:
		break;
	default:
		if (port == 0)
			DBGI(DBG_PNP, "no device to be ejected\n");
		else
			DBGI(DBG_PNP, "the device does not exist: port: %u\n", port);
		break;
	}

	return status;
}

static PAGEABLE NTSTATUS
get_device_capabilities(__in PDEVICE_OBJECT devobj, __in PDEVICE_CAPABILITIES DeviceCapabilities)
{
	IO_STATUS_BLOCK		ioStatus;
	PIO_STACK_LOCATION	irpStack;
	KEVENT		pnpEvent;
	PDEVICE_OBJECT	targetObject;
	PIRP		pnpIrp;
	NTSTATUS	status;

	PAGED_CODE();

	// Initialize the capabilities that we will send down
	RtlZeroMemory(DeviceCapabilities, sizeof(DEVICE_CAPABILITIES));
	DeviceCapabilities->Size = sizeof(DEVICE_CAPABILITIES);
	DeviceCapabilities->Version = 1;
	DeviceCapabilities->Address = (ULONG)-1;
	DeviceCapabilities->UINumber = (ULONG)-1;

	KeInitializeEvent(&pnpEvent, NotificationEvent, FALSE);

	targetObject = IoGetAttachedDeviceReference(devobj);

	// Build an Irp
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
		// Block until the irp comes back.
		// Important thing to note here is when you allocate
		// the memory for an event in the stack you must do a
		// KernelMode wait instead of UserMode to prevent
		// the stack from getting paged out.
		KeWaitForSingleObject(&pnpEvent, Executive, KernelMode, FALSE, NULL);
		status = ioStatus.Status;
	}

GetDeviceCapabilitiesExit:
	ObDereferenceObject(targetObject);
	return status;
}

static PAGEABLE NTSTATUS
vhci_QueryDeviceCaps_vpdo(pvpdo_dev_t vpdo, PIRP irp)
{
	PIO_STACK_LOCATION	stack;
	PDEVICE_CAPABILITIES	deviceCapabilities;
	DEVICE_CAPABILITIES	parentCapabilities;
	NTSTATUS		status;

	PAGED_CODE();

	stack = IoGetCurrentIrpStackLocation(irp);

	deviceCapabilities = stack->Parameters.DeviceCapabilities.Capabilities;

	//
	// Set the capabilities.
	//
	if (deviceCapabilities->Version != 1 || deviceCapabilities->Size < sizeof(DEVICE_CAPABILITIES)) {
		DBGW(DBG_PNP, "Invalid deviceCapabilities\n");
		return STATUS_UNSUCCESSFUL;
	}

	//
	// Get the device capabilities of the parent
	//
	status = get_device_capabilities(vpdo->vhub->vhci->devobj_lower, &parentCapabilities);
	if (!NT_SUCCESS(status)) {
		DBGI(DBG_PNP, "QueryDeviceCaps failed\n");
		return status;
	}

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
	RtlCopyMemory(deviceCapabilities->DeviceState, parentCapabilities.DeviceState, (PowerSystemShutdown + 1) * sizeof(DEVICE_POWER_STATE));

	// Adjust the caps to what your device supports.
	// Our device just supports D0 and D3.
	deviceCapabilities->DeviceState[PowerSystemWorking] = PowerDeviceD0;

	if (deviceCapabilities->DeviceState[PowerSystemSleeping1] != PowerDeviceD0)
		deviceCapabilities->DeviceState[PowerSystemSleeping1] = PowerDeviceD1;

	if (deviceCapabilities->DeviceState[PowerSystemSleeping2] != PowerDeviceD0)
		deviceCapabilities->DeviceState[PowerSystemSleeping2] = PowerDeviceD3;

	if (deviceCapabilities->DeviceState[PowerSystemSleeping3] != PowerDeviceD0)
		deviceCapabilities->DeviceState[PowerSystemSleeping3] = PowerDeviceD3;

	// We can wake the system from D1
	deviceCapabilities->DeviceWake = PowerDeviceD1;

	// Specifies whether the device hardware supports the D1 and D2
	// power state. Set these bits explicitly.
	deviceCapabilities->DeviceD1 = TRUE; // Yes we can
	deviceCapabilities->DeviceD2 = FALSE;

	// Specifies whether the device can respond to an external wake
	// signal while in the D0, D1, D2, and D3 state.
	// Set these bits explicitly.
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

	// This flag specifies whether the device's hardware is disabled.
	// The PnP Manager only checks this bit right after the device is
	// enumerated. Once the device is started, this bit is ignored.
	deviceCapabilities->HardwareDisabled = FALSE;

	// Our simulated device can be physically removed.
	deviceCapabilities->Removable = TRUE;

	// Setting it to TRUE prevents the warning dialog from appearing
	// whenever the device is surprise removed.
	deviceCapabilities->SurpriseRemovalOK = TRUE;

	// If a custom instance id is used, assume that it is system-wide unique */
	deviceCapabilities->UniqueID = (vpdo->winstid != NULL) ? TRUE: FALSE;

	// Specify whether the Device Manager should suppress all
	// installation pop-ups except required pop-ups such as
	// "no compatible drivers found."
	deviceCapabilities->SilentInstall = FALSE;

	// Specifies an address indicating where the device is located
	// on its underlying bus. The interpretation of this number is
	// bus-specific. If the address is unknown or the bus driver
	// does not support an address, the bus driver leaves this
	// member at its default value of 0xFFFFFFFF. In this example
	// the location address is same as instance id.
	deviceCapabilities->Address = vpdo->port;

	// UINumber specifies a number associated with the device that can
	// be displayed in the user interface.
	deviceCapabilities->UINumber = vpdo->port;

	return STATUS_SUCCESS;
}

static NTSTATUS
setup_vpdo_device_id(pvpdo_dev_t vpdo, PIRP irp)
{
	PWCHAR	id_dev;

	id_dev = ExAllocatePoolWithTag(PagedPool, 22 * sizeof(wchar_t), USBIP_VHCI_POOL_TAG);
	if (id_dev == NULL) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	RtlStringCchPrintfW(id_dev, 22, L"USB\\VID_%04hx&PID_%04hx", vpdo->vendor, vpdo->product);
	irp->IoStatus.Information = (ULONG_PTR)id_dev;
	return STATUS_SUCCESS;
}

static NTSTATUS
setup_vpdo_inst_id(pvpdo_dev_t vpdo, PIRP irp)
{
	PWCHAR	id_inst;

	id_inst = ExAllocatePoolWithTag(PagedPool, (MAX_VHCI_INSTANCE_ID + 1) * sizeof(wchar_t), USBIP_VHCI_POOL_TAG);
	if (id_inst == NULL) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	if (vpdo->winstid != NULL)
		RtlStringCchCopyW(id_inst, MAX_VHCI_INSTANCE_ID + 1, vpdo->winstid);
	else
		RtlStringCchPrintfW(id_inst, 5, L"%04hx", vpdo->port);

	irp->IoStatus.Information = (ULONG_PTR)id_inst;
	return STATUS_SUCCESS;
}

static NTSTATUS
setup_vpdo_hw_ids(pvpdo_dev_t vpdo, PIRP irp)
{
	PWCHAR	ids_hw;

	ids_hw = ExAllocatePoolWithTag(PagedPool, 54 * sizeof(wchar_t), USBIP_VHCI_POOL_TAG);
	if (ids_hw == NULL) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	RtlStringCchPrintfW(ids_hw, 31, L"USB\\VID_%04hx&PID_%04hx&REV_%04hx", vpdo->vendor, vpdo->product, vpdo->revision);
	RtlStringCchPrintfW(ids_hw + 31, 22, L"USB\\VID_%04hx&PID_%04hx", vpdo->vendor, vpdo->product);
	ids_hw[53] = L'\0';
	irp->IoStatus.Information = (ULONG_PTR)ids_hw;
	return STATUS_SUCCESS;
}

static NTSTATUS
setup_vpdo_compat_ids(pvpdo_dev_t vpdo, PIRP irp)
{
	PWCHAR	ids_compat;

	ids_compat = ExAllocatePoolWithTag(PagedPool, 86 * sizeof(wchar_t), USBIP_VHCI_POOL_TAG);
	if (ids_compat == NULL) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	RtlStringCchPrintfW(ids_compat, 33, L"USB\\Class_%02hhx&SubClass_%02hhx&Prot_%02hhx", vpdo->usbclass, vpdo->subclass, vpdo->protocol);
	RtlStringCchPrintfW(ids_compat + 33, 25, L"USB\\Class_%02hhx&SubClass_%02hhx", vpdo->usbclass, vpdo->subclass);
	RtlStringCchPrintfW(ids_compat + 58, 13, L"USB\\Class_%02hhx", vpdo->usbclass);
	if (IS_ZERO_CLASS(vpdo) || IS_IAD_DEVICE(vpdo)) {
		RtlStringCchCopyW(ids_compat + 71, 14, L"USB\\COMPOSITE");
		ids_compat[85] = L'\0';
	}
	else
		ids_compat[71] = L'\0';
	irp->IoStatus.Information = (ULONG_PTR)ids_compat;
	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS
vhci_QueryDeviceId_vpdo(__in pvpdo_dev_t vpdo, __in PIRP Irp)
{
	PIO_STACK_LOCATION	irpstack;
	NTSTATUS	status;

	PAGED_CODE();

	irpstack = IoGetCurrentIrpStackLocation(Irp);

	switch (irpstack->Parameters.QueryId.IdType) {
	case BusQueryDeviceID:
		status = setup_vpdo_device_id(vpdo, Irp);
		break;
	case BusQueryInstanceID:
		status = setup_vpdo_inst_id(vpdo, Irp);
		break;
	case BusQueryHardwareIDs:
		status = setup_vpdo_hw_ids(vpdo, Irp);
		break;
	case BusQueryCompatibleIDs:
		status = setup_vpdo_compat_ids(vpdo, Irp);
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
vhci_QueryDeviceText_vpdo(__in pvpdo_dev_t vpdo, __in PIRP Irp)
{
	PWCHAR	buffer;
	PIO_STACK_LOCATION	stack;
	NTSTATUS	status;

	UNREFERENCED_PARAMETER(vpdo);

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
vhci_QueryResources_vpdo(__in pvpdo_dev_t vpdo, __in PIRP Irp)
{
	/* A device requires no hardware resources */
	PAGED_CODE ();

	UNREFERENCED_PARAMETER(vpdo);

	return Irp->IoStatus.Status;
}

static PAGEABLE NTSTATUS
vhci_QueryResourceRequirements_vpdo(__in pvpdo_dev_t vpdo, __in PIRP Irp)
{
	/* A device requires no hardware resources */
	PAGED_CODE();

	UNREFERENCED_PARAMETER(vpdo);

	return Irp->IoStatus.Status;
}

static PAGEABLE NTSTATUS
vhci_QueryDeviceRelations_vpdo(__in pvpdo_dev_t vpdo, __in PIRP Irp)
{
	PIO_STACK_LOCATION	stack;
	PDEVICE_RELATIONS	deviceRelations;
	NTSTATUS	status;

	PAGED_CODE();

	stack = IoGetCurrentIrpStackLocation(Irp);

	switch (stack->Parameters.QueryDeviceRelations.Type) {
	case TargetDeviceRelation:
		deviceRelations = (PDEVICE_RELATIONS)Irp->IoStatus.Information;
		if (deviceRelations) {
			// Only vpdo can handle this request. Somebody above
			// is not playing by rule.
			ASSERTMSG("Someone above is handling TagerDeviceRelation", !deviceRelations);
		}

		deviceRelations = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(PagedPool, sizeof(DEVICE_RELATIONS), USBIP_VHCI_POOL_TAG);
		if (!deviceRelations) {
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}

		// There is only one vpdo in the structure
		// for this relation type. The PnP Manager removes
		// the reference to the vpdo when the driver or application
		// un-registers for notification on the device.
		deviceRelations->Count = 1;
		deviceRelations->Objects[0] = vpdo->common.Self;
		ObReferenceObject(vpdo->common.Self);

		status = STATUS_SUCCESS;
		Irp->IoStatus.Information = (ULONG_PTR)deviceRelations;
		break;
	case BusRelations: // Not handled by vpdo
	case RemovalRelations: // // optional for vpdo
	case EjectionRelations: // optional for vpdo
	default:
		status = Irp->IoStatus.Status;
		break;
	}

	return status;
}

static PAGEABLE NTSTATUS
vhci_QueryBusInformation_vpdo(__in pvpdo_dev_t vpdo, __in PIRP Irp)
{
	PPNP_BUS_INFORMATION busInfo;

	PAGED_CODE ();

	UNREFERENCED_PARAMETER(vpdo);

	busInfo = ExAllocatePoolWithTag(PagedPool, sizeof(PNP_BUS_INFORMATION), USBIP_VHCI_POOL_TAG);

	if (busInfo == NULL) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	busInfo->BusTypeGuid = GUID_BUS_TYPE_USB;

	// Some buses have a specific INTERFACE_TYPE value,
	// such as PCMCIABus, PCIBus, or PNPISABus.
	// For other buses, especially newer buses like USBIP, the bus
	// driver sets this member to PNPBus.
	busInfo->LegacyBusType = PNPBus;

	// This is an hypothetical bus
	busInfo->BusNumber = 10;
	Irp->IoStatus.Information = (ULONG_PTR)busInfo;

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
vhci_pnp_vpdo(pvpdo_dev_t vpdo, PIRP irp, PIO_STACK_LOCATION irpstack)
{
	NTSTATUS	status;

	PAGED_CODE();

	// NB: Because we are a bus enumerator, we have no one to whom we could
	// defer these irps.  Therefore we do not pass them down but merely
	// return them.
	switch (irpstack->MinorFunction) {
	case IRP_MN_START_DEVICE:
		// Here we do what ever initialization and ``turning on'' that is
		// required to allow others to access this device.
		// Power up the device.
		vpdo->common.DevicePowerState = PowerDeviceD0;
		SET_NEW_PNP_STATE(vpdo, Started);
		status = IoRegisterDeviceInterface(TO_DEVOBJ(vpdo), &GUID_DEVINTERFACE_USB_DEVICE, NULL, &vpdo->usb_dev_interface);
		if (status == STATUS_SUCCESS)
			IoSetDeviceInterfaceState(&vpdo->usb_dev_interface, TRUE);
		DBGI(DBG_GENERAL, "Device started: %s\n", dbg_ntstatus(status));
		break;
	case IRP_MN_STOP_DEVICE:
		// Here we shut down the device and give up and unmap any resources
		// we acquired for the device.
		SET_NEW_PNP_STATE(vpdo, Stopped);
		IoSetDeviceInterfaceState(&vpdo->usb_dev_interface, FALSE);
		status = STATUS_SUCCESS;
		break;
	case IRP_MN_QUERY_STOP_DEVICE:
		// No reason here why we can't stop the device.
		// If there were a reason we should speak now, because answering success
		// here may result in a stop device irp.
		SET_NEW_PNP_STATE(vpdo, StopPending);
		status = STATUS_SUCCESS;
		break;
	case IRP_MN_CANCEL_STOP_DEVICE:
		// The stop was canceled.  Whatever state we set, or resources we put
		// on hold in anticipation of the forthcoming STOP device IRP should be
		// put back to normal.  Someone, in the long list of concerned parties,
		// has failed the stop device query.

		// First check to see whether you have received cancel-stop
		// without first receiving a query-stop. This could happen if someone
		// above us fails a query-stop and passes down the subsequent
		// cancel-stop.

		if (StopPending == vpdo->common.DevicePnPState) {
			// We did receive a query-stop, so restore.
			RESTORE_PREVIOUS_PNP_STATE(vpdo);
		}
		status = STATUS_SUCCESS;// We must not fail this IRP.
		break;
	case IRP_MN_QUERY_REMOVE_DEVICE:
		// Check to see whether the device can be removed safely.
		// If not fail this request. This is the last opportunity
		// to do so.
		if (vpdo->common.n_refs > 0) {
			// Somebody is still using our interface.
			// We must fail remove.
			status = STATUS_UNSUCCESSFUL;
			break;
		}

		SET_NEW_PNP_STATE(vpdo, RemovePending);
		status = STATUS_SUCCESS;
		break;
	case IRP_MN_CANCEL_REMOVE_DEVICE:
		// Clean up a remove that did not go through.

		// First check to see whether you have received cancel-remove
		// without first receiving a query-remove. This could happen if
		// someone above us fails a query-remove and passes down the
		// subsequent cancel-remove.
		if (RemovePending == vpdo->common.DevicePnPState) {
			// We did receive a query-remove, so restore.
			RESTORE_PREVIOUS_PNP_STATE(vpdo);
		}
		status = STATUS_SUCCESS; // We must not fail this IRP.
		break;
	case IRP_MN_SURPRISE_REMOVAL:
		// We should stop all access to the device and relinquish all the
		// resources. Let's just mark that it happened and we will do
		// the cleanup later in IRP_MN_REMOVE_DEVICE.
		SET_NEW_PNP_STATE(vpdo, SurpriseRemovePending);
		status = STATUS_SUCCESS;
		break;
	case IRP_MN_REMOVE_DEVICE:
		// Present is set to true when the pdo is exposed via PlugIn IOCTL.
		// It is set to FALSE when a UnPlug IOCTL is received.
		// We will delete the vpdo only after we have reported to the
		// Plug and Play manager that it's missing.
		if (vpdo->ReportedMissing) {
			SET_NEW_PNP_STATE(vpdo, Deleted);

			// Remove the vpdo from the list and decrement the count of vpdo.
			// Don't forget to synchronize access to the vhub.
			// If the parent vhub is already deleted, vhub will be NULL.
			// This could happen if the child vpdo is in a SurpriseRemovePending
			// state when the vhub is removed.
			if (vpdo->vhub)
				vhub_detach_vpdo(vpdo->vhub, vpdo);

			// Free up resources associated with vpdo and delete it.
			status = destroy_vpdo(vpdo);
		}
		else if (vpdo->plugged) {
			// When the device is disabled, the vpdo transitions from
			// RemovePending to NotStarted. We shouldn't delete the vpdo because
			// a) the device is still present on the bus,
			// b) we haven't reported missing to the PnP manager.

			SET_NEW_PNP_STATE(vpdo, NotStarted);
			status = STATUS_SUCCESS;
		}
		else {
			DBGE(DBG_GENERAL, "why we are not present??: vpdo port: %u\n", vpdo->port);
			status = STATUS_UNSUCCESSFUL;
		}
		break;
	case IRP_MN_QUERY_CAPABILITIES:
		// Return the capabilities of a device, such as whether the device
		// can be locked or ejected..etc

		status = vhci_QueryDeviceCaps_vpdo(vpdo, irp);
		break;
	case IRP_MN_QUERY_ID:
		DBGI(DBG_PNP, "QueryId Type: %s\n", dbg_bus_query_id_type(irpstack->Parameters.QueryId.IdType));

		status = vhci_QueryDeviceId_vpdo(vpdo, irp);
		break;
	case IRP_MN_QUERY_DEVICE_RELATIONS:
		DBGI(DBG_PNP, "QueryDeviceRelation Type: %s\n", dbg_dev_relation(irpstack->Parameters.QueryDeviceRelations.Type));
		status = vhci_QueryDeviceRelations_vpdo(vpdo, irp);
		break;
	case IRP_MN_QUERY_DEVICE_TEXT:
		status = vhci_QueryDeviceText_vpdo(vpdo, irp);
		break;
	case IRP_MN_QUERY_RESOURCES:
		status = vhci_QueryResources_vpdo(vpdo, irp);
		break;
	case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
		status = vhci_QueryResourceRequirements_vpdo(vpdo, irp);
		break;
	case IRP_MN_QUERY_BUS_INFORMATION:
		status = vhci_QueryBusInformation_vpdo(vpdo, irp);
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

		vhub_mark_unplugged_vpdo(vpdo->vhub, vpdo);

		status = STATUS_SUCCESS;
		break;
	case IRP_MN_DEVICE_ENUMERATED:
		//
		// This request notifies bus drivers that a device object exists and
		// that it has been fully enumerated by the plug and play manager.
		//
		status = STATUS_SUCCESS;
		break;
	case IRP_MN_QUERY_PNP_DEVICE_STATE:
		irp->IoStatus.Information = 0;
		status = irp->IoStatus.Status = STATUS_SUCCESS;
		break;
	case IRP_MN_QUERY_LEGACY_BUS_INFORMATION:
	case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
		/* not handled */
		status = irp->IoStatus.Status;
		break;
	default:
		DBGW(DBG_PNP, "not handled: %s\n", dbg_pnp_minor(irpstack->MinorFunction));

		// For PnP requests to the vpdo that we do not understand we should
		// return the IRP WITHOUT setting the status or information fields.
		// These fields may have already been set by a filter (eg acpi).
		status = irp->IoStatus.Status;
		break;
	}

	irp->IoStatus.Status = status;
	IoCompleteRequest(irp, IO_NO_INCREMENT);

	return status;
}
