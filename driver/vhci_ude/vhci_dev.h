#pragma once

#include <ntddk.h>
#include <wdf.h>

EXTERN_C_START

struct _ctx_vusb;

typedef struct
{
	WDFDEVICE	hdev;
	ULONG		n_max_ports;
	WDFQUEUE	queue;
	struct _ctx_vusb	**vusbs;
	WDFWAITLOCK	lock;
} ctx_vhci_t, *pctx_vhci_t;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(ctx_vhci_t, TO_VHCI)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(pctx_vhci_t, TO_PVHCI)

struct _urb_req;
struct _ctx_ep;

typedef struct _ctx_vusb
{
	ULONG		port;
	pctx_vhci_t	vhci;
	UDECXUSBDEVICE	ude_usbdev;

	BOOLEAN		invalid;
	// pending req which doesn't find an available urbr
	WDFREQUEST	pending_req_read;
	// a partially transferred urbr
	struct _urb_req	*urbr_sent_partial;
	// a partially transferred length of urbr_sent_partial
	ULONG		len_sent_partial;
	// all urbr's. This list will be used for clear or cancellation.
	LIST_ENTRY	head_urbr;
	// pending urbr's which are not transferred yet
	LIST_ENTRY	head_urbr_pending;
	// urbr's which had been sent and have waited for response
	LIST_ENTRY	head_urbr_sent;
	ULONG		devid;
	ULONG		seq_num;
	struct _ctx_ep	*ep_default;
	WDFLOOKASIDE	lookaside_urbr;
	WDFWAITLOCK	lock;
} ctx_vusb_t, *pctx_vusb_t;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(ctx_vusb_t, TO_VUSB)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(pctx_vusb_t, TO_PVUSB)

typedef struct _ctx_ep
{
	pctx_vusb_t	vusb;
	UCHAR		type, addr, interval;
	UDECXUSBENDPOINT	ude_ep;
	WDFQUEUE	queue;
} ctx_ep_t, *pctx_ep_t;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(ctx_ep_t, TO_EP)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(pctx_ep_t, TO_PEP)

EXTERN_C_END