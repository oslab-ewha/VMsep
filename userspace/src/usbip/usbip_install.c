
#include "usbip.h"
#include "usbip_common.h"

#include <windows.h>
#include <setupapi.h>
#include <newdev.h>

#define ROOT_DEVICE_ID	"ROOT\\USBIP\\root"

typedef enum {
	DRIVER_ROOT,
	DRIVER_VHCI,
} drv_type_t;

typedef struct {
	const char	*name;
	const char	*inf_filename;
	const char	*hwid_inf_section;
	const char	*hwid_inf_key;
	const char	*hwid;
} drv_info_t;

static drv_info_t	drv_infos[] = {
	{ "root", "usbip_root.inf", "Standard.NTamd64", "usbip-win VHCI Root", "USBIPWIN\\root\0" },
	{ "vhci", "usbip_vhci.inf", "Standard.NTamd64", "usbip-win VHCI", "USBIPWIN\\vhci" }
};

void
usbip_install_usage(void)
{
	printf(
"usage: usbip install\n"
"    install or reinstall usbip VHCI drivers\n"
	);
}

void
usbip_uninstall_usage(void)
{
	printf(
"usage: usbip uninstall\n"
"    uninstall usbip VHCI drivers\n"
	);
}

static char *
get_source_inf_path(drv_info_t *pinfo)
{
	char	inf_path[MAX_PATH];
	char	exe_path[MAX_PATH];
	char	*sep;

	if (GetModuleFileName(NULL, exe_path, MAX_PATH) == 0) {
		err("failed to get a executable path");
		return NULL;
	}
	if ((sep = strrchr(exe_path, '\\')) == NULL) {
		err("invalid executanle path: %s", exe_path);
		return NULL;
	}
	*sep = '\0';
	snprintf(inf_path, MAX_PATH, "%s\\%s", exe_path, pinfo->inf_filename);
	return _strdup(inf_path);
}

static BOOL
is_driver_oem_inf(drv_info_t *pinfo, const char *inf_path)
{
	HINF	hinf;
	INFCONTEXT	ctx;
	BOOL	found = FALSE;

	hinf = SetupOpenInfFile(inf_path, NULL, INF_STYLE_WIN4, NULL);
	if (hinf == INVALID_HANDLE_VALUE) {
		err("cannot open inf file: %s", inf_path);
		return FALSE;
	}

	if (SetupFindFirstLine(hinf, pinfo->hwid_inf_section, pinfo->hwid_inf_key, &ctx)) {
		char	hwid[32];
		DWORD	reqsize;

		if (SetupGetStringField(&ctx, 2, hwid, 32, &reqsize)) {
			if (strcmp(hwid, pinfo->hwid) == 0)
				found = TRUE;
		}
	}

	SetupCloseInfFile(hinf);
	return found;
}

static char *
get_oem_inf_pattern(void)
{
	char	oem_inf_pattern[MAX_PATH];
	char	windir[MAX_PATH];

	if (GetWindowsDirectory(windir, MAX_PATH) == 0)
		return NULL;
	snprintf(oem_inf_pattern, MAX_PATH, "%s\\inf\\oem*.inf", windir);
	return _strdup(oem_inf_pattern);
}

static char *
get_oem_inf(drv_info_t *pinfo)
{
	char	*oem_inf_pattern;
	HANDLE	hFind;
	WIN32_FIND_DATA	wfd;
	char	*oem_inf_name = NULL;

	oem_inf_pattern = get_oem_inf_pattern();
	if (oem_inf_pattern == NULL) {
		err("failed to get oem inf pattern");
		return NULL;
	}

	hFind = FindFirstFile(oem_inf_pattern, &wfd);
	free(oem_inf_pattern);

	if (hFind == INVALID_HANDLE_VALUE) {
		err("failed to get oem inf: 0x%lx", GetLastError());
		return NULL;
	}

	do {
		if (is_driver_oem_inf(pinfo, wfd.cFileName)) {
			oem_inf_name = _strdup(wfd.cFileName);
			break;
		}
	} while (FindNextFile(hFind, &wfd));

	FindClose(hFind);

	return oem_inf_name;
}

static BOOL
uninstall_driver_package(drv_info_t *pinfo)
{
	char *oem_inf_name;

	oem_inf_name = get_oem_inf(pinfo);
	if (oem_inf_name == NULL)
		return TRUE;

	if (!SetupUninstallOEMInf(oem_inf_name, 0, NULL)) {
		err("failed to uninstall a old %s driver package: 0x%lx", pinfo->name, GetLastError());
		free(oem_inf_name);
		return FALSE;
	}
	free(oem_inf_name);

	return TRUE;
}

static BOOL
install_driver_package(drv_info_t *pinfo)
{
	char	*inf_path;
	BOOL	res = TRUE;

	inf_path = get_source_inf_path(pinfo);
	if (inf_path == NULL)
		return FALSE;

	if (!SetupCopyOEMInf(inf_path, NULL, SPOST_PATH, 0, NULL, 0, NULL, NULL)) {
		DWORD	err = GetLastError();
		switch (err) {
		case ERROR_FILE_NOT_FOUND:
			err("usbip_%s.inf or usbip_vhci.sys file not found", pinfo->name);
			break;
		default:
			err("failed to install %s driver package: err:%lx", pinfo->name, err);
			break;
		}
		res = FALSE;
	}
	free(inf_path);
	return res;
}

