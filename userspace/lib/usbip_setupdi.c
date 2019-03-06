#include "usbip_common.h"
#include "usbip_windows.h"

#include <setupapi.h>
#include <stdlib.h>

#include "usbip_setupdi.h"

char *
get_dev_property(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, DWORD prop)
{
	char	*value;
	DWORD	length;

	if (!SetupDiGetDeviceRegistryProperty(dev_info, pdev_info_data, prop, NULL, NULL, 0, &length)) {
		DWORD	err = GetLastError();
		switch (err) {
		case ERROR_INVALID_DATA:
			return _strdup("");
		case ERROR_INSUFFICIENT_BUFFER:
			break;
		default:
			err("get_dev_property: failed to get device property: err: %x", err);
			return NULL;
		}
	}
	else {
		err("get_dev_property: unexpected case");
		return NULL;
	}
	value = malloc(length);
	if (value == NULL) {
		err("get_dev_property: out of memory");
		return NULL;
	}
	if (!SetupDiGetDeviceRegistryProperty(dev_info, pdev_info_data, prop, NULL, (PBYTE)value, length, &length)) {
		err("get_dev_property: failed to get device property: err: %x", GetLastError());
		free(value);
		return NULL;
	}
	return value;
}

char *
get_id_hw(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data)
{
	return get_dev_property(dev_info, pdev_info_data, SPDRP_HARDWAREID);
}

char *
get_upper_filters(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data)
{
	return get_dev_property(dev_info, pdev_info_data, SPDRP_UPPERFILTERS);
}

char *
get_id_inst(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data)
{
	char	*id_inst;
	DWORD	length;

	if (!SetupDiGetDeviceInstanceId(dev_info, pdev_info_data, NULL, 0, &length)) {
		DWORD	err = GetLastError();
		if (err != ERROR_INSUFFICIENT_BUFFER) {
			err("get_id_inst: failed to get instance id: err: %x", err);
			return NULL;
		}
	}
	else {
		err("get_id_inst: unexpected case");
		return NULL;
	}
	id_inst = (char *)malloc(length);
	if (id_inst == NULL) {
		err("get_id_inst: out of memory");
		return NULL;
	}
	if (!SetupDiGetDeviceInstanceId(dev_info, pdev_info_data, id_inst, length, NULL)) {
		err("failed to get instance id\n");
		free(id_inst);
		return NULL;
	}
	return id_inst;
}

PSP_DEVICE_INTERFACE_DETAIL_DATA
get_intf_detail(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, LPCGUID pguid)
{
	SP_DEVICE_INTERFACE_DATA	dev_interface_data;
	PSP_DEVICE_INTERFACE_DETAIL_DATA	pdev_interface_detail;
	unsigned long len = 0;
	DWORD	err;

	dev_interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
	if (!SetupDiEnumDeviceInterfaces(dev_info, pdev_info_data, pguid, 0, &dev_interface_data)) {
		DWORD	err = GetLastError();
		if (err != ERROR_NO_MORE_ITEMS)
			err("SetupDiEnumDeviceInterfaces failed: err: 0x%lx", err);
		return NULL;
	}
	SetupDiGetDeviceInterfaceDetail(dev_info, &dev_interface_data, NULL, 0, &len, NULL);
	err = GetLastError();
	if (err != ERROR_INSUFFICIENT_BUFFER) {
		err("SetupDiGetDeviceInterfaceDetail failed: err: 0x%lx", err);
		return NULL;
	}

	// Allocate the required memory and set the cbSize.
	pdev_interface_detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(len);
	if (pdev_interface_detail == NULL) {
		err("can't malloc %lu size memory", len);
		return NULL;
	}

	pdev_interface_detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

	// Try to get device details.
	if (!SetupDiGetDeviceInterfaceDetail(dev_info, &dev_interface_data,
		pdev_interface_detail, len, &len, NULL)) {
		// Errors.
		err("SetupDiGetDeviceInterfaceDetail failed: err: 0x%lx", GetLastError());
		free(pdev_interface_detail);
		return NULL;
	}

	return pdev_interface_detail;
}

BOOL
set_device_state(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, DWORD state)
{
	SP_PROPCHANGE_PARAMS	prop_params;

	memset(&prop_params, 0, sizeof(SP_PROPCHANGE_PARAMS));

	prop_params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
	prop_params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
	prop_params.StateChange = state;
	prop_params.Scope = DICS_FLAG_CONFIGSPECIFIC;//DICS_FLAG_GLOBAL;
	prop_params.HwProfile = 0;

	if (!SetupDiSetClassInstallParams(dev_info, pdev_info_data, (SP_CLASSINSTALL_HEADER *)&prop_params, sizeof(SP_PROPCHANGE_PARAMS))) {
		err("failed to set class install parameters\n");
		return FALSE;
	}

	if (!SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, dev_info, pdev_info_data)) {
		err("failed to call class installer\n");
		return FALSE;
	}

	return TRUE;
}

