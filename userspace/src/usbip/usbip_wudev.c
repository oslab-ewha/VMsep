#include "usbip_windows.h"

#include "usbip_wudev.h"
#include "usbip_proto.h"

static BOOL
is_zero_class(usbip_wudev_t *wudev)
{
	if (wudev->bDeviceClass == 0 && wudev->bDeviceSubClass == 0 && wudev->bDeviceProtocol == 0)
		return TRUE;
	return FALSE;
}

static int
fetch_conf_desc(SOCKET sockfd, unsigned devid, char *pdesc, unsigned desc_size)
{
	struct usbip_header	uhdr;
	unsigned	alen;

	memset(&uhdr, 0, sizeof(uhdr));

	uhdr.base.command = htonl(USBIP_CMD_SUBMIT);
	/* sufficient large enough seq used to avoid conflict with normal vhci operation */
	uhdr.base.seqnum = htonl(0x7fffffff);
	uhdr.base.direction = htonl(USBIP_DIR_IN);
	uhdr.base.devid = htonl(devid);

	uhdr.u.cmd_submit.transfer_buffer_length = htonl(desc_size);
	uhdr.u.cmd_submit.setup[0] = 0x80;	/* IN/control port */
	uhdr.u.cmd_submit.setup[1] = 6;		/* GetDescriptor */
	*(unsigned short *)(uhdr.u.cmd_submit.setup + 6) = (unsigned short)desc_size;	/* Length */
	uhdr.u.cmd_submit.setup[3] = 2;		/* Configuration Descriptor */

	if (usbip_net_send(sockfd, &uhdr, sizeof(uhdr)) < 0) {
		dbg("fetch_conf_desc: failed to send usbip header\n");
		return -1;
	}
	if (usbip_net_recv(sockfd, &uhdr, sizeof(uhdr)) < 0) {
		dbg("fetch_conf_desc: failed to recv usbip header\n");
		return -1;
	}
	if (uhdr.u.ret_submit.status != 0) {
		dbg("fetch_conf_desc: command submit error: %d\n", uhdr.u.ret_submit.status);
		return -1;
	}
	alen = ntohl(uhdr.u.ret_submit.actual_length);
	if (alen < desc_size) {
		err("fetch_conf_desc: too short response: actual length: %d\n", alen);
		return -1;
	}
	if (usbip_net_recv(sockfd, pdesc, alen) < 0) {
		err("fetch_conf_desc: failed to recv usbip payload\n");
		return -1;
	}
	return 0;
}

/*
* Sadly, udev structure from linux does not have an interface descriptor.
* So we should get interface class number via GET_DESCRIPTOR usb command.
*/
static void
supplement_with_interface(SOCKET sockfd, usbip_wudev_t *wudev)
{
	unsigned char	buf[18];

	if (fetch_conf_desc(sockfd, wudev->devid, buf, 18) < 0) {
		err("failed to adjust device descriptor with interface descriptor\n");
		return;
	}

	wudev->bNumInterfaces = buf[4];

	if (wudev->bNumInterfaces == 1){
		/* buf[4] holds the number of interfaces in USB configuration.
		 * Supplement class/subclass/protocol only if there exists only single interface.
		 * A device with multiple interfaces will be detected as a composite by vhci. 
		 */
		wudev->bDeviceClass = buf[14];
		wudev->bDeviceSubClass = buf[15];
		wudev->bDeviceProtocol = buf[16];
	}
}

static void
setup_wudev_from_udev(usbip_wudev_t *wudev, struct usbip_usb_device *udev)
{
	wudev->devid = udev->busnum << 16 | udev->devnum;

	wudev->idVendor = udev->idVendor;
	wudev->idProduct = udev->idProduct;
	wudev->bcdDevice = udev->bcdDevice;
	wudev->speed = udev->speed;

	wudev->bDeviceClass = udev->bDeviceClass;
	wudev->bDeviceSubClass = udev->bDeviceSubClass;
	wudev->bDeviceProtocol = udev->bDeviceProtocol;

	wudev->bNumInterfaces = udev->bNumInterfaces;
}

void
get_wudev(SOCKET sockfd, usbip_wudev_t *wudev, struct usbip_usb_device *udev)
{
	setup_wudev_from_udev(wudev, udev);

	/* Many devices have 0 usb class number in a device descriptor.
	 * 0 value means that class number is determined at interface level.
	 * USB class, subclass and protocol numbers should be setup before importing.
	 * Because windows vhci driver builds a device compatible id with those numbers.
	 */
	if (is_zero_class(wudev)) {
		supplement_with_interface(sockfd, wudev);
	}
}
