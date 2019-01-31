#include "vhci.h"

#include "vhci_devconf.h"
#include "usbip_vhci_api.h"
#include "usbip_proto.h"

#define NEXT_USBD_INTERFACE_INFO(info_intf)	(USBD_INTERFACE_INFORMATION *)((PUINT8)(info_intf + 1) - \
	(1 * sizeof(USBD_PIPE_INFORMATION)) + (info_intf->NumberOfPipes * sizeof(USBD_PIPE_INFORMATION)));

#define MAKE_PIPE(ep, type, interval) ((USBD_PIPE_HANDLE)((ep) | ((interval) << 8) | ((type) << 16)))
#define TO_INTF_HANDLE(intf_num, altsetting)	((USBD_INTERFACE_HANDLE)((intf_num << 8) + altsetting))
#define TO_INTF_NUM(handle)		(UCHAR)(((UINT_PTR)(handle)) >> 8)
#define TO_INTF_ALTSETTING(handle)	(UCHAR)((UINT_PTR)(handle) & 0xff)

#ifdef DBG

static const char *
dbg_pipe(PUSBD_PIPE_INFORMATION pipe)
{
	static char	buf[512];

<<<<<<< HEAD
	libdrv_snprintf(buf, 512, "addr:%02x intv:%d typ:%d mps:%d mts:%d flags:%x",
=======
	dbg_snprintf(buf, 512, "addr:%02x intv:%d typ:%d mps:%d mts:%d flags:%x",
>>>>>>> a32b206... vhci, code cleanup for usb descriptor manipulation
		pipe->EndpointAddress, pipe->Interval, pipe->PipeType, pipe->PipeFlags,
		pipe->MaximumPacketSize, pipe->MaximumTransferSize, pipe->PipeFlags);
	return buf;
}

#endif

static void
set_pipe(PUSBD_PIPE_INFORMATION pipe, PUSB_ENDPOINT_DESCRIPTOR ep_desc, unsigned char speed)
{
	pipe->MaximumPacketSize = ep_desc->wMaxPacketSize;
	pipe->EndpointAddress = ep_desc->bEndpointAddress;
	pipe->Interval = ep_desc->bInterval;
	pipe->PipeType = ep_desc->bmAttributes & USB_ENDPOINT_TYPE_MASK;
	/* From usb_submit_urb in linux */
	if (pipe->PipeType == USB_ENDPOINT_TYPE_ISOCHRONOUS && speed == USB_SPEED_HIGH) {
		USHORT	mult = 1 + ((pipe->MaximumPacketSize >> 11) & 0x03);
		pipe->MaximumPacketSize &= 0x7ff;
		pipe->MaximumPacketSize *= mult;
	}
	pipe->PipeHandle = MAKE_PIPE(ep_desc->bEndpointAddress, pipe->PipeType, ep_desc->bInterval);
}

static NTSTATUS
setup_endpoints(USBD_INTERFACE_INFORMATION *intf, PUSB_CONFIGURATION_DESCRIPTOR dsc_conf, PUSB_INTERFACE_DESCRIPTOR dsc_intf, UCHAR speed)
{
	PVOID	start = dsc_intf;
<<<<<<< HEAD
	ULONG	n_pipes_setup;
=======
>>>>>>> a32b206... vhci, code cleanup for usb descriptor manipulation
	unsigned int	i;

	n_pipes_setup = (intf->Length - sizeof(USBD_INTERFACE_INFORMATION)) / sizeof(USBD_PIPE_INFORMATION) + 1;
	if (n_pipes_setup < intf->NumberOfPipes) {
		DBGW(DBG_URB, "insufficient interface information size: %u < %u\n", n_pipes_setup, intf->NumberOfPipes);
	}
	else {
		n_pipes_setup = intf->NumberOfPipes;
	}

<<<<<<< HEAD
	for (i = 0; i < n_pipes_setup; i++) {
		PUSB_ENDPOINT_DESCRIPTOR	dsc_ep;

=======
>>>>>>> a32b206... vhci, code cleanup for usb descriptor manipulation
		dsc_ep = dsc_next_ep(dsc_conf, start);
		if (dsc_ep == NULL) {
			DBGW(DBG_IOCTL, "no ep desc\n");
			return FALSE;
		}

		set_pipe(&intf->Pipes[i], dsc_ep, speed);
<<<<<<< HEAD
		DBGI(DBG_IOCTL, "ep setup[%u]: %s\n", i, dbg_pipe(&intf->Pipes[i]));
		start = dsc_ep;

=======
		DBGI(DBG_IOCTL, "ep setup: %s\n", dbg_pipe(&intf->Pipes[i]));
		start = dsc_ep;
>>>>>>> a32b206... vhci, code cleanup for usb descriptor manipulation
	}
	return TRUE;
}

NTSTATUS
setup_intf(USBD_INTERFACE_INFORMATION *intf, PUSB_CONFIGURATION_DESCRIPTOR dsc_conf, UCHAR speed)
{
	PUSB_INTERFACE_DESCRIPTOR	dsc_intf;

	if (sizeof(USBD_INTERFACE_INFORMATION) - sizeof(USBD_PIPE_INFORMATION) > intf->Length) {
		DBGE(DBG_URB, "insufficient interface information size?\n");
		///TODO: need to check
		return STATUS_SUCCESS;
	}

	dsc_intf = dsc_find_intf(dsc_conf, intf->InterfaceNumber, intf->AlternateSetting);
	if (dsc_intf == NULL) {
		DBGW(DBG_IOCTL, "no interface desc\n");
		return STATUS_INVALID_DEVICE_REQUEST;
	}
<<<<<<< HEAD
=======
	if (dsc_intf->bNumEndpoints != intf->NumberOfPipes) {
		DBGW(DBG_IOCTL, "numbers of pipes are not same:(%d,%d)\n", dsc_intf->bNumEndpoints, intf->NumberOfPipes);
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	if (intf->NumberOfPipes > 0) {
		if (sizeof(USBD_INTERFACE_INFORMATION) + (intf->NumberOfPipes - 1) * sizeof(USBD_PIPE_INFORMATION) > intf->Length) {
			DBGE(DBG_URB, "insufficient interface information size\n");
			return STATUS_INVALID_PARAMETER;
		}
	}
>>>>>>> a32b206... vhci, code cleanup for usb descriptor manipulation

	intf->Class = dsc_intf->bInterfaceClass;
	intf->SubClass = dsc_intf->bInterfaceSubClass;
	intf->Protocol = dsc_intf->bInterfaceProtocol;
	intf->InterfaceHandle = TO_INTF_HANDLE(intf->InterfaceNumber, intf->AlternateSetting);
<<<<<<< HEAD
	intf->NumberOfPipes = dsc_intf->bNumEndpoints;
=======
>>>>>>> a32b206... vhci, code cleanup for usb descriptor manipulation

	if (!setup_endpoints(intf, dsc_conf, dsc_intf, speed))
		return STATUS_INVALID_DEVICE_REQUEST;
	return STATUS_SUCCESS;
}

NTSTATUS
setup_config(PUSB_CONFIGURATION_DESCRIPTOR dsc_conf, PUSBD_INTERFACE_INFORMATION info_intf, PVOID end_info_intf, UCHAR speed)
{
<<<<<<< HEAD
	unsigned int	i;

	for (i = 0; i < dsc_conf->bNumInterfaces; i++) {
=======
	PUSB_CONFIGURATION_DESCRIPTOR	dsc_conf;
	PUSBD_INTERFACE_INFORMATION	info_intf;
	/*
	 * The end position of _URB_SELECT_CONFIGURATION, with which
	 * valid count of info_intf can be detected.
	 */
	PVOID	end_urb_selc;
	unsigned int	i;

	/* assign meaningless value, handle value is not used */
	urb_selc->ConfigurationHandle = (USBD_CONFIGURATION_HANDLE)0x12345678;

	dsc_conf = urb_selc->ConfigurationDescriptor;
	end_urb_selc = (PUCHAR)urb_selc + urb_selc->Hdr.Length;
	info_intf = &urb_selc->Interface;
	for (i = 0; i < urb_selc->ConfigurationDescriptor->bNumInterfaces; i++) {
>>>>>>> cf70c86... vhci, fix urb selection error
		NTSTATUS	status;

		if ((status = setup_intf(info_intf, dsc_conf, speed)) != STATUS_SUCCESS)
			return status;

		info_intf = NEXT_USBD_INTERFACE_INFO(info_intf);
		/* urb_selc may have less info_intf than bNumInterfaces in conf desc */
<<<<<<< HEAD
		if ((PVOID)info_intf >= end_info_intf)
=======
		if ((PVOID)info_intf >= end_urb_selc)
>>>>>>> cf70c86... vhci, fix urb selection error
			break;
	}

	/* it seems we must return now */
	return STATUS_SUCCESS;
}
<<<<<<< HEAD
=======

NTSTATUS
select_interface(struct _URB_SELECT_INTERFACE *urb_seli, PUSB_CONFIGURATION_DESCRIPTOR dsc_conf, UCHAR speed)
{
	PUSBD_INTERFACE_INFORMATION	info_intf;

	info_intf = &urb_seli->Interface;

	return setup_intf(info_intf, dsc_conf, speed);
}
>>>>>>> a32b206... vhci, code cleanup for usb descriptor manipulation
