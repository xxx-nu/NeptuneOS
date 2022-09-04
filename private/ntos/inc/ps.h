#pragma once

#include <nt.h>
#include "mm.h"
#include "ex.h"
#include "ob.h"
#include "cm.h"

/* Generated by syssvc-gen.py. Needed by the THREAD struct definition. */
#include <ntos_svc_params_gen.h>

#define NTOS_TCB_CAP			(seL4_CapInitThreadTCB)

/* Initial CNode for client processes has 256 slots */
#define PROCESS_INIT_CNODE_LOG2SIZE	(8)

compile_assert(CNODE_USEDMAP_NOT_AT_LEAST_ONE_MWORD,
	       (1ULL << PROCESS_INIT_CNODE_LOG2SIZE) >= MWORD_BITS);

#define NTOS_PS_TAG		EX_POOL_TAG('n', 't', 'p', 's')

/* Symbol name for the ntdll user exception entry point */
#define USER_EXCEPTION_DISPATCHER_NAME "KiUserExceptionDispatcher"

/*
 * Thread object
 */
typedef struct _THREAD {
    CAP_TREE_NODE TreeNode;	/* Must be first member */
    IPC_ENDPOINT ReplyEndpoint;
    struct _PROCESS *Process;
    LIST_ENTRY ThreadListEntry;	/* List link for the PROCESS object's ThreadList */
    LIST_ENTRY QueuedApcList; /* List of all queued APCs. Note the objects in this
			       * list are the APC objects that have been queued on
			       * this THREAD object (not just registered, but queued
			       * due to, say, timer expiry). */
    LIST_ENTRY TimerApcList; /* List of all registered timer APCs. Note the objects in
			      * this list are the TIMER objects. Note additionally that
			      * the APCs here may or may not be queued (this depends on
			      * whether they have expired). */
    PIPC_ENDPOINT SystemServiceEndpoint;
    PIPC_ENDPOINT WdmServiceEndpoint;
    PIPC_ENDPOINT FaultEndpoint;
    PCSTR DebugName;
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
    BOOLEAN InitialThread;
    BOOLEAN Suspended; /* TRUE if the thread has been suspended due to async await */
    BOOLEAN Alertable; /* TRUE if we can deliver APC to the thread */
    LIST_ENTRY PendingIrpList;	/* List of pending IO packets. The objects of this list
				 * are PENDING_IRP. List entry is PENDING_IRP.Link. */
    LIST_ENTRY ReadyListLink; /* Links all threads that are ready to be resumed. */
    KWAIT_BLOCK RootWaitBlock; /* Root wait condition to satisfy in order to unblock the thread. */
    ASYNC_STACK AsyncStack; /* Stack of asynchronous call frames, starting from the service handler */
    ULONG SvcNum;	    /* Saved service number */
    BOOLEAN WdmSvc;	    /* Saved service is WDM service */
    ULONG MsgBufferEnd;	    /* Offset to the end of the message buffer after all parameters
			     * have been marshaled (but before any APC objects are marshaled).*/
    NTSTATUS ExitStatus;    /* Exit status of thread */
    SAVED_SERVICE_PARAMETERS SavedParams;
} THREAD, *PTHREAD;

/*
 * Process object
 */
typedef struct _PROCESS {
    BOOLEAN Initialized; /* FALSE when process is first created. TRUE once
			  * the initial thread has been successfully created. */
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
    NOTIFICATION DpcMutex;
    NOTIFICATION SystemAdapterMutex;
    /* TODO: */
    ULONG_PTR AffinityMask;
    ULONG_PTR InheritedFromUniqueProcessId;
    ULONG_PTR BasePriority;
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
    PVOID EntryPoint;
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
			      IN PSYSTEM_THREAD_ENTRY EntryPoint,
			      IN BOOLEAN Suspended);
NTSTATUS PsCreateProcess(IN PIO_FILE_OBJECT ImageFile,
			 IN PIO_DRIVER_OBJECT DriverObject,
			 IN PSECTION ImageSection,
			 OUT PPROCESS *pProcess);
NTSTATUS PsLoadDll(IN PPROCESS Process,
		   IN PCSTR DllName);
NTSTATUS PsResumeThread(IN PTHREAD Thread);
NTSTATUS PsResumeSystemThread(IN PSYSTEM_THREAD Thread);
NTSTATUS PsMapDriverCoroutineStack(IN PPROCESS Process,
				   OUT MWORD *pStackTop);
NTSTATUS PsSetThreadPriority(IN PTHREAD Thread,
			     IN THREAD_PRIORITY Priority);
NTSTATUS PsSetSystemThreadPriority(IN PSYSTEM_THREAD Thread,
				   IN THREAD_PRIORITY Priority);

/* kill.c */
NTSTATUS PsTerminateThread(IN PTHREAD Thread,
    			   IN NTSTATUS ExitStatus);
NTSTATUS PsTerminateSystemThread(IN PSYSTEM_THREAD Thread);
