#include "devconf.h"

#include <usbdlib.h>

PUSB_INTERFACE_DESCRIPTOR
dsc_find_intf(PUSB_CONFIGURATION_DESCRIPTOR dsc_conf, UCHAR intf_num, USHORT alt_setting)
{
	return USBD_ParseConfigurationDescriptorEx(dsc_conf, dsc_conf, intf_num, alt_setting, -1, -1, -1);
}

PUSB_ENDPOINT_DESCRIPTOR
dsc_next_ep(PUSB_CONFIGURATION_DESCRIPTOR dsc_conf, PVOID start)
{
	PUSB_COMMON_DESCRIPTOR	dsc = (PUSB_COMMON_DESCRIPTOR)start;
	if (dsc->bDescriptorType == USB_ENDPOINT_DESCRIPTOR_TYPE)
		dsc = NEXT_DESC(dsc);
	return (PUSB_ENDPOINT_DESCRIPTOR)USBD_ParseDescriptors(dsc_conf, dsc_conf->wTotalLength, dsc, USB_ENDPOINT_DESCRIPTOR_TYPE);
}

ULONG
dsc_conf_get_n_intfs(PUSB_CONFIGURATION_DESCRIPTOR dsc_conf)
{
	PVOID	start = dsc_conf;
	ULONG	n_intfs = 0;

	while (TRUE) {
		PUSB_COMMON_DESCRIPTOR	desc = USBD_ParseDescriptors(dsc_conf, dsc_conf->wTotalLength, start, USB_INTERFACE_DESCRIPTOR_TYPE);
		if (desc == NULL)
			break;
		start = NEXT_DESC(desc);
		n_intfs++;
	}
	return n_intfs;
}