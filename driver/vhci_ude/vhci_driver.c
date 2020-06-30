#include "vhci_driver.h"
#include "vhci_driver.tmh"

extern NTSTATUS
evt_add_vhci(_In_ WDFDRIVER drv, _Inout_ PWDFDEVICE_INIT dinit);

static PAGEABLE VOID
vhci_EvtDriverContextCleanup(_In_ WDFOBJECT drvobj)
{
	PAGED_CODE();

	TRD(DRIVER, "Enter");

	WPP_CLEANUP(WdfDriverWdmGetDriverObject((WDFDRIVER)drvobj));
}

INITABLE NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT drvobj, _In_ PUNICODE_STRING regpath)
{
	WDF_DRIVER_CONFIG	conf;
	NTSTATUS		status;
	WDF_OBJECT_ATTRIBUTES	attrs;

	WPP_INIT_TRACING(drvobj, regpath);

	TRD(DRIVER, "Enter");

	WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
	attrs.EvtCleanupCallback = vhci_EvtDriverContextCleanup;

	WDF_DRIVER_CONFIG_INIT(&conf, evt_add_vhci);
	conf.DriverPoolTag = VHCI_POOLTAG;

	status = WdfDriverCreate(drvobj, regpath, &attrs, &conf, WDF_NO_HANDLE);
	if (!NT_SUCCESS(status)) {
		TRE(DRIVER, "WdfDriverCreate failed: %!STATUS!", status);
		WPP_CLEANUP(drvobj);
		return status;
	}

	TRD(DRIVER, "Leave: %!STATUS!", status);

	return status;
}