BOOL
restart_device(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data)
{
	if (!set_device_state(dev_info, pdev_info_data, DICS_DISABLE))
		return FALSE;
	if (!set_device_state(dev_info, pdev_info_data, DICS_ENABLE))
		return FALSE;
	return TRUE;
}

static unsigned char
get_devno_from_inst_id(unsigned char devno_map[], const char *id_inst)
{
	unsigned char	devno = 0;
	int	ndevs;
	int	i;

	for (i = 0; id_inst[i]; i++) {
		devno += (unsigned char)(id_inst[i] * 19 + 13);
	}
	if (devno == 0)
		devno++;

	ndevs = 0;
	while (devno_map[devno - 1]) {
		if (devno == 255)
			devno = 1;
		else
			devno++;
		if (ndevs == 255) {
			/* devno map is full */
			return 0;
		}
		ndevs++;
	}
	devno_map[devno - 1] = 1;
	return devno;
}

int
traverse_usbdevs(walkfunc_t walker, BOOL present_only, void *ctx)
{
	HDEVINFO	dev_info;
	SP_DEVINFO_DATA	dev_info_data;
	DWORD	flags = DIGCF_ALLCLASSES;
	unsigned char	devno_map[255];
	int	ret = 0;
	int	idx;

	if (present_only)
		flags |= DIGCF_PRESENT;
	dev_info = SetupDiGetClassDevs(NULL, "USB", NULL, flags);
	if (dev_info == INVALID_HANDLE_VALUE) {
		err("SetupDiGetClassDevs failed: 0x%lx\n", GetLastError());
		return -1;
	}

	memset(devno_map, 0, 255);
	dev_info_data.cbSize = sizeof(SP_DEVINFO_DATA);
	for (idx = 0;; idx++) {
		char	*id_inst;
		devno_t	devno;

		if (!SetupDiEnumDeviceInfo(dev_info, idx, &dev_info_data)) {
			DWORD	err = GetLastError();

			if (err != ERROR_NO_MORE_ITEMS) {
				err("failed to get device information: err: %d\n", err);
			}
			break;
		}
		id_inst = get_id_inst(dev_info, &dev_info_data);
		if (id_inst == NULL)
			continue;
		devno = get_devno_from_inst_id(devno_map, id_inst);
		free(id_inst);
		if (devno == 0)
			continue;
		ret = walker(dev_info, &dev_info_data, devno, ctx);
		if (ret != 0)
			break;
	}

	SetupDiDestroyDeviceInfoList(dev_info);
	return ret;
}

int
traverse_intfdevs(walkfunc_t walker, LPCGUID pguid, void *ctx)
{
	HDEVINFO	dev_info;
	SP_DEVINFO_DATA	dev_info_data;
	int	ret = 0;
	int	idx;

	dev_info = SetupDiGetClassDevs(pguid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (dev_info == INVALID_HANDLE_VALUE) {
		err("SetupDiGetClassDevs failed: 0x%lx\n", GetLastError());
		return -1;
	}

	dev_info_data.cbSize = sizeof(SP_DEVINFO_DATA);
	for (idx = 0;; idx++) {
		if (!SetupDiEnumDeviceInfo(dev_info, idx, &dev_info_data)) {
			DWORD	err = GetLastError();

			if (err != ERROR_NO_MORE_ITEMS) {
				err("failed to get device information: err: %d\n", err);
			}
			break;
		}
		ret = walker(dev_info, &dev_info_data, 0, ctx);
		if (ret != 0)
			break;
	}

	SetupDiDestroyDeviceInfoList(dev_info);
	return ret;
}

static BOOL
is_valid_usb_dev(const char *id_hw)
{
	if (id_hw == NULL)
		return FALSE;
	if (strncmp(id_hw, "USB\\", 4) != 0)
		return FALSE;
	if (strncmp(id_hw + 4, "VID_", 4) != 0)
		return FALSE;
	if (strlen(id_hw + 8) < 4)
		return FALSE;
	if (strncmp(id_hw + 12, "&PID_", 5) != 0)
		return FALSE;
	if (strlen(id_hw + 17) < 4)
		return FALSE;
	return TRUE;
}

BOOL
get_usbdev_info(const char *cid_hw, unsigned short *pvendor, unsigned short *pproduct)
{
	char	*id_hw = _strdup(cid_hw);

	if (!is_valid_usb_dev(id_hw))
		return FALSE;
	id_hw[12] = '\0';
	sscanf_s(id_hw + 8, "%hx", pvendor);
	id_hw[21] = '\0';
	sscanf_s(id_hw + 17, "%hx", pproduct);
	return TRUE;
}

devno_t
get_devno_from_busid(const char *busid)
{
	unsigned	busno;
	devno_t		devno;

	if (sscanf_s(busid, "%u-%hhu", &busno, &devno) != 2) {
		err("invalid busid: %s", busid);
		return 0;
	}
	if (busno != 1) {
		err("invalid busid: %s", busid);
		return 0;
	}
	return devno;
}
