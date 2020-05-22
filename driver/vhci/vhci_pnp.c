#include "vhci.h"

#include "usbip_vhci_api.h"
#include "vhci_pnp.h"
#include "usbreq.h"

extern NTSTATUS
vhci_pnp_vhci(pvhci_dev_t vhci, PIRP irp, PIO_STACK_LOCATION irpstack);
extern NTSTATUS
vhci_pnp_hpdo(phpdo_dev_t hpdo, PIRP irp, PIO_STACK_LOCATION irpstack);
extern NTSTATUS
vhci_pnp_vhub(pvhub_dev_t vhub, PIRP irp, PIO_STACK_LOCATION irpstack);
extern NTSTATUS
vhci_pnp_vpdo(pvpdo_dev_t vpdo, PIRP irp, PIO_STACK_LOCATION irpstack);
extern NTSTATUS
vhci_add_vhci(PDRIVER_OBJECT drvobj, PDEVICE_OBJECT pdo, phpdo_dev_t hpdo);
extern NTSTATUS
vhci_add_vhub(pvhci_dev_t vhci, PDEVICE_OBJECT pdo);

extern NTSTATUS
vdev_query_interface(pvdev_t vdev, PIO_STACK_LOCATION irpstack);

static LPCWSTR vdev_descs[] = {
	L"usbip-win VHCI", L"usbip-win HPDO", L"usbip-win VHUB", L"usbip-win VPDO"
};

static LPCWSTR vdev_locinfos[] = {
	L"root", L"VHCI", L"VHCI", L"HPDO"
};

PAGEABLE NTSTATUS
vdev_query_device_text(pvdev_t vdev, PIRP irp)
{
	PIO_STACK_LOCATION	irpstack;
	NTSTATUS	status;

	PAGED_CODE();

	status = irp->IoStatus.Status;
	irpstack = IoGetCurrentIrpStackLocation(irp);

	switch (irpstack->Parameters.QueryDeviceText.DeviceTextType) {
	case DeviceTextDescription:
		if (!irp->IoStatus.Information) {
			irp->IoStatus.Information = (ULONG_PTR)libdrv_strdupW(vdev_descs[vdev->type]);
			status = STATUS_SUCCESS;
		}
		break;
	case DeviceTextLocationInformation:
		if (!irp->IoStatus.Information) {
			irp->IoStatus.Information = (ULONG_PTR)libdrv_strdupW(vdev_locinfos[vdev->type]);
			status = STATUS_SUCCESS;
		}
		break;
	default:
		DBGI(DBG_PNP, "unsupported device text type: %u\n", irpstack->Parameters.QueryDeviceText.DeviceTextType);
		status = STATUS_SUCCESS;
		break;
	}

	return status;
}

static PAGEABLE BOOLEAN
process_pnp_common(pvdev_t vdev, PIRP irp, PIO_STACK_LOCATION irpstack, NTSTATUS *pstatus)
{
	switch (irpstack->MinorFunction) {
	case IRP_MN_QUERY_DEVICE_TEXT:
		*pstatus = vdev_query_device_text(vdev, irp);
		return TRUE;
	case IRP_MN_QUERY_INTERFACE:
		*pstatus = vdev_query_interface(vdev, irpstack);
		return TRUE;
	default:
		return FALSE;
	}
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

	if (process_pnp_common(vdev, irp, irpstack, &status)) {
		irp->IoStatus.Status = status;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		goto END;
	}

	switch (DEVOBJ_VDEV_TYPE(devobj)) {
	case VDEV_VHCI:
		status = vhci_pnp_vhci((pvhci_dev_t)vdev, irp, irpstack);
		break;
	case VDEV_HPDO:
		status = vhci_pnp_hpdo((phpdo_dev_t)vdev, irp, irpstack);
		break;
	case VDEV_VHUB:
		status = vhci_pnp_vhub((pvhub_dev_t)vdev, irp, irpstack);
		break;
	default:
		/* VDEV_VPDO */
		status = vhci_pnp_vpdo((pvpdo_dev_t)vdev, irp, irpstack);
		break;
	}

END:
	DBGI(DBG_GENERAL | DBG_PNP, "vhci_pnp(%s): Leave: irp:%p, status:%s\n", dbg_vdev_type(DEVOBJ_VDEV_TYPE(devobj)), irp, dbg_ntstatus(status));

	return status;
}

static PAGEABLE BOOLEAN
is_valid_vdev_hwid(PDEVICE_OBJECT devobj)
{
	LPWSTR	hwid;
	UNICODE_STRING	ustr_hwid_devprop, ustr_hwid;
	BOOLEAN	res;

	hwid = get_device_prop(devobj, DevicePropertyHardwareID, NULL);
	if (hwid == NULL)
		return FALSE;

	RtlInitUnicodeString(&ustr_hwid, HWID_VDEV);
	RtlInitUnicodeString(&ustr_hwid_devprop, hwid);

	res = RtlEqualUnicodeString(&ustr_hwid, &ustr_hwid_devprop, TRUE);
	ExFreePoolWithTag(hwid, USBIP_VHCI_POOL_TAG);
	return res;
}

static PAGEABLE pvdev_t
get_vdev_from_driver(PDRIVER_OBJECT drvobj, vdev_type_t type)
{
	PDEVICE_OBJECT	devobj = drvobj->DeviceObject;

	while (devobj) {
		if (DEVOBJ_VDEV_TYPE(devobj) == type)
			return DEVOBJ_TO_VDEV(devobj);
		devobj = devobj->NextDevice;
	}

	return NULL;
}

PAGEABLE NTSTATUS
vhci_add_device(__in PDRIVER_OBJECT drvobj, __in PDEVICE_OBJECT pdo)
{
	pvhci_dev_t	vhci;

	PAGED_CODE();

	if (!is_valid_vdev_hwid(pdo)) {
		DBGE(DBG_GENERAL | DBG_PNP, "invalid hw id\n");
		return STATUS_INVALID_PARAMETER;
	}

	vhci = (pvhci_dev_t)get_vdev_from_driver(drvobj, VDEV_VHCI);

	if (vhci == NULL) {
		phpdo_dev_t	hpdo = (phpdo_dev_t)get_vdev_from_driver(drvobj, VDEV_HPDO);

		return vhci_add_vhci(drvobj, pdo, hpdo);
	}
	else if (vhci->vhub != NULL) {
		DBGE(DBG_GENERAL | DBG_PNP, "vhub already exist\n");
		return STATUS_DEVICE_ALREADY_ATTACHED;
	}

	return vhci_add_vhub(vhci, pdo);
}