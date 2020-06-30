#include "vhci_driver.h"
#include "vhci_plugin.tmh"

#include "usbip_vhci_api.h"

extern VOID
setup_ep_callbacks(PUDECX_USB_DEVICE_STATE_CHANGE_CALLBACKS pcallbacks);

static BOOLEAN
setup_vusb(UDECXUSBDEVICE ude_usbdev)
{
	pctx_vusb_t	vusb = TO_VUSB(ude_usbdev);
	WDF_OBJECT_ATTRIBUTES       attrs, attrs_hmem;
	NTSTATUS	status;

	WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
	attrs.ParentObject = ude_usbdev;

	status = WdfWaitLockCreate(&attrs, &vusb->lock);
	if (NT_ERROR(status)) {
		TRE(VUSB, "failed to create wait lock: %!STATUS!", status);
		return FALSE;
	}

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs_hmem, urb_req_t);
	attrs_hmem.ParentObject = ude_usbdev;

	status = WdfLookasideListCreate(&attrs, sizeof(urb_req_t), PagedPool, &attrs_hmem, 0, &vusb->lookaside_urbr);
	if (NT_ERROR(status)) {
		TRE(VUSB, "failed to create urbr memory: %!STATUS!", status);
		return FALSE;
	}

	vusb->ude_usbdev = ude_usbdev;
	vusb->pending_req_read = NULL;
	vusb->urbr_sent_partial = NULL;
	vusb->len_sent_partial = 0;
	vusb->seq_num = 0;
	vusb->invalid = FALSE;
	vusb->ep_default = NULL;

	InitializeListHead(&vusb->head_urbr);
	InitializeListHead(&vusb->head_urbr_pending);
	InitializeListHead(&vusb->head_urbr_sent);

	return TRUE;
}

static PUDECXUSBDEVICE_INIT
build_vusb_pdinit(pctx_vhci_t vhci)
{
	PUDECXUSBDEVICE_INIT	pdinit;
	UDECX_USB_DEVICE_STATE_CHANGE_CALLBACKS	callbacks;

	pdinit = UdecxUsbDeviceInitAllocate(vhci->hdev);

	UDECX_USB_DEVICE_CALLBACKS_INIT(&callbacks);

	setup_ep_callbacks(&callbacks);

	UdecxUsbDeviceInitSetStateChangeCallbacks(pdinit, &callbacks);
	UdecxUsbDeviceInitSetSpeed(pdinit, UdecxUsbFullSpeed);
	UdecxUsbDeviceInitSetEndpointsType(pdinit, UdecxEndpointTypeDynamic);

	return pdinit;
}

static void
setup_descriptors(PUDECXUSBDEVICE_INIT pdinit, pvhci_pluginfo_t pluginfo)
{
	NTSTATUS	status;
	USHORT		conf_dscr_fullsize;

	status = UdecxUsbDeviceInitAddDescriptor(pdinit, pluginfo->dscr_dev, 18);
	if (NT_ERROR(status)) {
		TRW(VUSB, "failed to add a device descriptor to device init");
	}
	conf_dscr_fullsize = *((PUSHORT)pluginfo->dscr_conf + 1);
	status = UdecxUsbDeviceInitAddDescriptor(pdinit, pluginfo->dscr_conf, conf_dscr_fullsize);
	if (NT_ERROR(status)) {
		TRW(VUSB, "failed to add a configuration descriptor to device init");
	}
}

static VOID
vusb_cleanup(_In_ WDFOBJECT ude_usbdev)
{
	UNREFERENCED_PARAMETER(ude_usbdev);
	TRD(VUSB, "Enter");
}

static pctx_vusb_t
vusb_plugin(pctx_vhci_t vhci, pvhci_pluginfo_t pluginfo)
{
	pctx_vusb_t	vusb;
	PUDECXUSBDEVICE_INIT	pdinit;
	UDECX_USB_DEVICE_PLUG_IN_OPTIONS	opts;
	UDECXUSBDEVICE	ude_usbdev;
	WDF_OBJECT_ATTRIBUTES       attrs;
	NTSTATUS	status;

	pdinit = build_vusb_pdinit(vhci);
	setup_descriptors(pdinit, pluginfo);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, ctx_vusb_t);
	attrs.EvtCleanupCallback = vusb_cleanup;

	status = UdecxUsbDeviceCreate(&pdinit, &attrs, &ude_usbdev);
	if (NT_ERROR(status)) {
		TRE(VUSB, "failed to create usb device: %!STATUS!", status);
		UdecxUsbDeviceInitFree(pdinit);
		return NULL;
	}

	UDECX_USB_DEVICE_PLUG_IN_OPTIONS_INIT(&opts);
	opts.Usb20PortNumber = pluginfo->port;
	status = UdecxUsbDevicePlugIn(ude_usbdev, &opts);
	if (NT_ERROR(status)) {
		TRE(VUSB, "failed to plugin a new device %!STATUS!", status);
		WdfObjectDelete(ude_usbdev);
		return NULL;
	}

	if (!setup_vusb(ude_usbdev)) {
		WdfObjectDelete(ude_usbdev);
		return NULL;
	}
	vusb = TO_VUSB(ude_usbdev);
	vusb->vhci = vhci;
	vusb->devid = pluginfo->devid;

	return vusb;
}

NTSTATUS
plugin_vusb(pctx_vhci_t vhci, WDFREQUEST req, pvhci_pluginfo_t pluginfo)
{
	pctx_vusb_t	vusb;
	NTSTATUS	status = STATUS_UNSUCCESSFUL;

	WdfWaitLockAcquire(vhci->lock, NULL);

	if (vhci->vusbs[pluginfo->port - 1] != NULL) {
		WdfWaitLockRelease(vhci->lock);
		return STATUS_OBJECT_NAME_COLLISION;
	}

	vusb = vusb_plugin(vhci, pluginfo);
	if (vusb != NULL) {
		WDFFILEOBJECT	fo = WdfRequestGetFileObject(req);
		if (fo != NULL) {
			pctx_vusb_t	*pvusb = TO_PVUSB(fo);
			*pvusb = vusb;
		}
		else {
			TRE(IOCTL, "empty fileobject. setup failed");
		}
		vhci->vusbs[pluginfo->port - 1] = vusb;
		status = STATUS_SUCCESS;
	}

	WdfWaitLockRelease(vhci->lock);

	return status;
}