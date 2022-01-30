/*
 * PNP Root Enumerator (Root Bus) Driver
 *
 * This driver implements the root bus, also known as the root enumerator,
 * of the Plug and Play subsystem. On Windows this is part of the NTOSKRNL
 * PNP manager, whereas here in NeptuneOS the root bus is a standard PNP
 * driver that implements the PNP bus driver interface.
 */

#include <assert.h>
#include <wdm.h>

#define PNP_ROOT_ENUMERATOR_U	L"\\Device\\pnp"

typedef struct _PNP_DEVICE {
    // Device ID
    PCWSTR DeviceID;
    // Instance ID
    PCWSTR InstanceID;
    // Device description
    PCWSTR DeviceDescription;
    // Resource requirement list
    PIO_RESOURCE_REQUIREMENTS_LIST ResourceRequirementsList;
    // Associated resource list
    PCM_RESOURCE_LIST ResourceList;
    ULONG ResourceListSize;
} PNP_DEVICE, *PPNP_DEVICE;

typedef struct _PNP_DEVICE_EXTENSION {
    PPNP_DEVICE DeviceInfo; /* If NULL, device is the root enumerator */
} PNP_DEVICE_EXTENSION, *PPNP_DEVICE_EXTENSION;

static PNP_DEVICE I8042DeviceInfo = {
    .DeviceID = L"PNP0303",
    .InstanceID = L"0"
};

/*
 * PNP QueryDeviceRelations handler for the root enumerator device
 */
static NTSTATUS PnpRootQueryDeviceRelations(IN PDEVICE_OBJECT DeviceObject,
					    IN PIRP Irp)
{
    DPRINT("PnpRootQueryDeviceRelations(FDO %p, Irp %p)\n",
	   DeviceObject, Irp);

    PIO_STACK_LOCATION IoSp = IoGetCurrentIrpStackLocation(Irp);
    if (IoSp->Parameters.QueryDeviceRelations.Type != BusRelations) {
	return STATUS_INVALID_PARAMETER;
    }

    ULONG Size = sizeof(DEVICE_RELATIONS) + sizeof(PDEVICE_OBJECT);
    PDEVICE_RELATIONS Relations = (PDEVICE_RELATIONS)ExAllocatePool(Size);
    NTSTATUS Status;
    if (!Relations) {
	DPRINT("ExAllocatePool() failed\n");
	Status = STATUS_NO_MEMORY;
	goto out;
    }

    /* For now we simply hard-code the only device that we will create:
     * the PS/2 keyboard. */
    PDEVICE_OBJECT Pdo = NULL;
    Status = IoCreateDevice(DeviceObject->DriverObject,
			    sizeof(PNP_DEVICE_EXTENSION),
			    NULL, FILE_DEVICE_8042_PORT,
			    FILE_AUTOGENERATED_DEVICE_NAME,
			    FALSE, &Pdo);
    if (!NT_SUCCESS(Status)) {
	DPRINT("IoCreateDevice() failed with status 0x%08x\n",
	       Status);
	goto out;
    }
    PPNP_DEVICE_EXTENSION DeviceExtension = (PPNP_DEVICE_EXTENSION)Pdo->DeviceExtension;
    DeviceExtension->DeviceInfo = &I8042DeviceInfo;
    Relations->Count = 1;
    Relations->Objects[0] = Pdo;
    Irp->IoStatus.Information = (ULONG_PTR)Relations;
    Status = STATUS_SUCCESS;

out:
    if (!NT_SUCCESS(Status)) {
	if (Relations)
	    ExFreePool(Relations);
	if (Pdo) {
	    IoDeleteDevice(Pdo);
	}
    }

    return Status;
}

/*
 * PNP QueryDeviceRelations handler for the child devices
 */
