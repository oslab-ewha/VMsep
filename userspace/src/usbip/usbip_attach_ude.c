/*
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "usbip_windows.h"

#include <stdlib.h>

#include "usbip_vhci_api.h"

#include "usbip_common.h"
#include "usbip_network.h"
#include "usbip_vhci.h"
#include "usbip_forward.h"
#include "usbip_dscr.h"

extern void usbip_attach_usage(void);

static int
import_device(SOCKET sockfd, pvhci_pluginfo_t pluginfo, HANDLE *phdev)
{
	HANDLE hdev;
	int rc;
	int port;

	hdev = usbip_vhci_driver_open();
	if (hdev == INVALID_HANDLE_VALUE) {
		err("open vhci driver");
		return 1;
	}

	port = usbip_vhci_get_free_port(hdev);
	if (port <= 0) {
		err("no free port");
		usbip_vhci_driver_close(hdev);
		return 1;
	}

	dbg("got free port %d", port);

	pluginfo->port = port;

	rc = usbip_vhci_attach_device_ude(hdev, pluginfo);

	if (rc < 0) {
		err("import device");
		usbip_vhci_driver_close(hdev);
		return 1;
	}

	*phdev = hdev;

	return port;
}

static pvhci_pluginfo_t
build_pluginfo(SOCKET sockfd, unsigned devid)
{
	pvhci_pluginfo_t	pluginfo;
	unsigned long	pluginfo_size;
	unsigned short	conf_dscr_len;

	if (fetch_conf_descriptor(sockfd, devid, NULL, &conf_dscr_len) < 0) {
		err("failed to get configuration descriptor size");
		return NULL;
	}

	pluginfo_size = sizeof(vhci_pluginfo_t) + conf_dscr_len - 9;
	pluginfo = (pvhci_pluginfo_t)malloc(pluginfo_size);
	if (pluginfo == NULL) {
		err("out of memory or invalid vhci pluginfo size");
		return NULL;
	}
	if (fetch_device_descriptor(sockfd, devid, pluginfo->dscr_dev) < 0) {
		err("failed to fetch device descriptor");
		free(pluginfo);
		return NULL;
	}
	if (fetch_conf_descriptor(sockfd, devid, pluginfo->dscr_conf, &conf_dscr_len) < 0) {
		err("failed to fetch configuration descriptor");
		free(pluginfo);
		return NULL;
	}

	pluginfo->size = pluginfo_size;
	pluginfo->devid = devid;

	return pluginfo;
}

static int query_import_device(SOCKET sockfd, const char *busid, HANDLE *phdev, const char *instid)
{
	int rc;
	struct op_import_request request;
	struct op_import_reply   reply;
	pvhci_pluginfo_t	pluginfo;
	uint16_t code = OP_REP_IMPORT;
	unsigned	devid;

	memset(&request, 0, sizeof(request));
	memset(&reply, 0, sizeof(reply));

	/* send a request */
	rc = usbip_net_send_op_common(sockfd, OP_REQ_IMPORT, 0);
	if (rc < 0) {
		err("send op_common");
		return 1;
	}

	strncpy_s(request.busid, USBIP_BUS_ID_SIZE, busid, sizeof(request.busid));

	PACK_OP_IMPORT_REQUEST(0, &request);

	rc = usbip_net_send(sockfd, (void *)&request, sizeof(request));
	if (rc < 0) {
		err("send op_import_request");
		return 1;
	}

	/* recieve a reply */
	rc = usbip_net_recv_op_common(sockfd, &code);
	if (rc < 0) {
		err("recv op_common");
		return 1;
	}

	rc = usbip_net_recv(sockfd, (void *)&reply, sizeof(reply));
	if (rc < 0) {
		err("recv op_import_reply");
		return 1;
	}

	PACK_OP_IMPORT_REPLY(0, &reply);

	/* check the reply */
	if (strncmp(reply.udev.busid, busid, sizeof(reply.udev.busid))) {
		err("recv different busid %s", reply.udev.busid);
		return 1;
	}

	devid = reply.udev.busnum << 16 | reply.udev.devnum;
	pluginfo = build_pluginfo(sockfd, devid);
	if (pluginfo == NULL)
		return 2;

	if (instid != NULL)
		mbstowcs_s(NULL, pluginfo->winstid, MAX_VHCI_INSTANCE_ID, instid, _TRUNCATE);
	else
		pluginfo->winstid[0] = L'\0';

	/* import a device */
	return import_device(sockfd, pluginfo, phdev);
}

static int
attach_device(const char *host, const char *busid, const char *instid)
{
	SOCKET	sockfd;
	int	rhport;
	HANDLE	hdev = INVALID_HANDLE_VALUE;

	sockfd = usbip_net_tcp_connect(host, usbip_port_string);
	if (sockfd == INVALID_SOCKET) {
		err("tcp connect");
		return 1;
	}

	rhport = query_import_device(sockfd, busid, &hdev, instid);
	if (rhport < 0) {
		err("query");
		return 1;
	}

	usbip_forward(hdev, (HANDLE)sockfd, FALSE);

	usbip_vhci_detach_device(hdev, rhport);

	usbip_vhci_driver_close(hdev);

	closesocket(sockfd);

	return 0;
}

int usbip_attach_ude(int argc, char *argv[])
{
	static const struct option opts[] = {
		{ "remote", required_argument, NULL, 'r' },
		{ "busid", required_argument, NULL, 'b' },
		{ "instid", optional_argument, NULL, 'i' },
		{ NULL, 0, NULL, 0 }
	};
	char *host = NULL;
	char *busid = NULL;
	char *instid = NULL;
	int opt;
	int ret = -1;

	for (;;) {
		opt = getopt_long(argc, argv, "r:b:i:", opts, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case 'r':
			host = optarg;
			break;
		case 'b':
			busid = optarg;
			break;
		case 'i':
			instid = optarg;
			break;
		default:
			goto err_out;
		}
	}

	if (!host || !busid)
		goto err_out;

	ret = attach_device(host, busid, instid);
	goto out;

err_out:
	usbip_attach_usage();
out:
	return ret;
}
