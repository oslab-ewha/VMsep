#include "vhci.h"

#include "vhci_dev.h"
#include "usbip_vhci_api.h"
#include "vhci_irp.h"

#define DEVID_VHCI	HWID_VHCI
#define DEVID_VHUB	L"USB\\ROOT_HUB"

#define HWIDS_VHCI	HWID_VHCI L"\0"
#define HWIDS_VHUB	HWID_VHUB L"\0USB\\ROOT_HUB&VID1209&PID8250\0USB\\ROOT_HUB\0"

/* Device with zero class/subclass/protocol */
#define IS_ZERO_CLASS(vpdo)	((vpdo)->usbclass == 0x00 && (vpdo)->subclass == 0x00 && (vpdo)->protocol == 0x00 && (vpdo)->inum > 1)
/* Device with IAD(Interface Association Descriptor) */
#define IS_IAD_DEVICE(vpdo)	((vpdo)->usbclass == 0xef && (vpdo)->subclass == 0x02 && (vpdo)->protocol == 0x01)

static LPCWSTR vdev_devids[] = {
	NULL, DEVID_VHCI, NULL, DEVID_VHUB, NULL, (LPCWSTR)TRUE
};

static ULONG vdev_devid_lens[] = {
	0, sizeof(DEVID_VHCI), 0, sizeof(DEVID_VHUB), 0, 22 * sizeof(wchar_t)
};

static LPCWSTR vdev_hwids[] = {
	NULL, HWIDS_VHCI, NULL, HWIDS_VHUB, NULL, (LPCWSTR)TRUE
};

static ULONG vdev_hwids_lens[] = {
	0, sizeof(HWIDS_VHCI), 0, sizeof(HWIDS_VHUB), 0, 54 * sizeof(wchar_t)
};

