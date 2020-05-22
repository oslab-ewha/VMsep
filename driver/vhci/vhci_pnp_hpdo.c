#include "vhci.h"

#include <wdmguid.h>

#include "usbip_vhci_api.h"
#include "vhci_pnp.h"
#include "usbreq.h"

extern void remove_hpdo(phpdo_dev_t hpdo);

static NTSTATUS
setup_hpdo_device_id(PIRP irp)
{
	PWCHAR	id_dev;

	id_dev = ExAllocatePoolWithTag(PagedPool, sizeof(HWID_VDEV), USBIP_VHCI_POOL_TAG);
	if (id_dev == NULL) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	RtlStringCchCopyW(id_dev, WTEXT_LEN(HWID_VDEV), HWID_VDEV);
	irp->IoStatus.Information = (ULONG_PTR)id_dev;

	DBGI(DBG_PNP, "hpdo: device id: %S\n", id_dev);

	return STATUS_SUCCESS;
}

static NTSTATUS
setup_hpdo_hw_ids(PIRP irp)
{
	PWCHAR	ids_hw;

	ids_hw = ExAllocatePoolWithTag(PagedPool, sizeof(HWID_VDEV) + sizeof(WCHAR), USBIP_VHCI_POOL_TAG);
	if (ids_hw == NULL) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	RtlStringCchCopyW(ids_hw, WTEXT_LEN(HWID_VDEV), HWID_VDEV);
	ids_hw[WTEXT_LEN(HWID_VDEV)] = L'\0';
	irp->IoStatus.Information = (ULONG_PTR)ids_hw;

	DBGI(DBG_PNP, "hpdo: hw id: %S\n", ids_hw);

	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS
hpdo_query_id(phpdo_dev_t hpdo, PIRP irp, PIO_STACK_LOCATION irpstack)
{
	NTSTATUS	status = STATUS_NOT_SUPPORTED;

	UNREFERENCED_PARAMETER(hpdo);

	DBGI(DBG_PNP, "hpdo: query id: %s\n", dbg_bus_query_id_type(irpstack->Parameters.QueryId.IdType));

	PAGED_CODE();

	switch (irpstack->Parameters.QueryId.IdType) {
	case BusQueryDeviceID:
		status = setup_hpdo_device_id(irp);
		break;
	case BusQueryHardwareIDs:
		status = setup_hpdo_hw_ids(irp);
		break;
	default:
		DBGI(DBG_PNP, "unhandled query id: %s\n", dbg_bus_query_id_type(irpstack->Parameters.QueryId.IdType));
		break;
	}
	return status;
}

PAGEABLE NTSTATUS
vhci_pnp_hpdo(phpdo_dev_t hpdo, PIRP irp, PIO_STACK_LOCATION irpstack)
{
	NTSTATUS	status = STATUS_SUCCESS;

	PAGED_CODE ();

	switch (irpstack->MinorFunction) {
	case IRP_MN_QUERY_STOP_DEVICE:
		SET_NEW_PNP_STATE(hpdo, StopPending);
		break;
	case IRP_MN_CANCEL_STOP_DEVICE:
		if (hpdo->common.DevicePnPState == StopPending) {
			// We did receive a query-stop, so restore.
			RESTORE_PREVIOUS_PNP_STATE(hpdo);
			ASSERT(hpdo->common.DevicePnPState == Started);
		}
		break;
	case IRP_MN_REMOVE_DEVICE:
		remove_hpdo(hpdo);
		break;
	case IRP_MN_CANCEL_REMOVE_DEVICE:
		if (hpdo->common.DevicePnPState == RemovePending) {
			RESTORE_PREVIOUS_PNP_STATE(hpdo);
		}
		break;
	case IRP_MN_SURPRISE_REMOVAL:
		SET_NEW_PNP_STATE(hpdo, SurpriseRemovePending);
		break;
	case IRP_MN_QUERY_ID:
		status = hpdo_query_id(hpdo, irp, irpstack);
		break;
	case IRP_MN_QUERY_DEVICE_TEXT:
		status = vdev_query_device_text((pvdev_t)hpdo, irp);
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