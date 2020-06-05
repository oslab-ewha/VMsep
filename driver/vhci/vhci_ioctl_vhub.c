#include "vhci.h"

#include "vhci_dev.h"
#include "vhci_pnp.h"

extern NTSTATUS
vhub_get_roothub_name(pvhub_dev_t vhub, PVOID buffer, ULONG inlen, PULONG poutlen);

extern NTSTATUS
vpdo_get_nodeconn_info(pvpdo_dev_t vpdo, PUSB_NODE_CONNECTION_INFORMATION nodeconn, PULONG poutlen);

extern NTSTATUS
vpdo_get_nodeconn_info_ex(pvpdo_dev_t vpdo, PUSB_NODE_CONNECTION_INFORMATION_EX nodeconn, PULONG poutlen);

extern NTSTATUS
vpdo_get_nodeconn_info_ex_v2(pvpdo_dev_t vpdo, PUSB_NODE_CONNECTION_INFORMATION_EX_V2 nodeconn, PULONG poutlen);

extern NTSTATUS
vpdo_get_dsc_from_nodeconn(pvpdo_dev_t vpdo, PIRP irp, PUSB_DESCRIPTOR_REQUEST dsc_req, PULONG poutlen);

extern NTSTATUS
vhub_get_information_ex(pvhub_dev_t vhub, PUSB_HUB_INFORMATION_EX pinfo);

extern NTSTATUS
vhub_get_capabilities_ex(pvhub_dev_t vhub, PUSB_HUB_CAPABILITIES_EX pinfo);

extern NTSTATUS
vhub_get_port_connector_properties(pvhub_dev_t vhub, PUSB_PORT_CONNECTOR_PROPERTIES pinfo, PULONG poutlen);

extern void
vhub_get_hub_descriptor(pvhub_dev_t vhub, PUSB_HUB_DESCRIPTOR pdesc);

static PAGEABLE NTSTATUS
get_node_info(pvhub_dev_t vhub, PVOID buffer, ULONG inlen, PULONG poutlen)
{
	PUSB_NODE_INFORMATION	nodeinfo = (PUSB_NODE_INFORMATION)buffer;

	if (inlen < sizeof(USB_NODE_INFORMATION)) {
		*poutlen = sizeof(USB_NODE_INFORMATION);
		return STATUS_BUFFER_TOO_SMALL;
	}

	if (nodeinfo->NodeType == UsbMIParent)
		nodeinfo->u.MiParentInformation.NumberOfInterfaces = 1;
	else {
		vhub_get_hub_descriptor(vhub, &nodeinfo->u.HubInformation.HubDescriptor);
		nodeinfo->u.HubInformation.HubIsBusPowered = FALSE;
	}

	return STATUS_SUCCESS;
}

static PAGEABLE NTSTATUS
get_nodeconn_info(pvhub_dev_t vhub, PVOID buffer, ULONG inlen, PULONG poutlen)
{
	PUSB_NODE_CONNECTION_INFORMATION	conninfo = (PUSB_NODE_CONNECTION_INFORMATION)buffer;
	pvpdo_dev_t	vpdo;
	NTSTATUS	status;

	if (inlen < sizeof(USB_NODE_CONNECTION_INFORMATION)) {
		*poutlen = sizeof(USB_NODE_CONNECTION_INFORMATION);
		return STATUS_BUFFER_TOO_SMALL;
	}
	if (conninfo->ConnectionIndex > vhub->n_max_ports)
		return STATUS_NO_SUCH_DEVICE;
	vpdo = vhub_find_vpdo(vhub, conninfo->ConnectionIndex);
	status = vpdo_get_nodeconn_info(vpdo, conninfo, poutlen);
	if (vpdo != NULL)
		vdev_del_ref((pvdev_t)vpdo);
	return status;
}

static PAGEABLE NTSTATUS
get_nodeconn_info_ex(pvhub_dev_t vhub, PVOID buffer, ULONG inlen, PULONG poutlen)
{
	PUSB_NODE_CONNECTION_INFORMATION_EX	conninfo = (PUSB_NODE_CONNECTION_INFORMATION_EX)buffer;
	pvpdo_dev_t	vpdo;
	NTSTATUS	status;

	if (inlen < sizeof(PUSB_NODE_CONNECTION_INFORMATION_EX)) {
		*poutlen = sizeof(PUSB_NODE_CONNECTION_INFORMATION_EX);
		return STATUS_BUFFER_TOO_SMALL;
	}
	if (conninfo->ConnectionIndex > vhub->n_max_ports)
		return STATUS_NO_SUCH_DEVICE;
	vpdo = vhub_find_vpdo(vhub, conninfo->ConnectionIndex);
	status = vpdo_get_nodeconn_info_ex(vpdo, conninfo, poutlen);
	if (vpdo != NULL)
		vdev_del_ref((pvdev_t)vpdo);
	return status;
}

