#include "vhci.h"

#include "usbip_vhci_api.h"
#include "vhci_pnp.h"

extern void complete_pending_irp(pvpdo_dev_t vpdo);
extern void complete_pending_read_irp(pvpdo_dev_t vpdo);

extern BOOLEAN start_hpdo(phpdo_dev_t hpdo);
extern void invalidate_hpdo(phpdo_dev_t hpdo);

PAGEABLE pvpdo_dev_t
vhub_find_vpdo(pvhub_dev_t vhub, unsigned port)
{
	PLIST_ENTRY	entry;

	ExAcquireFastMutex(&vhub->Mutex);

	for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		pvpdo_dev_t	vpdo = CONTAINING_RECORD(entry, vpdo_dev_t, Link);

		if (vpdo->port == port) {
			vdev_add_ref((pvdev_t)vpdo);
			ExReleaseFastMutex(&vhub->Mutex);
			return vpdo;
		}
	}

	ExReleaseFastMutex(&vhub->Mutex);

	return NULL;
}

PAGEABLE BOOLEAN
vhub_is_empty_port(pvhub_dev_t vhub, ULONG port)
{
	pvpdo_dev_t	vpdo;

	vpdo = vhub_find_vpdo(vhub, port);
	if (vpdo == NULL)
		return TRUE;
	if (vpdo->common.DevicePnPState == SurpriseRemovePending) {
		vdev_del_ref((pvdev_t)vpdo);
		return TRUE;
	}

	vdev_del_ref((pvdev_t)vpdo);
	return FALSE;
}

PAGEABLE void
vhub_attach_vpdo(pvhub_dev_t vhub, pvpdo_dev_t vpdo)
{
	ExAcquireFastMutex(&vhub->Mutex);

	InsertTailList(&vhub->head_vpdo, &vpdo->Link);
	vhub->n_vpdos++;
	if (vpdo->plugged)
		vhub->n_vpdos_plugged++;

	ExReleaseFastMutex(&vhub->Mutex);
}

PAGEABLE void
vhub_detach_vpdo(pvhub_dev_t vhub, pvpdo_dev_t vpdo)
{
	ExAcquireFastMutex(&vhub->Mutex);

	RemoveEntryList(&vpdo->Link);
	InitializeListHead(&vpdo->Link);
	ASSERT(vhub->n_vpdos > 0);
	vhub->n_vpdos--;

	ExReleaseFastMutex(&vhub->Mutex);
}

static pvpdo_dev_t
find_managed_vpdo(pvhub_dev_t vhub, PDEVICE_OBJECT devobj)
{
	PLIST_ENTRY	entry;

	for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		pvpdo_dev_t	vpdo = CONTAINING_RECORD(entry, vpdo_dev_t, Link);
		if (vpdo->common.Self == devobj) {
			return vpdo;
		}
	}
	return NULL;
}

static BOOLEAN
is_in_dev_relations(PDEVICE_OBJECT devobjs[], ULONG n_counts, pvpdo_dev_t vpdo)
{
	ULONG	i;

	for (i = 0; i < n_counts; i++) {
		if (vpdo->common.Self == devobjs[i]) {
			return TRUE;
		}
	}
	return FALSE;
}

