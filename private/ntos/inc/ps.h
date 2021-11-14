#pragma once

#include <nt.h>
#include "mm.h"
#include "ex.h"
#include "ob.h"

/* Generated by syssvc-gen.py. Needed by the THREAD struct definition. */
#include <ntos_svc_params_gen.h>

#define NTEX_TCB_CAP			(seL4_CapInitThreadTCB)

/* Initial CNode for client processes has 256 slots */
#define PROCESS_INIT_CNODE_LOG2SIZE	(8)

compile_assert(CNODE_USEDMAP_NOT_AT_LEAST_ONE_MWORD,
	       (1ULL << PROCESS_INIT_CNODE_LOG2SIZE) >= MWORD_BITS);

#define NTOS_PS_TAG		EX_POOL_TAG('n', 't', 'p', 's')

/* Not to be confused with CONTEXT, defined in the NT headers */
typedef seL4_UserContext THREAD_CONTEXT, *PTHREAD_CONTEXT;
typedef ULONG THREAD_PRIORITY;

/*
 * Thread object
 */
typedef struct _THREAD {
    CAP_TREE_NODE TreeNode;
    IPC_ENDPOINT ReplyEndpoint;
    struct _PROCESS *Process;
    LIST_ENTRY ThreadListEntry;
    PIPC_ENDPOINT SystemServiceEndpoint;
    PIPC_ENDPOINT HalServiceEndpoint;
    MWORD IpcBufferClientAddr;
    MWORD IpcBufferServerAddr;
    MWORD TebClientAddr;
    MWORD TebServerAddr;
    MWORD SystemDllTlsBase; /* Address in the client's virtual address space */
    MWORD StackTop;
    MWORD StackReserve;
    MWORD StackCommit;
    THREAD_PRIORITY CurrentPriority;
    NTDLL_THREAD_INIT_INFO InitInfo;
    BOOLEAN Suspended; /* TRUE if the thread has been suspended due to async await */
    PIO_REQUEST_PACKET PendingIrp; /* IRP that the thread is waiting for a response for.
				    * There can only be one pending IRP per thread.
				    * For driver processes, the IRPs from higher-level
				    * IoCallDriver are queued on the driver object. */
    IO_STATUS_BLOCK IoResponseStatus; /* Response status to the pending IRP. */
    KEVENT IoCompletionEvent; /* Signaled when the IO request has been completed. */
    LIST_ENTRY ReadyListLink; /* Links all threads that are ready to be resumed. */
    KWAIT_BLOCK RootWaitBlock; /* Root wait condition to satisfy in order to unblock the thread. */
    ASYNC_STACK AsyncStack; /* Stack of asynchronous state, starting from the service handler */
    ULONG SvcNum;	    /* Saved service number */
    BOOLEAN HalSvc;	    /* Saved service is HAL service */
    union {
	SYSTEM_SERVICE_PARAMETERS SysSvcParams;
	HAL_SERVICE_PARAMETERS HalSvcParams;
    };
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
} PROCESS, *PPROCESS;

/* init.c */
NTSTATUS PsInitSystemPhase0();
NTSTATUS PsInitSystemPhase1();

/* create.c */
NTSTATUS PsCreateThread(IN PPROCESS Process,
			OUT PTHREAD *pThread);
NTSTATUS PsCreateProcess(IN PIO_FILE_OBJECT ImageFile,
			 IN PIO_DRIVER_OBJECT DriverObject,
			 OUT PPROCESS *pProcess);
NTSTATUS PsLoadDll(IN PPROCESS Process,
		   IN PCSTR DllName);
