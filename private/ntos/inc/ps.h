#pragma once

#include <nt.h>
#include "mm.h"
#include "ex.h"
#include "ob.h"
#include "cm.h"

/* Generated by syssvc-gen.py. Needed by the THREAD struct definition. */
#include <ntos_svc_params_gen.h>

#define NTEX_TCB_CAP			(seL4_CapInitThreadTCB)

/* Initial CNode for client processes has 256 slots */
#define PROCESS_INIT_CNODE_LOG2SIZE	(8)

compile_assert(CNODE_USEDMAP_NOT_AT_LEAST_ONE_MWORD,
	       (1ULL << PROCESS_INIT_CNODE_LOG2SIZE) >= MWORD_BITS);

#define NTOS_PS_TAG		EX_POOL_TAG('n', 't', 'p', 's')

/*
 * Thread object
 */
typedef struct _THREAD {
    CAP_TREE_NODE TreeNode;
    IPC_ENDPOINT ReplyEndpoint;
    struct _PROCESS *Process;
    LIST_ENTRY ThreadListEntry;
    LIST_ENTRY ApcList;
    PIPC_ENDPOINT SystemServiceEndpoint;
    PIPC_ENDPOINT WdmServiceEndpoint;
    PIPC_ENDPOINT FaultEndpoint;
    PVOID EntryPoint;
    MWORD IpcBufferClientAddr;
    MWORD IpcBufferServerAddr;
    MWORD TebClientAddr;
    MWORD TebServerAddr;
    MWORD SystemDllTlsBase; /* Address in the client's virtual address space */
    MWORD InitialStackTop;
    MWORD InitialStackReserve;
    MWORD InitialStackCommit;
    THREAD_PRIORITY CurrentPriority;
    NTDLL_THREAD_INIT_INFO InitInfo;
    BOOLEAN Suspended; /* TRUE if the thread has been suspended due to async await */
    BOOLEAN Alertable; /* TRUE if we can deliver APC to the thread */
    PIO_PACKET PendingIoPacket; /* IO packet that the thread is waiting for a response for.
				 * There can only be one pending IO packet per thread.
				 * For driver processes, the IO packets from higher-level
				 * IoCallDriver calls are queued on the driver object.
				 * The pending IO packet must be of type IoPacketTypeRequest. */
    IO_STATUS_BLOCK IoResponseStatus; /* Response status to the pending IO packet. */
    KEVENT IoCompletionEvent; /* Signaled when the IO request has been completed. */
    LIST_ENTRY ReadyListLink; /* Links all threads that are ready to be resumed. */
    KWAIT_BLOCK RootWaitBlock; /* Root wait condition to satisfy in order to unblock the thread. */
    ASYNC_STACK AsyncStack; /* Stack of asynchronous state, starting from the service handler */
    ULONG SvcNum;	    /* Saved service number */
    BOOLEAN WdmSvc;	    /* Saved service is WDM service */
    NTSTATUS ExitStatus;    /* Exit status of thread */
    union {
	SYSTEM_SERVICE_PARAMETERS SysSvcParams;
	WDM_SERVICE_PARAMETERS WdmSvcParams;
    };
    union {
	/* There can only be one system service being served at any point, so
	 * it's safe to put their saved states in one union */
	struct {
	    PIO_DRIVER_OBJECT DriverObject;
	} NtLoadDriverSavedState;
	struct {
	    IO_OPEN_CONTEXT OpenContext;
	} NtCreateFileSavedState;
	struct {
	    IO_OPEN_CONTEXT OpenContext;
	} NtOpenFileSavedState;
	struct {
	    CM_OPEN_CONTEXT OpenContext;
	} NtCreateKeySavedState;
	struct {
	    CM_OPEN_CONTEXT OpenContext;
	} NtOpenKeySavedState;
    };
    struct {
	POBJECT Object;
	POBJECT UserRootDirectory;
	PCSTR Path;
	BOOLEAN Reparsed;
    } ObOpenObjectSavedState;
} THREAD, *PTHREAD;

/*
 * Process object
 */
typedef struct _PROCESS {
    PTHREAD InitThread;
    LIST_ENTRY ThreadList;
    PCNODE CSpace;
    HANDLE_TABLE HandleTable;
    VIRT_ADDR_SPACE VSpace;	/* Virtual address space of the process */
    PIO_FILE_OBJECT ImageFile;
    PSECTION ImageSection;
    MWORD ImageBaseAddress;
    MWORD ImageVirtualSize;
    LIST_ENTRY ProcessListEntry;
    MWORD PebClientAddr;
    MWORD PebServerAddr;
    MWORD LoaderSharedDataClientAddr;
    MWORD LoaderSharedDataServerAddr;
    NTDLL_PROCESS_INIT_INFO InitInfo;
    PIO_DRIVER_OBJECT DriverObject; /* TODO: Mini-driver? */
    NTSTATUS ExitStatus;	    /* Exit status of process */
    ULONG Cookie;
} PROCESS, *PPROCESS;

/*
 * System thread object
 *
 * A system thread is a thread of the NTOS Executive root task, usually used
 * for interrupt handling.
 */
typedef struct _SYSTEM_THREAD {
    CAP_TREE_NODE TreeNode;
    IPC_ENDPOINT ReplyEndpoint;
    PIPC_ENDPOINT FaultEndpoint;
    PCSTR DebugName;
    PVOID TlsBase;	 /* TLS base of the thread's TLS region */
    PVOID IpcBuffer;	 /* Address of the thread's seL4 IPC buffer */
    PVOID StackTop;	 /* Stack of the thread */
    THREAD_PRIORITY CurrentPriority;
} SYSTEM_THREAD, *PSYSTEM_THREAD;

typedef VOID (*PSYSTEM_THREAD_ENTRY)();

/* init.c */
NTSTATUS PsInitSystemPhase0();
NTSTATUS PsInitSystemPhase1();
PKUSER_SHARED_DATA PsGetUserSharedData();

/* create.c */
NTSTATUS PsCreateThread(IN PPROCESS Process,
                        IN PCONTEXT ThreadContext,
                        IN PINITIAL_TEB InitialTeb,
                        IN BOOLEAN CreateSuspended,
			OUT PTHREAD *pThread);
NTSTATUS PsCreateSystemThread(IN PSYSTEM_THREAD Thread,
			      IN PCSTR DebugName,
			      IN PSYSTEM_THREAD_ENTRY EntryPoint);
NTSTATUS PsCreateProcess(IN PIO_FILE_OBJECT ImageFile,
			 IN PIO_DRIVER_OBJECT DriverObject,
			 IN PSECTION ImageSection,
			 OUT PPROCESS *pProcess);
NTSTATUS PsLoadDll(IN PPROCESS Process,
		   IN PCSTR DllName);
NTSTATUS PsResumeThread(IN PTHREAD Thread);

/* kill.c */
NTSTATUS PsTerminateThread(IN PTHREAD Thread,
    			   IN NTSTATUS ExitStatus);
NTSTATUS PsTerminateSystemThread(IN PSYSTEM_THREAD Thread);