PAGEABLE NTSTATUS
vhub_get_bus_relations(pvhub_dev_t vhub, PDEVICE_RELATIONS *pdev_relations)
{
	PDEVICE_RELATIONS	relations_old = *pdev_relations, relations;
	ULONG			length, n_olds = 0, n_news = 0;
	PLIST_ENTRY		entry;
	ULONG	i;

	ExAcquireFastMutex(&vhub->Mutex);

	if (relations_old)
		n_olds = relations_old->Count;

	// Need to allocate a new relations structure and add our vpdos to it
	length = sizeof(DEVICE_RELATIONS) + (vhub->n_vpdos_plugged + n_olds - 1) * sizeof(PDEVICE_OBJECT);

	relations = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(PagedPool, length, USBIP_VHCI_POOL_TAG);
	if (relations == NULL) {
		DBGE(DBG_VHUB, "failed to allocate a new relation: out of memory\n");

		ExReleaseFastMutex(&vhub->Mutex);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	for (i = 0; i < n_olds; i++) {
		pvpdo_dev_t	vpdo;
		PDEVICE_OBJECT	devobj = relations_old->Objects[i];
		vpdo = find_managed_vpdo(vhub, devobj);
		if (vpdo == NULL || vpdo->plugged) {
			relations->Objects[n_news] = devobj;
			n_news++;
		}
		else {
			vpdo->ReportedMissing = TRUE;
			ObDereferenceObject(devobj);
		}
	}

	for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		pvpdo_dev_t	vpdo = CONTAINING_RECORD(entry, vpdo_dev_t, Link);

		if (is_in_dev_relations(relations->Objects, n_news, vpdo))
			continue;
		if (vpdo->plugged) {
			relations->Objects[n_news] = vpdo->common.Self;
			n_news++;
			ObReferenceObject(vpdo->common.Self);
		} else {
			vpdo->ReportedMissing = TRUE;
		}
	}

	relations->Count = n_news;

	DBGI(DBG_VHUB, "vhub vpdos: total:%u,plugged:%u: bus relations: old:%u,new:%u\n", vhub->n_vpdos, vhub->n_vpdos_plugged, n_olds, n_news);

	if (relations_old)
		ExFreePool(relations_old);

	ExReleaseFastMutex(&vhub->Mutex);

	*pdev_relations = relations;
	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
vhub_get_information_ex(pvhub_dev_t vhub, PUSB_HUB_INFORMATION_EX pinfo)
{
	UNREFERENCED_PARAMETER(vhub);

	pinfo->HubType = UsbRootHub;
	pinfo->HighestPortNumber = (USHORT)vhub->n_max_ports;
	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
vhub_get_capabilities_ex(pvhub_dev_t vhub, PUSB_HUB_CAPABILITIES_EX pinfo)
{
	UNREFERENCED_PARAMETER(vhub);

	pinfo->CapabilityFlags.ul = 0xffffffff;
	pinfo->CapabilityFlags.HubIsHighSpeedCapable = FALSE;
	pinfo->CapabilityFlags.HubIsHighSpeed = FALSE;
	pinfo->CapabilityFlags.HubIsMultiTtCapable = TRUE;
	pinfo->CapabilityFlags.HubIsMultiTt = TRUE;
	pinfo->CapabilityFlags.HubIsRoot = TRUE;
	pinfo->CapabilityFlags.HubIsBusPowered = FALSE;

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
vhub_get_port_connector_properties(pvhub_dev_t vhub, PUSB_PORT_CONNECTOR_PROPERTIES pinfo, PULONG poutlen)
{
	if (pinfo->ConnectionIndex > vhub->n_max_ports)
		return STATUS_INVALID_PARAMETER;
	if (*poutlen < sizeof(USB_PORT_CONNECTOR_PROPERTIES))
		return STATUS_BUFFER_TOO_SMALL;

	pinfo->ActualLength = sizeof(USB_PORT_CONNECTOR_PROPERTIES);
	pinfo->UsbPortProperties.ul = 0xffffffff;
	pinfo->UsbPortProperties.PortIsUserConnectable = TRUE;
	pinfo->UsbPortProperties.PortIsDebugCapable = TRUE;
	pinfo->UsbPortProperties.PortHasMultipleCompanions = FALSE;
	pinfo->UsbPortProperties.PortConnectorIsTypeC = FALSE;
	pinfo->CompanionIndex = 0;
	pinfo->CompanionPortNumber = 0;
	pinfo->CompanionHubSymbolicLinkName[0] = L'\0';

	*poutlen = sizeof(USB_PORT_CONNECTOR_PROPERTIES);

	return STATUS_SUCCESS;
}

static PAGEABLE void
mark_unplugged(pvhub_dev_t vhub, pvpdo_dev_t vpdo)
{
	if (vpdo->plugged) {
		vpdo->plugged = FALSE;
		ASSERT(vhub->n_vpdos_plugged > 0);
		vhub->n_vpdos_plugged--;
	}
	else {
		DBGE(DBG_VHUB, "vpdo already unplugged: port: %u\n", vpdo->port);
	}
}

PAGEABLE void
vhub_mark_unplugged_vpdo(pvhub_dev_t vhub, pvpdo_dev_t vpdo)
{
	ExAcquireFastMutex(&vhub->Mutex);

	mark_unplugged(vhub, vpdo);

	ExReleaseFastMutex(&vhub->Mutex);
}

PAGEABLE void
vhub_remove_all_vpdos(pvhub_dev_t vhub)
{
	PLIST_ENTRY	entry, nextEntry;

	// Typically the system removes all the  children before
	// removing the parent vhub. If for any reason child vpdo's are
	// still present we will destroy them explicitly, with one exception -
	// we will not delete the vpdos that are in SurpriseRemovePending state.

	ExAcquireFastMutex(&vhub->Mutex);

	for (entry = vhub->head_vpdo.Flink, nextEntry = entry->Flink; entry != &vhub->head_vpdo; entry = nextEntry, nextEntry = entry->Flink) {
		pvpdo_dev_t	vpdo = CONTAINING_RECORD(entry, vpdo_dev_t, Link);

		RemoveEntryList(&vpdo->Link);
		if (vpdo->common.DevicePnPState == SurpriseRemovePending) {
			// We will reinitialize the list head so that we
			// wouldn't barf when we try to delink this vpdo from
			// the parent's vpdo list, when the system finally
			// removes the vpdo. Let's also not forget to set the
			// ReportedMissing flag to cause the deletion of the vpdo.
			DBGI(DBG_VHUB, "\tFound a surprise removed device: 0x%p\n", vpdo->common.Self);
			InitializeListHead(&vpdo->Link);
			vpdo->vhub = NULL;
			vpdo->ReportedMissing = TRUE;
			continue;
		}
		vhub->n_vpdos--;
		destroy_vpdo(vpdo);
	}

	ExReleaseFastMutex(&vhub->Mutex);
}

PAGEABLE NTSTATUS
vhub_unplug_vpdo(pvhub_dev_t vhub, ULONG port, BOOLEAN is_eject)
{
	BOOLEAN		found = FALSE;
	PLIST_ENTRY	entry;

	ExAcquireFastMutex(&vhub->Mutex);
	if (vhub->n_vpdos == 0) {
		ExReleaseFastMutex(&vhub->Mutex);
		return STATUS_NO_SUCH_DEVICE;
	}

	for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		pvpdo_dev_t	vpdo = CONTAINING_RECORD(entry, vpdo_dev_t, Link);

		if (port == 0 || port == vpdo->port) {
			if (!is_eject) {
				DBGI(DBG_VHUB, "plugging out: port: %u\n", vpdo->port);
				mark_unplugged(vhub, vpdo);
				complete_pending_read_irp(vpdo);
			}
			else {
				IoRequestDeviceEject(vpdo->common.Self);
			}
			found = TRUE;
			if (port != 0)
				break;
		}
	}

	ExReleaseFastMutex(&vhub->Mutex);

	if (!found)
		return STATUS_INVALID_PARAMETER;
	return STATUS_SUCCESS;
}

PAGEABLE void
vhub_invalidate_unplugged_vpdos(pvhub_dev_t vhub)
{
	PLIST_ENTRY	entry;

	ExAcquireFastMutex(&vhub->Mutex);

	for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		pvpdo_dev_t	vpdo = CONTAINING_RECORD(entry, vpdo_dev_t, Link);

		if (!vpdo->plugged) {
			complete_pending_irp(vpdo);
			SET_NEW_PNP_STATE(vpdo, PNP_DEVICE_REMOVED);
			IoInvalidateDeviceState(vpdo->common.Self);
		}
	}

	ExReleaseFastMutex(&vhub->Mutex);
}

PAGEABLE NTSTATUS
vhci_get_ports_status(ioctl_usbip_vhci_get_ports_status *st, pvhub_dev_t vhub)
{
	pvpdo_dev_t	vpdo;
	PLIST_ENTRY		entry;

	PAGED_CODE();

	DBGI(DBG_VHUB, "get ports status\n");

	RtlZeroMemory(st, sizeof(*st));
	ExAcquireFastMutex(&vhub->Mutex);

	for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		vpdo = CONTAINING_RECORD (entry, vpdo_dev_t, Link);
		if (vpdo->port > 127 || vpdo->port == 0) {
			DBGE(DBG_VHUB, "strange error");
			continue;
		}
		if (st->u.max_used_port < (char)vpdo->port)
			st->u.max_used_port = (char)vpdo->port;
		st->u.port_status[vpdo->port] = 1;
	}
	ExReleaseFastMutex(&vhub->Mutex);
	return STATUS_SUCCESS;
}

PAGEABLE BOOLEAN
start_vhub(pvhub_dev_t vhub)
{
	PAGED_CODE();

	if (!start_hpdo(vhub->vhci->hpdo))
		return FALSE;

	vhub->common.DevicePowerState = PowerDeviceD0;
	SET_NEW_PNP_STATE(vhub, Started);

	return TRUE;
}

PAGEABLE void
stop_vhub(pvhub_dev_t vhub)
{
	PAGED_CODE();

	KeWaitForSingleObject(&vhub->StopEvent, Executive, KernelMode, FALSE, NULL);
	SET_NEW_PNP_STATE(vhub, Stopped);
}

PAGEABLE void
remove_vhub(pvhub_dev_t vhub)
{
	invalidate_hpdo(vhub->vhci->hpdo);
	IoDetachDevice(vhub->devobj_lower);

	SET_NEW_PNP_STATE(vhub, Deleted);
	vhub_remove_all_vpdos(vhub);

	DBGI(DBG_PNP, "Deleting vhub device object: 0x%p\n", TO_DEVOBJ(vhub));

	IoDeleteDevice(TO_DEVOBJ(vhub));
	vhub->vhci->vhub = NULL;
}