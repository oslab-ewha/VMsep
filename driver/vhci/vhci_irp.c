#include "vhci.h"

PAGEABLE NTSTATUS
irp_pass_down(PDEVICE_OBJECT objdev, PIRP irp)
{
	irp->IoStatus.Status = STATUS_SUCCESS;
	IoSkipCurrentIrpStackLocation(irp);
	return IoCallDriver(objdev, irp);
}