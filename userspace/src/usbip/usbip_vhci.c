#define INITGUID

#include "usbip_common.h"
#include "usbip_windows.h"
#include "usbip_wudev.h"

#include <stdlib.h>

#include "usbip_setupdi.h"
#include "usbip_vhci_api.h"

static int
walker_devpath(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, devno_t devno, void *ctx)
{
	char	**pdevpath = (char **)ctx;
	char	*id_hw;
	PSP_DEVICE_INTERFACE_DETAIL_DATA	pdev_interface_detail;

	id_hw = get_id_hw(dev_info, pdev_info_data);
	if (id_hw == NULL || _stricmp(id_hw, "usbipwin\\vdev") != 0) {
		err("%s: invalid hw id: %s\n", __FUNCTION__, id_hw ? id_hw : "");
		if (id_hw != NULL)
			free(id_hw);
		return 0;
	}
	free(id_hw);

	pdev_interface_detail = get_intf_detail(dev_info, pdev_info_data, (LPCGUID)&GUID_DEVINTERFACE_VHCI_USBIP);
	if (pdev_interface_detail == NULL) {
		return 0;
	}

	*pdevpath = _strdup(pdev_interface_detail->DevicePath);
	free(pdev_interface_detail);
	return -1;
}

static char *
get_vhci_devpath(void)
{
	char	*devpath;

	if (traverse_intfdevs(walker_devpath, &GUID_DEVINTERFACE_VHCI_USBIP, &devpath) != -1) {
		return NULL;
	}

	return devpath;
}

HANDLE
usbip_vhci_driver_open(void)
{
	HANDLE	hdev;
	char	*devpath;

	devpath = get_vhci_devpath();
	if (devpath == NULL) {
		return INVALID_HANDLE_VALUE;
	}
	dbg("device path: %s", devpath);
	hdev = CreateFile(devpath, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	free(devpath);
	return hdev;
}

void
usbip_vhci_driver_close(HANDLE hdev)
{
	CloseHandle(hdev);
}

static int
usbip_vhci_get_ports_status(HANDLE hdev, char *buf, int l)
{
	ioctl_usbip_vhci_get_ports_status	*st;
	unsigned long len;

	st = (ioctl_usbip_vhci_get_ports_status *)buf;

	if (l != sizeof(ioctl_usbip_vhci_get_ports_status))
		return -1;

	if (DeviceIoControl(hdev, IOCTL_USBIP_VHCI_GET_PORTS_STATUS,
		NULL, 0, st, sizeof(ioctl_usbip_vhci_get_ports_status), &len, NULL)) {
		if (len == sizeof(ioctl_usbip_vhci_get_ports_status))
			return 0;
	}
	return -1;
}

int
usbip_vhci_get_free_port(HANDLE hdev)
{
	char	buf[128];
	int	i;

	if (usbip_vhci_get_ports_status(hdev, buf, sizeof(buf)))
		return -1;
	for(i = 1;i < sizeof(buf); i++) {
		if (!buf[i])
			return i;
	}
	return -1;
}

int
usbip_vhci_attach_device(HANDLE hdev, int port, const char *instid, usbip_wudev_t *wudev)
{
	ioctl_usbip_vhci_plugin  plugin;
	unsigned long	unused;

	plugin.devid = wudev->devid;
	plugin.vendor = wudev->idVendor;
	plugin.product = wudev->idProduct;
	plugin.version = wudev->bcdDevice;
	plugin.speed = wudev->speed;
	plugin.inum = wudev->bNumInterfaces;
	plugin.class = wudev->bDeviceClass;
	plugin.subclass = wudev->bDeviceSubClass;
	plugin.protocol = wudev->bDeviceProtocol;

	plugin.port = port;

	if (instid != NULL)
		mbstowcs_s(NULL, plugin.winstid, MAX_VHCI_INSTANCE_ID, instid, _TRUNCATE);
	else
		plugin.winstid[0] = L'\0';

	if (!DeviceIoControl(hdev, IOCTL_USBIP_VHCI_PLUGIN_HARDWARE,
		&plugin, sizeof(plugin), NULL, 0, &unused, NULL)) {
		err("usbip_vhci_attach_device: DeviceIoControl failed: err: 0x%lx", GetLastError());
		return -1;
	}

	return 0;
}

int
usbip_vhci_detach_device(HANDLE hdev, int port)
{
	ioctl_usbip_vhci_unplug  unplug;
	unsigned long	unused;

	unplug.addr = port;
	if (DeviceIoControl(hdev, IOCTL_USBIP_VHCI_UNPLUG_HARDWARE,
		&unplug, sizeof(unplug), NULL, 0, &unused, NULL))
		return 0;
	return -1;
}

int
show_port_status(void)
{
	HANDLE fd;
	int i;
	char buf[128];

	fd = usbip_vhci_driver_open();
	if (INVALID_HANDLE_VALUE == fd) {
		err("open vhci driver");
		return -1;
	}
	if (usbip_vhci_get_ports_status(fd, buf, sizeof(buf))) {
		err("get port status");
		return -1;
	}
	info("max used port:%d\n", buf[0]);
	for (i = 1; i <= buf[0]; i++) {
		if (buf[i])
			info("port %d: used\n", i);
		else
			info("port %d: idle\n", i);
	}
	CloseHandle(fd);
	return 0;
}