static BOOL
uninstall_root_device(void)
{
	HDEVINFO	hdevinfoset;
	SP_DEVINFO_DATA	devinfo;

	hdevinfoset = SetupDiCreateDeviceInfoList(NULL, NULL);
	if (hdevinfoset == INVALID_HANDLE_VALUE) {
		err("failed to create devinfoset");
		return FALSE;
	}

	memset(&devinfo, 0, sizeof(SP_DEVINFO_DATA));
	devinfo.cbSize = sizeof(SP_DEVINFO_DATA);
	if (!SetupDiOpenDeviceInfo(hdevinfoset, ROOT_DEVICE_ID, NULL, 0, &devinfo)) {
		/* If there's no root device, it fails with 0xe000020b error code. */
		SetupDiDestroyDeviceInfoList(hdevinfoset);
		return TRUE;
	}

	if (!DiUninstallDevice(NULL, hdevinfoset, &devinfo, 0, NULL)) {
		err("cannot uninstall root device: error: 0x%lx", GetLastError());
		SetupDiDestroyDeviceInfoList(hdevinfoset);
		return FALSE;
	}
	SetupDiDestroyDeviceInfoList(hdevinfoset);
	return TRUE;
}

static BOOL
create_root_devinfo(LPGUID pguid_system, HDEVINFO hdevinfoset, PSP_DEVINFO_DATA pdevinfo)
{
	DWORD	err;

	memset(pdevinfo, 0, sizeof(SP_DEVINFO_DATA));
	pdevinfo->cbSize = sizeof(SP_DEVINFO_DATA);
	if (SetupDiCreateDeviceInfo(hdevinfoset, ROOT_DEVICE_ID, pguid_system, NULL, NULL, 0, pdevinfo))
		return TRUE;

	err = GetLastError();
	switch (err) {
	case ERROR_ACCESS_DENIED:
		err("access denied - make sure you are running as administrator");
		return FALSE;
	default:
		err("failed to create a root device info: 0x%lx", err);
		break;
	}

	return FALSE;
}

static BOOL
setup_guid(LPCTSTR classname, LPGUID pguid)
{
	DWORD	reqsize;

	if (!SetupDiClassGuidsFromName(classname, pguid, 1, &reqsize)) {
		err("failed to get System setup class");
		return FALSE;
	}
	return TRUE;
}

static BOOL
install_root_device(void)
{
	HDEVINFO	hdevinfoset;
	SP_DEVINFO_DATA	devinfo;
	GUID	guid_system;
	const char	*hwid;

	setup_guid("System", &guid_system);

	hdevinfoset = SetupDiGetClassDevs(&guid_system, NULL, NULL, 0);
	if (hdevinfoset == INVALID_HANDLE_VALUE) {
		err("failed to create devinfoset");
		return FALSE;
	}

	if (!create_root_devinfo(&guid_system, hdevinfoset, &devinfo)) {
		SetupDiDestroyDeviceInfoList(hdevinfoset);
		return FALSE;
	}

	hwid = drv_infos[DRIVER_ROOT].hwid;
	if (!SetupDiSetDeviceRegistryProperty(hdevinfoset, &devinfo, SPDRP_HARDWAREID, hwid, (DWORD)(strlen(hwid) + 2))) {
		err("failed to set hw id: 0x%lx", GetLastError());
		SetupDiDestroyDeviceInfoList(hdevinfoset);
		return FALSE;
	}
	if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, hdevinfoset, &devinfo)) {
		err("failed to register: 0x%lx", GetLastError());
		SetupDiDestroyDeviceInfoList(hdevinfoset);
		return FALSE;
	}
	if (!DiInstallDevice(NULL, hdevinfoset, &devinfo, NULL, 0, NULL)) {
		err("failed to install: 0x%lx", GetLastError());
		SetupDiDestroyDeviceInfoList(hdevinfoset);
		return FALSE;
	}
	SetupDiDestroyDeviceInfoList(hdevinfoset);
	return TRUE;
}

int
usbip_install(int argc, char *argv[])
{
	/* remove first if vhci driver package already exists */
	uninstall_root_device();
	uninstall_driver_package(&drv_infos[DRIVER_VHCI]);
	uninstall_driver_package(&drv_infos[DRIVER_ROOT]);

	if (!install_driver_package(&drv_infos[DRIVER_VHCI])) {
		err("cannot install vhci driver package");
		return 2;
	}
	if (!install_driver_package(&drv_infos[DRIVER_ROOT])) {
		err("cannot install root driver package");
		return 3;
	}
	if (!install_root_device()) {
		err("cannot install root device");
		return 4;
	}

	info("vhci driver installed successfully");
	return 0;
}

int
usbip_uninstall(int argc, char *argv[])
{
	uninstall_root_device();

	if (!uninstall_driver_package(&drv_infos[DRIVER_VHCI])) {
		err("cannot uninstall vhci driver package");
		return 2;
	}
	if (!uninstall_driver_package(&drv_infos[DRIVER_ROOT])) {
		err("cannot uninstall root driver package");
		return 3;
	}

	info("vhci drivers uninstalled");
	return 0;
}