static PAGEABLE NTSTATUS
get_nodeconn_info_ex_v2(pvhub_dev_t vhub, PVOID buffer, ULONG inlen, PULONG poutlen)
{
	PUSB_NODE_CONNECTION_INFORMATION_EX_V2	conninfo = (PUSB_NODE_CONNECTION_INFORMATION_EX_V2)buffer;
	pvpdo_dev_t	vpdo;
	NTSTATUS	status;

	if (inlen < sizeof(PUSB_NODE_CONNECTION_INFORMATION_EX_V2)) {
		*poutlen = sizeof(PUSB_NODE_CONNECTION_INFORMATION_EX_V2);
		return STATUS_BUFFER_TOO_SMALL;
	}
	if (conninfo->ConnectionIndex > vhub->n_max_ports)
		return STATUS_NO_SUCH_DEVICE;
	vpdo = vhub_find_vpdo(vhub, conninfo->ConnectionIndex);
	status = vpdo_get_nodeconn_info_ex_v2(vpdo, conninfo, poutlen);
	if (vpdo != NULL)
		vdev_del_ref((pvdev_t)vpdo);
	return status;
}

static PAGEABLE NTSTATUS
get_descriptor_from_nodeconn(pvhub_dev_t vhub, PIRP irp, PVOID buffer, ULONG inlen, PULONG poutlen)
{
	PUSB_DESCRIPTOR_REQUEST	dsc_req = (PUSB_DESCRIPTOR_REQUEST)buffer;
	pvpdo_dev_t	vpdo;
	NTSTATUS	status;

	if (inlen < sizeof(USB_DESCRIPTOR_REQUEST)) {
		*poutlen = sizeof(USB_DESCRIPTOR_REQUEST);
		return STATUS_BUFFER_TOO_SMALL;
	}

	vpdo = vhub_find_vpdo(vhub, dsc_req->ConnectionIndex);
	if (vpdo == NULL)
		return STATUS_NO_SUCH_DEVICE;

	status = vpdo_get_dsc_from_nodeconn(vpdo, irp, dsc_req, poutlen);
	vdev_del_ref((pvdev_t)vpdo);
	return status;
}

static PAGEABLE NTSTATUS
get_hub_information_ex(pvhub_dev_t vhub, PVOID buffer, PULONG poutlen)
{
	PUSB_HUB_INFORMATION_EX	pinfo = (PUSB_HUB_INFORMATION_EX)buffer;

	if (*poutlen < sizeof(USB_HUB_INFORMATION_EX))
		return STATUS_BUFFER_TOO_SMALL;
	return vhub_get_information_ex(vhub, pinfo);
}

static PAGEABLE NTSTATUS
get_hub_capabilities_ex(pvhub_dev_t vhub, PVOID buffer, PULONG poutlen)
{
	PUSB_HUB_CAPABILITIES_EX	pinfo = (PUSB_HUB_CAPABILITIES_EX)buffer;

	if (*poutlen < sizeof(USB_HUB_CAPABILITIES_EX))
		return STATUS_BUFFER_TOO_SMALL;
	return vhub_get_capabilities_ex(vhub, pinfo);
}

static PAGEABLE NTSTATUS
get_port_connector_properties(pvhub_dev_t vhub, PVOID buffer, ULONG inlen, PULONG poutlen)
{
	PUSB_PORT_CONNECTOR_PROPERTIES	pinfo = (PUSB_PORT_CONNECTOR_PROPERTIES)buffer;

	if (inlen < sizeof(USB_PORT_CONNECTOR_PROPERTIES))
		return STATUS_BUFFER_TOO_SMALL;
	return vhub_get_port_connector_properties(vhub, pinfo, poutlen);
}

PAGEABLE NTSTATUS
vhci_ioctl_vhub(pvhub_dev_t vhub, PIRP irp, ULONG ioctl_code, PVOID buffer, ULONG inlen, ULONG *poutlen)
{
	NTSTATUS	status = STATUS_INVALID_DEVICE_REQUEST;

	switch (ioctl_code) {
	case IOCTL_USB_GET_NODE_INFORMATION:
		status = get_node_info(vhub, buffer, inlen, poutlen);
		break;
	case IOCTL_USB_GET_NODE_CONNECTION_INFORMATION:
		status = get_nodeconn_info(vhub, buffer, inlen, poutlen);
		break;
	case IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX:
		status = get_nodeconn_info_ex(vhub, buffer, inlen, poutlen);
		break;
	case IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2:
		status = get_nodeconn_info_ex_v2(vhub, buffer, inlen, poutlen);
		break;
	case IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION:
		status = get_descriptor_from_nodeconn(vhub, irp, buffer, inlen, poutlen);
		break;
	case IOCTL_USB_GET_HUB_INFORMATION_EX:
		status = get_hub_information_ex(vhub, buffer, poutlen);
		break;
	case IOCTL_USB_GET_HUB_CAPABILITIES_EX:
		status = get_hub_capabilities_ex(vhub, buffer, poutlen);
		break;
	case IOCTL_USB_GET_PORT_CONNECTOR_PROPERTIES:
		status = get_port_connector_properties(vhub, buffer, inlen, poutlen);
		break;
	default:
		DBGE(DBG_IOCTL, "unhandled vhub ioctl: %s\n", dbg_vhci_ioctl_code(ioctl_code));
		break;
	}

	return status;
}