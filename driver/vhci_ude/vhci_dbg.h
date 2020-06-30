#pragma once

#include "dbgcommon.h"

#ifdef DBG

#include "vhci_dev.h"
#include "vhci_urbr.h"
#include "dbgcode.h"

extern const char *dbg_vhci_ioctl_code(unsigned int ioctl_code);
extern const char *dbg_urbfunc(USHORT urbfunc);
extern const char *dbg_usb_setup_packet(PCUCHAR packet);
extern const char *dbg_urbr(purb_req_t urbr);

#endif	
