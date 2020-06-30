#ifndef __VHCI_DRIVER_H
#define __VHCI_DRIVER_H

#pragma

#include "usbip_vhci_api.h"
#include "usbip_wudev.h"

HANDLE usbip_vhci_driver_open(void);
void usbip_vhci_driver_close(HANDLE hdev);
int usbip_vhci_get_free_port(HANDLE hdev);
int usbip_vhci_attach_device(HANDLE hdev, int port, const char *instid, usbip_wudev_t *wudev);
int usbip_vhci_attach_device_ude(HANDLE hdev, pvhci_pluginfo_t pluginfo);
int usbip_vhci_detach_device(HANDLE hdev, int port);

#endif /* __VHCI_DRIVER_H */
