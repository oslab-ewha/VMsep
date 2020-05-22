#include "vhci.h"

#include "vhci_pnp.h"

extern NTSTATUS
reg_wmi(pvhci_dev_t vhci);
extern PAGEABLE NTSTATUS
dereg_wmi(pvhci_dev_t vhci);

static PAGEABLE void
relations_deref_devobj(PDEVICE_RELATIONS relations, ULONG idx)
{
	ObDereferenceObject(relations->Objects[idx]);
	if (idx < relations->Count - 1)
		RtlCopyMemory(relations->Objects + idx, relations->Objects + idx + 1, sizeof(PDEVICE_OBJECT) * (relations->Count - 1 - idx));
}

static PAGEABLE BOOLEAN
relations_has_devobj(PDEVICE_RELATIONS relations, PDEVICE_OBJECT devobj, BOOLEAN deref)
{
	ULONG	i;

	for (i = 0; i < relations->Count; i++) {
		if (relations->Objects[i] == devobj) {
			if (deref)
				relations_deref_devobj(relations, i);
			return TRUE;
		}
	}
	return FALSE;
}

PAGEABLE NTSTATUS
vhci_get_controller_name(pvhci_dev_t vhci, PVOID buffer, ULONG inlen, PULONG poutlen)
{
	PUSB_HUB_NAME	hub_name = (PUSB_HUB_NAME)buffer;
	ULONG	hub_namelen, drvkey_buflen;
	LPWSTR	drvkey;

	UNREFERENCED_PARAMETER(inlen);

	drvkey = get_device_prop(vhci->devobj_lower, DevicePropertyDriverKeyName, &drvkey_buflen);
	if (drvkey == NULL)
		return STATUS_UNSUCCESSFUL;

	hub_namelen = (ULONG)(sizeof(USB_HUB_NAME) + drvkey_buflen - sizeof(WCHAR));
	if (*poutlen < sizeof(USB_HUB_NAME)) {
		*poutlen = (ULONG)hub_namelen;
		ExFreePoolWithTag(drvkey, USBIP_VHCI_POOL_TAG);
		return STATUS_BUFFER_TOO_SMALL;
	}

	hub_name->ActualLength = hub_namelen;
	RtlStringCchCopyW(hub_name->HubName, (*poutlen - sizeof(USB_HUB_NAME) + sizeof(WCHAR)) / sizeof(WCHAR), drvkey);
	ExFreePoolWithTag(drvkey, USBIP_VHCI_POOL_TAG);

	if (*poutlen > hub_namelen)
		*poutlen = (ULONG)hub_namelen;
	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
vhci_get_bus_relations(pvhci_dev_t vhci, PDEVICE_RELATIONS *pdev_relations)
{
	BOOLEAN	hpdo_exist = TRUE;
	PDEVICE_RELATIONS	relations = *pdev_relations, relations_new;
	PDEVICE_OBJECT	devobj_hpdo;
	ULONG	size;

	if (vhci->hpdo->common.DevicePnPState == Deleted)
		hpdo_exist = FALSE;
	if (relations == NULL) {
		relations = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(PagedPool, sizeof(DEVICE_RELATIONS), USBIP_VHCI_POOL_TAG);
		if (relations == NULL) {
			DBGE(DBG_VHUB, "no relations will be reported: out of memory\n");
			return STATUS_INSUFFICIENT_RESOURCES;
		}
		relations->Count = 0;
	}
	if (!hpdo_exist) {
		*pdev_relations = relations;
		return STATUS_SUCCESS;
	}

	devobj_hpdo = TO_DEVOBJ(vhci->hpdo);
	if (relations->Count == 0) {
		*pdev_relations = relations;
		relations->Count = 1;
		relations->Objects[0] = devobj_hpdo;
		ObReferenceObject(devobj_hpdo);
		return STATUS_SUCCESS;
	}
	if (relations_has_devobj(relations, devobj_hpdo, !hpdo_exist)) {
		*pdev_relations = relations;
		return STATUS_SUCCESS;
	}

	// Need to allocate a new relations structure and add vhub to it
	size = sizeof(DEVICE_RELATIONS) + relations->Count * sizeof(PDEVICE_OBJECT);
	relations_new = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(PagedPool, size, USBIP_VHCI_POOL_TAG);
	if (relations_new == NULL) {
		DBGE(DBG_VHUB, "old relations will be used: out of memory\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	RtlCopyMemory(relations_new->Objects, relations->Objects, sizeof(PDEVICE_OBJECT) * relations->Count);
	relations_new->Count = relations->Count + 1;
	relations_new->Objects[relations->Count] = devobj_hpdo;
	ObReferenceObject(devobj_hpdo);

	ExFreePool(relations);
	*pdev_relations = relations_new;

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
start_vhci(pvhci_dev_t vhci)
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
	status = IoSetDeviceInterfaceState(&vhci->DevIntfVhci, TRUE);
	if (!NT_SUCCESS(status)) {
		DBGE(DBG_PNP, "failed to enable vhci device interface: %s\n", dbg_ntstatus(status));
		return status;
	}
	status = IoSetDeviceInterfaceState(&vhci->DevIntfUSBHC, TRUE);
	if (!NT_SUCCESS(status)) {
		IoSetDeviceInterfaceState(&vhci->DevIntfVhci, FALSE);
		DBGE(DBG_PNP, "failed to enable USB host controller device interface: %s\n", dbg_ntstatus(status));
		return status;
	}

	// Set the device power state to fully on. Also if this Start
	// is due to resource rebalance, you should restore the device
	// to the state it was before you stopped the device and relinquished
	// resources.

	vhci->common.DevicePowerState = PowerDeviceD0;
	powerState.DeviceState = PowerDeviceD0;
	PoSetPowerState(vhci->common.Self, DevicePowerState, powerState);

	SET_NEW_PNP_STATE(vhci, Started);

	// Register with WMI
	status = reg_wmi(vhci);
	if (!NT_SUCCESS(status)) {
		DBGE(DBG_VHCI, "start_vhci: reg_wmi failed: %s\n", dbg_ntstatus(status));
	}

	return status;
}

PAGEABLE void
remove_vhci(pvhci_dev_t vhci)
{
	PAGED_CODE();

	ASSERT(vhci->vhub == NULL);
	SET_NEW_PNP_STATE(vhci, Deleted);

#if 0 ////TODO
	// Wait for all outstanding requests to complete.
	// We need two decrements here, one for the increment in
	// the beginning of this function, the other for the 1-biased value of
	// OutstandingIO.

	dec_io_vhci(vhci);

	// The requestCount is at least one here (is 1-biased)

	dec_io_vhci(vhci);

	KeWaitForSingleObject(&vhci->RemoveEvent, Executive, KernelMode, FALSE, NULL);

	vhub_remove_all_vpdos(vhci->vhub);
#endif
	IoSetDeviceInterfaceState(&vhci->DevIntfVhci, FALSE);
	IoSetDeviceInterfaceState(&vhci->DevIntfUSBHC, FALSE);
	RtlFreeUnicodeString(&vhci->DevIntfVhci);
	RtlFreeUnicodeString(&vhci->DevIntfUSBHC);

	// Inform WMI to remove this DeviceObject from its list of providers.
	dereg_wmi(vhci);

	// Detach from the underlying devices.
	IoDetachDevice(vhci->devobj_lower);

	DBGI(DBG_PNP, "deleting vhci device object: 0x%p\n", vhci->common.Self);

	IoDeleteDevice(vhci->common.Self);
}