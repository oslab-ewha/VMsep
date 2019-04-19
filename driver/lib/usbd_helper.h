#pragma once

#include <ntddk.h>
#include <usb.h>

USBD_STATUS to_usbd_status(int usbip_status);
int to_usbip_status(USBD_STATUS usbd_status);

ULONG to_usbd_flags(int flags);

void to_usbd_iso_descs(ULONG n_pkts, USBD_ISO_PACKET_DESCRIPTOR *usbd_iso_descs,
	struct usbip_iso_packet_descriptor *iso_descs, BOOLEAN as_result);
void to_iso_descs(ULONG n_pkts, struct usbip_iso_packet_descriptor *iso_descs, USBD_ISO_PACKET_DESCRIPTOR *usbd_iso_descs, BOOLEAN as_result);

ULONG get_iso_descs_len(ULONG n_pkts, struct usbip_iso_packet_descriptor *iso_descs, BOOLEAN is_actual);
ULONG get_usbd_iso_descs_len(ULONG n_pkts, USBD_ISO_PACKET_DESCRIPTOR *usbd_iso_descs);

#define USB_REQUEST_RESET_PIPE 0xfe