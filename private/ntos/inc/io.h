#pragma once

#define NTOS_IO_TAG	(EX_POOL_TAG('n','t','i','o'))

#define DRIVER_OBJECT_DIRECTORY		"\\Driver"
#define DEVICE_OBJECT_DIRECTORY		"\\Device"

struct _PROCESS;
struct _IO_FILE_OBJECT;

/*
 * Server-side object of the client side DRIVER_OBJECT.
 */
typedef struct _IO_DRIVER_OBJECT {
    PCSTR DriverImageName;
    LIST_ENTRY DeviceList;    /* All devices created by this driver */
    struct _IO_FILE_OBJECT *DriverFile;
    struct _PROCESS *DriverProcess;   /* TODO: We need to figure out Driver and Mini-driver */
    struct _THREAD *MainEventLoopThread; /* Main event loop thread of the driver process */
    LIST_ENTRY IrpQueue; /* IRPs queued on this driver object but has not been processed yet. */
    LIST_ENTRY PendingIrpList;	/* IRPs that have already been moved to driver process's
				 * incoming IRP buffer. Note that the driver may choose to
				 * save this IRP to its internal buffer and withhold the
				 * response until much later (say, after several calls to
				 * IopRequestIrp). Therefore this list does NOT in general
				 * equal the IRPs in the driver's in/out IRP buffer. */
    KEVENT InitializationDoneEvent; /* Signaled when the client process starts accepting IRP */
    KEVENT IrpQueuedEvent;	    /* Signaled when an IRP is queued on the driver object. */
    MWORD IncomingIrpServerAddr; /* IO Request Packets sent to the driver */
    MWORD IncomingIrpClientAddr;
    MWORD OutgoingIrpServerAddr; /* Driver's replies */
    MWORD OutgoingIrpClientAddr;
    ULONG NumRequestPackets; /* Number of IO request packets currently in the incoming IRP buffer */
} IO_DRIVER_OBJECT, *PIO_DRIVER_OBJECT;

/*
 * Server-side object of the client side DEVICE_OBJECT
 */
typedef struct _IO_DEVICE_OBJECT {
    PCSTR DeviceName;
    PIO_DRIVER_OBJECT DriverObject;
    LIST_ENTRY DeviceLink; /* Links all devices created by the driver object */
    DEVICE_TYPE DeviceType;
    ULONG DeviceCharacteristics;
    BOOLEAN Exclusive;
} IO_DEVICE_OBJECT, *PIO_DEVICE_OBJECT;

typedef struct _SECTION_OBJECT_POINTERS {
    PDATA_SECTION_OBJECT DataSectionObject;
    PIMAGE_SECTION_OBJECT ImageSectionObject;
} SECTION_OBJECT_POINTERS;

/*
 * Server-side object of the client side FILE_OBJECT. Represents
 * an open instance of a DEVICE_OBJECT.
 */
typedef struct _IO_FILE_OBJECT {
    PIO_DEVICE_OBJECT DeviceObject;
    PCSTR FileName;
    SECTION_OBJECT_POINTERS SectionObject;
    PVOID BufferPtr;
    MWORD Size;
    BOOLEAN ReadAccess;
    BOOLEAN WriteAccess;
    BOOLEAN DeleteAccess;
    BOOLEAN SharedRead;
    BOOLEAN SharedWrite;
    BOOLEAN SharedDelete;
    ULONG Flags;
} IO_FILE_OBJECT, *PIO_FILE_OBJECT;

/*
 * Forward declarations.
 */

/* init.c */
NTSTATUS IoInitSystemPhase0();
NTSTATUS IoInitSystemPhase1();

/* create.c */
NTSTATUS IoCreateFile(IN PCSTR FileName,
		      IN PVOID BufferPtr,
		      IN MWORD FileSize,
		      OUT PIO_FILE_OBJECT *pFile);