static NTSTATUS PnpDeviceQueryDeviceRelations(IN PDEVICE_OBJECT DeviceObject,
					      IN PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);

    if (IrpSp->Parameters.QueryDeviceRelations.Type != TargetDeviceRelation)
        return STATUS_INVALID_DEVICE_REQUEST;

    DPRINT("IRP_MJ_PNP / IRP_MN_QUERY_DEVICE_RELATIONS / TargetDeviceRelation\n");
    ULONG Size = sizeof(DEVICE_RELATIONS) + sizeof(PDEVICE_OBJECT);
    PDEVICE_RELATIONS Relations = (PDEVICE_RELATIONS)ExAllocatePool(Size);
    if (!Relations) {
        DPRINT("ExAllocatePoolWithTag() failed\n");
        return STATUS_NO_MEMORY;
    } else {
        Relations->Count = 1;
        Relations->Objects[0] = DeviceObject;
        Irp->IoStatus.Information = (ULONG_PTR)Relations;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS PnpDeviceQueryCapabilities(IN PDEVICE_OBJECT DeviceObject,
					 IN PIRP Irp)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS PnpDeviceRemoveDevice(IN PDEVICE_OBJECT DeviceObject,
				      IN PIRP Irp)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS PnpDeviceQueryPnpDeviceState(IN PDEVICE_OBJECT DeviceObject,
					     IN PIRP Irp)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS PnpDeviceQueryResources(IN PDEVICE_OBJECT DeviceObject,
					IN PIRP Irp)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS PnpDeviceQueryResourceRequirements(IN PDEVICE_OBJECT DeviceObject,
						   IN PIRP Irp)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS PnpDeviceQueryId(IN PDEVICE_OBJECT Pdo,
				 IN PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PPNP_DEVICE_EXTENSION DeviceExtension = (PPNP_DEVICE_EXTENSION)Pdo->DeviceExtension;
    BUS_QUERY_ID_TYPE IdType = IrpSp->Parameters.QueryId.IdType;
    NTSTATUS Status;

    switch (IdType) {
        case BusQueryDeviceID:
	{
	    WCHAR Buffer[128];
	    ULONG Length = _snwprintf(Buffer, ARRAYSIZE(Buffer), L"Root\\%s",
				      DeviceExtension->DeviceInfo->DeviceID);
	    UNICODE_STRING StringBuffer = {
		.Buffer = Buffer,
		.Length = Length * sizeof(WCHAR),
		.MaximumLength = Length * sizeof(WCHAR)
	    };
            UNICODE_STRING String;
            DPRINT("IRP_MJ_PNP / IRP_MN_QUERY_ID / BusQueryDeviceID\n");

            Status = RtlDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE,
					       &StringBuffer, &String);
            Irp->IoStatus.Information = (ULONG_PTR)String.Buffer;
            break;
	}

        case BusQueryHardwareIDs:
        case BusQueryCompatibleIDs:
            /* Optional, do nothing */
            break;

        case BusQueryInstanceID:
	{
            DPRINT("IRP_MJ_PNP / IRP_MN_QUERY_ID / BusQueryInstanceID\n");

	    UNICODE_STRING String;
	    Status = RtlCreateUnicodeString(&String, DeviceExtension->DeviceInfo->InstanceID);
            Irp->IoStatus.Information = (ULONG_PTR)String.Buffer;
            break;
	}

        default:
	    Status = STATUS_INVALID_DEVICE_REQUEST;
            DPRINT1("IRP_MJ_PNP / IRP_MN_QUERY_ID / unknown query id type 0x%x\n", IdType);
    }

    return Status;
}

