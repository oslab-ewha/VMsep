#include "vhci.h"

#include "vhci_dev.h"
<<<<<<< HEAD
#include "vhci_irp.h"

static NTSTATUS
vhci_power_vhci(pvhci_dev_t vhci, PIRP irp, PIO_STACK_LOCATION irpstack)
{
	POWER_STATE		powerState;
	POWER_STATE_TYPE	powerType;
=======

static NTSTATUS
vhci_power_vhub(pusbip_vhub_dev_t vhub, PIRP Irp)
{
	PIO_STACK_LOCATION	stack;
	POWER_STATE		powerState;
	POWER_STATE_TYPE	powerType;
	NTSTATUS		status;
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo

	powerType = irpstack->Parameters.Power.Type;
	powerState = irpstack->Parameters.Power.State;

<<<<<<< HEAD
	// If the device is not stated yet, just pass it down.
	if (vhci->common.DevicePnPState == NotStarted) {
		return irp_pass_down(vhci->common.devobj_lower, irp);
=======
	inc_io_vhub(vhub);

	// If the device is not stated yet, just pass it down.
	if (vhub->common.DevicePnPState == NotStarted) {
		PoStartNextPowerIrp(Irp);
		IoSkipCurrentIrpStackLocation(Irp);
		status = PoCallDriver(vhub->NextLowerDriver, Irp);
		dec_io_vhub(vhub);
		return status;

>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
	}

	if (irpstack->MinorFunction == IRP_MN_SET_POWER) {
		DBGI(DBG_POWER, "\tRequest to set %s state to %s\n",
			((powerType == SystemPowerState) ? "System" : "Device"),
			((powerType == SystemPowerState) ? \
				dbg_system_power(powerState.SystemState) : \
				dbg_device_power(powerState.DeviceState)));
	}

<<<<<<< HEAD
	return irp_pass_down(vhci->common.devobj_lower, irp);
}

static NTSTATUS
vhci_power_vdev(pvdev_t vdev, PIRP irp, PIO_STACK_LOCATION irpstack)
{
=======
	PoStartNextPowerIrp(Irp);
	IoSkipCurrentIrpStackLocation(Irp);
	status = PoCallDriver(vhub->NextLowerDriver, Irp);
	dec_io_vhub(vhub);
	return status;
}

static NTSTATUS
vhci_power_vpdo(pusbip_vpdo_dev_t vpdo, PIRP Irp)
{
	PIO_STACK_LOCATION	stack;
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
	POWER_STATE		powerState;
	POWER_STATE_TYPE	powerType;
	NTSTATUS		status;

	powerType = irpstack->Parameters.Power.Type;
	powerState = irpstack->Parameters.Power.State;

	switch (irpstack->MinorFunction) {
	case IRP_MN_SET_POWER:
		DBGI(DBG_POWER, "\tSetting %s power state to %s\n",
			((powerType == SystemPowerState) ? "System" : "Device"),
			((powerType == SystemPowerState) ? \
				dbg_system_power(powerState.SystemState) : \
				dbg_device_power(powerState.DeviceState)));

		switch (powerType) {
		case DevicePowerState:
<<<<<<< HEAD
			PoSetPowerState(vdev->Self, powerType, powerState);
			vdev->DevicePowerState = powerState.DeviceState;
=======
			PoSetPowerState(vpdo->common.Self, powerType, powerState);
			vpdo->common.DevicePowerState = powerState.DeviceState;
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
			status = STATUS_SUCCESS;
			break;

		case SystemPowerState:
<<<<<<< HEAD
			vdev->SystemPowerState = powerState.SystemState;
=======
			vpdo->common.SystemPowerState = powerState.SystemState;
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
			status = STATUS_SUCCESS;
			break;

		default:
			status = STATUS_NOT_SUPPORTED;
			break;
		}
		break;
	case IRP_MN_QUERY_POWER:
		status = STATUS_SUCCESS;
		break;
	case IRP_MN_WAIT_WAKE:
		// We cannot support wait-wake because we are root-enumerated
		// driver, and our parent, the PnP manager, doesn't support wait-wake.
		// If you are a bus enumerated device, and if  your parent bus supports
		// wait-wake,  you should send a wait/wake IRP (PoRequestPowerIrp)
		// in response to this request.
		// If you want to test the wait/wake logic implemented in the function
		// driver (USBIP.sys), you could do the following simulation:
		// a) Mark this IRP pending.
		// b) Set a cancel routine.
		// c) Save this IRP in the device extension
		// d) Return STATUS_PENDING.
		// Later on if you suspend and resume your system, your vhci_power()
		// will be called to power the bus. In response to IRP_MN_SET_POWER, if the
		// powerstate is PowerSystemWorking, complete this Wake IRP.
		// If the function driver, decides to cancel the wake IRP, your cancel routine
		// will be called. There you just complete the IRP with STATUS_CANCELLED.
	case IRP_MN_POWER_SEQUENCE:
	default:
		status = STATUS_NOT_SUPPORTED;
		break;
	}

	if (status != STATUS_NOT_SUPPORTED) {
<<<<<<< HEAD
		irp->IoStatus.Status = status;
=======
		Irp->IoStatus.Status = status;
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
	}

	status = irp->IoStatus.Status;
	IoCompleteRequest(irp, IO_NO_INCREMENT);

	return status;
}

NTSTATUS
<<<<<<< HEAD
vhci_power(__in PDEVICE_OBJECT devobj, __in PIRP irp)
{
	pvdev_t	vdev = DEVOBJ_TO_VDEV(devobj);
	PIO_STACK_LOCATION	irpstack;
	NTSTATUS		status;

	irpstack = IoGetCurrentIrpStackLocation(irp);
=======
vhci_power(__in PDEVICE_OBJECT devobj, __in PIRP Irp)
{
	pdev_common_t	devcom;
	PIO_STACK_LOCATION	irpStack;
	NTSTATUS		status;

	DBGI(DBG_GENERAL | DBG_POWER, "vhci_power: Enter\n");
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo

	DBGI(DBG_GENERAL | DBG_POWER, "vhci_power(%s): Enter: minor:%s\n",
		dbg_vdev_type(DEVOBJ_VDEV_TYPE(devobj)), dbg_power_minor(irpstack->MinorFunction));

<<<<<<< HEAD
	status = STATUS_SUCCESS;

	// If the device has been removed, the driver should
	// not pass the IRP down to the next lower driver.
	if (vdev->DevicePnPState == Deleted) {
		PoStartNextPowerIrp(irp);
		irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return status;
	}

	switch (DEVOBJ_VDEV_TYPE(devobj)) {
	case VDEV_VHCI:
		status = vhci_power_vhci((pvhci_dev_t)vdev, irp, irpstack);
		break;
	default:
		status = vhci_power_vdev(vdev, irp, irpstack);
		break;
=======
	devcom = (pdev_common_t)devobj->DeviceExtension;

	// If the device has been removed, the driver should
	// not pass the IRP down to the next lower driver.
	if (devcom->DevicePnPState == Deleted) {
		PoStartNextPowerIrp (Irp);
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE ;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	if (devcom->is_vhub) {
		DBGI(DBG_POWER, "vhub: minor: %s IRP:0x%p %s %s\n",
		     dbg_power_minor(irpStack->MinorFunction), Irp,
		     dbg_system_power(devcom->SystemPowerState),
		     dbg_device_power(devcom->DevicePowerState));

		status = vhci_power_vhub((pusbip_vhub_dev_t)devobj->DeviceExtension, Irp);
	} else {
		DBGI(DBG_POWER, "vpdo: minor: %s IRP:0x%p %s %s\n",
			 dbg_power_minor(irpStack->MinorFunction), Irp,
			 dbg_system_power(devcom->SystemPowerState),
			 dbg_device_power(devcom->DevicePowerState));

		status = vhci_power_vpdo((pusbip_vpdo_dev_t)devobj->DeviceExtension, Irp);
>>>>>>> ccbd1a0... vhci code cleanup: vhub/vpdo instead of fdo/pdo
	}

	DBGI(DBG_GENERAL | DBG_POWER, "vhci_power(%s): Leave: status: %s\n",
		dbg_vdev_type(DEVOBJ_VDEV_TYPE(devobj)), dbg_ntstatus(status));

	return status;
}