static NTSTATUS
setup_device_id(pvdev_t vdev, PIRP irp)
{
	PWCHAR	id_dev;

	if (vdev_devids[vdev->type] == NULL) {
		DBGI(DBG_PNP, "%s: query device id: NOT SUPPORTED\n", dbg_vdev_type(vdev->type));
		return STATUS_NOT_SUPPORTED;
	}
	id_dev = ExAllocatePoolWithTag(PagedPool, vdev_devid_lens[vdev->type], USBIP_VHCI_POOL_TAG);
	if (id_dev == NULL) {
		DBGE(DBG_PNP, "%s: query device id: out of memory\n", dbg_vdev_type(vdev->type));
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	if (vdev->type == VDEV_VPDO) {
		pvpdo_dev_t	vpdo = (pvpdo_dev_t)vdev;
		RtlStringCchPrintfW(id_dev, 22, L"USB\\VID_%04hx&PID_%04hx", vpdo->vendor, vpdo->product);
	}
	else
		RtlStringCchCopyW(id_dev, vdev_devid_lens[vdev->type], vdev_devids[vdev->type]);

	irp->IoStatus.Information = (ULONG_PTR)id_dev;

	DBGI(DBG_PNP, "%s: device id: %S\n", dbg_vdev_type(vdev->type), id_dev);

	return STATUS_SUCCESS;
}

static NTSTATUS
setup_hw_ids(pvdev_t vdev, PIRP irp)
{
	PWCHAR	ids_hw;

	if (vdev_hwids[vdev->type] == NULL) {
		DBGI(DBG_PNP, "%s: query hw ids: NOT SUPPORTED%s\n", dbg_vdev_type(vdev->type));
		return STATUS_NOT_SUPPORTED;
	}
	ids_hw = ExAllocatePoolWithTag(PagedPool, vdev_hwids_lens[vdev->type], USBIP_VHCI_POOL_TAG);
	if (ids_hw == NULL) {
		DBGE(DBG_PNP, "%s: query hw ids: out of memory\n", dbg_vdev_type(vdev->type));
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	if (vdev->type == VDEV_VPDO) {
		pvpdo_dev_t	vpdo = (pvpdo_dev_t)vdev;
		RtlStringCchPrintfW(ids_hw, 31, L"USB\\VID_%04hx&PID_%04hx&REV_%04hx", vpdo->vendor, vpdo->product, vpdo->revision);
		RtlStringCchPrintfW(ids_hw + 31, 22, L"USB\\VID_%04hx&PID_%04hx", vpdo->vendor, vpdo->product);
		ids_hw[53] = L'\0';
	}
	else {
		RtlCopyMemory(ids_hw, vdev_hwids[vdev->type], vdev_hwids_lens[vdev->type]);
	}

	irp->IoStatus.Information = (ULONG_PTR)ids_hw;

	DBGI(DBG_PNP, "%s: hw id: %S\n", dbg_vdev_type(vdev->type), ids_hw);

	return STATUS_SUCCESS;
}

static NTSTATUS
setup_inst_id(pvdev_t vdev, PIRP irp)
{
	pvpdo_dev_t	vpdo;
	PWCHAR	id_inst;

	if (vdev->type != VDEV_VPDO) {
		DBGI(DBG_PNP, "%s: query instance id: NOT SUPPORTED\n", dbg_vdev_type(vdev->type));
		return STATUS_NOT_SUPPORTED;
	}

	vpdo = (pvpdo_dev_t)vdev;

	id_inst = ExAllocatePoolWithTag(PagedPool, (MAX_VHCI_INSTANCE_ID + 1) * sizeof(wchar_t), USBIP_VHCI_POOL_TAG);
	if (id_inst == NULL) {
		DBGE(DBG_PNP, "vpdo: query instance id: out of memory\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	if (vpdo->winstid != NULL)
		RtlStringCchCopyW(id_inst, MAX_VHCI_INSTANCE_ID + 1, vpdo->winstid);
	else
		RtlStringCchPrintfW(id_inst, 5, L"%04hx", vpdo->port);

	irp->IoStatus.Information = (ULONG_PTR)id_inst;

	DBGI(DBG_PNP, "vpdo: instance id: %S\n", id_inst);

	return STATUS_SUCCESS;
}

static NTSTATUS
setup_compat_ids(pvdev_t vdev, PIRP irp)
{
	pvpdo_dev_t	vpdo;
	PWCHAR	ids_compat;

	if (vdev->type != VDEV_VPDO) {
		DBGI(DBG_PNP, "%s: query compatible id: NOT SUPPORTED\n", dbg_vdev_type(vdev->type));
		return STATUS_NOT_SUPPORTED;
	}

	vpdo = (pvpdo_dev_t)vdev;

	ids_compat = ExAllocatePoolWithTag(PagedPool, 86 * sizeof(wchar_t), USBIP_VHCI_POOL_TAG);
	if (ids_compat == NULL) {
		DBGE(DBG_PNP, "vpdo: query compatible id: out of memory\n");
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

PAGEABLE NTSTATUS
pnp_query_id(pvdev_t vdev, PIRP irp, PIO_STACK_LOCATION irpstack)
{
	NTSTATUS	status = STATUS_NOT_SUPPORTED;

	DBGI(DBG_PNP, "%s: query id: %s\n", dbg_vdev_type(vdev->type), dbg_bus_query_id_type(irpstack->Parameters.QueryId.IdType));

	PAGED_CODE();

	switch (irpstack->Parameters.QueryId.IdType) {
	case BusQueryDeviceID:
		status = setup_device_id(vdev, irp);
		break;
	case BusQueryInstanceID:
		status = setup_inst_id(vdev, irp);
		break;
	case BusQueryHardwareIDs:
		status = setup_hw_ids(vdev, irp);
		break;
	case BusQueryCompatibleIDs:
		status = setup_compat_ids(vdev, irp);
		break;
	default:
		DBGI(DBG_PNP, "%s: unhandled query id: %s\n", dbg_vdev_type(vdev->type), dbg_bus_query_id_type(irpstack->Parameters.QueryId.IdType));
		break;
	}

	return irp_done(irp, status);
}