static NTSTATUS PnpDeviceQueryDeviceText(IN PDEVICE_OBJECT DeviceObject,
					 IN PIRP Irp)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS PnpDeviceFilterResourceRequirements(IN PDEVICE_OBJECT DeviceObject,
						    IN PIRP Irp)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS PnpDeviceQueryBusInformation(IN PDEVICE_OBJECT DeviceObject,
					     IN PIRP Irp)
{
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS PnpDeviceQueryDeviceUsageNotification(IN PDEVICE_OBJECT DeviceObject,
						      IN PIRP Irp)
{
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * PNP dispatch function for the root enumerator device
 */
static NTSTATUS PnpRootDispatch(IN PDEVICE_OBJECT DeviceObject,
				IN PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;
    switch (IrpSp->MinorFunction) {
    case IRP_MN_START_DEVICE:
	Status = STATUS_SUCCESS;
	break;

    case IRP_MN_QUERY_DEVICE_RELATIONS:
	Status = PnpRootQueryDeviceRelations(DeviceObject, Irp);
	break;

    default:
	Status = STATUS_INVALID_DEVICE_REQUEST;
	DPRINT("Invalid PnP code for root enumerator: %X\n",
	       IrpSp->MinorFunction);
	break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

/*
 * PNP dispatch function for the child devices
 */
static NTSTATUS PnpDeviceDispatch(IN PDEVICE_OBJECT DeviceObject,
				  IN PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;
    switch (IrpSp->MinorFunction) {
    case IRP_MN_START_DEVICE:
	Status = STATUS_SUCCESS;
	break;

    case IRP_MN_STOP_DEVICE:
	Status = STATUS_SUCCESS;
	break;

    case IRP_MN_QUERY_STOP_DEVICE:
	Status = STATUS_SUCCESS;
	break;

    case IRP_MN_QUERY_REMOVE_DEVICE:
	Status = STATUS_SUCCESS;
	break;

    case IRP_MN_QUERY_DEVICE_RELATIONS:
	Status = PnpDeviceQueryDeviceRelations(DeviceObject, Irp);
	break;

    case IRP_MN_QUERY_CAPABILITIES:
	Status = PnpDeviceQueryCapabilities(DeviceObject, Irp);
	break;

    case IRP_MN_SURPRISE_REMOVAL:
	Status = PnpDeviceRemoveDevice(DeviceObject, Irp);
	break;

    case IRP_MN_REMOVE_DEVICE:
	Status = PnpDeviceRemoveDevice(DeviceObject, Irp);
	break;

    case IRP_MN_QUERY_PNP_DEVICE_STATE:
	Status = PnpDeviceQueryPnpDeviceState(DeviceObject, Irp);
	break;

    case IRP_MN_QUERY_RESOURCES:
	Status = PnpDeviceQueryResources(DeviceObject, Irp);
	break;

    case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
	Status = PnpDeviceQueryResourceRequirements(DeviceObject, Irp);
	break;

    case IRP_MN_QUERY_ID:
	Status = PnpDeviceQueryId(DeviceObject, Irp);
	break;

    case IRP_MN_QUERY_DEVICE_TEXT:
	Status = PnpDeviceQueryDeviceText(DeviceObject, Irp);
	break;

    case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
	Status = PnpDeviceFilterResourceRequirements(DeviceObject, Irp);
	break;

    case IRP_MN_QUERY_BUS_INFORMATION:
	Status = PnpDeviceQueryBusInformation(DeviceObject, Irp);
	break;

    case IRP_MN_DEVICE_USAGE_NOTIFICATION:
	Status = PnpDeviceQueryDeviceUsageNotification(DeviceObject, Irp);
	break;

    case IRP_MN_CANCEL_REMOVE_DEVICE:
    case IRP_MN_CANCEL_STOP_DEVICE:
	Status = STATUS_SUCCESS;
	break;

    default:
	Status = STATUS_INVALID_DEVICE_REQUEST;
	DPRINT("Unknown PnP code: %X\n", IrpSp->MinorFunction);
	break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

NTAPI NTSTATUS PnpDispatch(IN PDEVICE_OBJECT DeviceObject,
			   IN PIRP Irp)
{
    PPNP_DEVICE_EXTENSION DeviceExtension = (PPNP_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    assert(DeviceExtension != NULL);
    if (DeviceExtension->DeviceInfo != NULL) {
	/* Device is a child device enumerated by the root bus */
	return PnpDeviceDispatch(DeviceObject, Irp);
    }
    /* Otherwise, device is the root enumerator itself */
    return PnpRootDispatch(DeviceObject, Irp);
}

NTAPI NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject,
			   IN PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    /* Create the root enumerator device object */
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(PNP_ROOT_ENUMERATOR_U);
    PDEVICE_OBJECT RootEnumerator;
    NTSTATUS Status = IoCreateDevice(DriverObject, sizeof(PNP_DEVICE_EXTENSION),
				     &DeviceName, FILE_DEVICE_BUS_EXTENDER, 0, FALSE,
				     &RootEnumerator);

    if (!NT_SUCCESS(Status))
	return Status;

    DriverObject->MajorFunction[IRP_MJ_PNP] = PnpDispatch;

    return STATUS_SUCCESS;
}
