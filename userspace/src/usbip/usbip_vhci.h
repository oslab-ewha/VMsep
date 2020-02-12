#ifndef __VHCI_DRIVER_H
#define __VHCI_DRIVER_H

#pragma

<<<<<<< HEAD
#include "usbip_vhci_api.h"
=======
>>>>>>> 393ac6a... vhci, let a webcam with IAD be detected as COMPOSITE
#include "usbip_wudev.h"

HANDLE usbip_vhci_driver_open(void);
void usbip_vhci_driver_close(HANDLE hdev);
int usbip_vhci_get_free_port(HANDLE hdev);
<<<<<<< HEAD
<<<<<<< HEAD
int usbip_vhci_attach_device(HANDLE hdev, int port, const char *instid, usbip_wudev_t *wudev);
int usbip_vhci_attach_device_ude(HANDLE hdev, pvhci_pluginfo_t pluginfo);
=======
int usbip_vhci_attach_device(HANDLE hdev, int port, const char *instid, usbip_wudev_t *wudev);
>>>>>>> fdca9fb... Allow specifying a custom instance ID when attaching vhci devices
int usbip_vhci_detach_device(HANDLE hdev, int port);

#endif /* __VHCI_DRIVER_H */
=======
int usbip_vhci_attach_device(HANDLE hdev, int port, usbip_wudev_t *wudev);
int usbip_vhci_detach_device(HANDLE hdev, int port);
>>>>>>> 393ac6a... vhci, let a webcam with IAD be detected as COMPOSITE
