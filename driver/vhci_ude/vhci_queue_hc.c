#include "vhci_driver.h"
#include "vhci_queue_hc.tmh"

extern VOID io_device_control(_In_ WDFQUEUE queue, _In_ WDFREQUEST req,
	_In_ size_t OutputBufferLength, _In_ size_t InputBufferLength, _In_ ULONG IoControlCode);
extern VOID io_read(_In_ WDFQUEUE queue, _In_ WDFREQUEST req, _In_ size_t len);
extern VOID io_write(_In_ WDFQUEUE queue, _In_ WDFREQUEST req, _In_ size_t len);

PAGEABLE VOID
create_queue_hc(pctx_vhci_t vhci)
{
	WDFQUEUE	queue;
	WDF_IO_QUEUE_CONFIG	conf;
	WDF_OBJECT_ATTRIBUTES	attrs;
	NTSTATUS	status;

	PAGED_CODE();

	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&conf, WdfIoQueueDispatchParallel);
	conf.EvtIoRead = io_read;
	conf.EvtIoWrite = io_write;
	conf.EvtIoDeviceControl = io_device_control;

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, pctx_vhci_t);
	attrs.SynchronizationScope = WdfSynchronizationScopeQueue;

	status = WdfIoQueueCreate(vhci->hdev, &conf, &attrs, &queue);
	if (NT_SUCCESS(status)) {
		*TO_PVHCI(queue) = vhci;
		vhci->queue = queue;
	}
	else {
		TRE(QUEUE, "failed to create queue: %!STATUS!", status);
	}
}