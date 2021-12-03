#include "ki.h"

/* TODO: This is for x86 and PIC only. We don't support IOAPIC yet.
 * We need to move the hardware-dependent code into ntos/hal. */
#define TIMER_IRQ_LINE		0

/* TODO: Most BIOS set the frequency divider to either 65535 or 0 (representing
 * 65536). We assume it is 65536. We should really be setting the frequency
 * divider ourselves. */
#define TIMER_TICK_PER_SECOND	(1193182 >> 16)

static SYSTEM_THREAD KiTimerIrqThread;
static IRQ_HANDLER KiTimerIrqHandler;
static NOTIFICATION KiTimerIrqNotification;
static IPC_ENDPOINT KiTimerServiceNotification;

static PCSTR KiWeekdayString[] = {
    "Suny",
    "Mon",
    "Tue",
    "Wed",
    "Thu",
    "Fri",
    "Sat"
};

/* This is shared between the timer interrupt service and the main thread.
 * You must use interlocked operations to access this! */
static ULONGLONG POINTER_ALIGNMENT KiTimerTickCount;

/* The absolute system time at the time of system initialization (more
 * specifically, when the KiInitTimer function is executing). This is
 * initialized from the system RTC (which we assume is in UTC). The system
 * time is measured in units of 100 nano-seconds since the midnight of
 * January 1, 1601. */
static LARGE_INTEGER KiInitialSystemTime;

/*
 * Convert the number of timer ticks to the system time relative to the
 * beginning of the timer sub-component initialization (KiInitTime). The system
 * time is measured in units of 100 nano-seconds.
 */
static inline ULONGLONG KiTimerTickCountToSystemTime(IN ULONGLONG TickCount)
{
    return TickCount * 10000000 / TIMER_TICK_PER_SECOND + KiInitialSystemTime.QuadPart;
}

/* List of all timers */
static LIST_ENTRY KiTimerList;

static KMUTEX KiTimerDatabaseLock;
/* The following data structures (as well the timer objects in these lists) are
 * protected by the timer database lock */
static LIST_ENTRY KiQueuedTimerList;
static LIST_ENTRY KiExpiredTimerList;
/* END of timer database lock protected data structure */

static inline VOID KiAcquireTimerDatabaseLock()
{
    KeAcquireMutex(&KiTimerDatabaseLock);
}

static inline VOID KiReleaseTimerDatabaseLock()
{
    KeReleaseMutex(&KiTimerDatabaseLock);
}

static NTSTATUS KiCreateIrqHandler(IN PIRQ_HANDLER IrqHandler,
				   IN MWORD IrqLine)
{
    extern CNODE MiNtosCNode;
    assert(IrqHandler != NULL);
    MWORD Cap = 0;
    RET_ERR(MmAllocateCap(&MiNtosCNode, &Cap));
    assert(Cap != 0);
    int Error = seL4_IRQControl_Get(seL4_CapIRQControl, IrqLine,
				    MiNtosCNode.TreeNode.Cap,
				    Cap, MiNtosCNode.Depth);
    if (Error != 0) {
	MmDeallocateCap(&MiNtosCNode, Cap);
	KeDbgDumpIPCError(Error);
	return SEL4_ERROR(Error);
    }
    KiInitializeIrqHandler(IrqHandler, &MiNtosCNode, Cap, IrqLine);
    assert(Cap == IrqHandler->TreeNode.Cap);
    return STATUS_SUCCESS;
}

static NTSTATUS KiConnectIrqNotification(IN PIRQ_HANDLER IrqHandler,
					 IN PNOTIFICATION Notification)
{
    assert(IrqHandler != NULL);
    assert(Notification != NULL);
    int Error = seL4_IRQHandler_SetNotification(IrqHandler->TreeNode.Cap,
						Notification->TreeNode.Cap);
    if (Error != 0) {
	KeDbgDumpIPCError(Error);
	return SEL4_ERROR(Error);
    }
    return STATUS_SUCCESS;
}

/*
 * Entry point for the timer interrupt service thread
 */
static VOID KiTimerInterruptService()
{
    while (TRUE) {
	int AckError = seL4_IRQHandler_Ack(KiTimerIrqHandler.TreeNode.Cap);
	if (AckError != 0) {
	    DbgTrace("Failed to ACK timer interrupt. Error:");
	    KeDbgDumpIPCError(AckError);
	}
	KeWaitOnNotification(&KiTimerIrqNotification);
	ULONGLONG TimerTicks = InterlockedIncrement64((PLONG64)&KiTimerTickCount);
	/* If a timer has a due time smaller than MaxDueTime, then it has expired */
	ULONGLONG MaxDueTime = KiTimerTickCountToSystemTime(TimerTicks + 1);
	/* Traverse the queued timer list and see if any of them expired */
	KiAcquireTimerDatabaseLock();
	LoopOverList(Timer, &KiQueuedTimerList, TIMER, QueueEntry) {
	    if (Timer->DueTime.QuadPart < MaxDueTime) {
		/* TODO: For periodic timer, we should compute the new DueTime
		 * and reinsert it in KiSignalExpiredTimer */
		RemoveEntryList(&Timer->QueueEntry);
		InsertTailList(&KiExpiredTimerList, &Timer->ExpiredListEntry);
	    }
	}
	KiReleaseTimerDatabaseLock();
	if (!IsListEmpty(&KiExpiredTimerList)) {
	    /* Notify the main event loop to check the expired timer list */
	    seL4_NBSend(KiTimerServiceNotification.TreeNode.Cap,
			seL4_MessageInfo_new(0, 0, 0, 0));
	}
    }
}

static NTSTATUS KiEnableTimerInterruptService()
{
    RET_ERR(KiCreateIrqHandler(&KiTimerIrqHandler, TIMER_IRQ_LINE));
    RET_ERR_EX(KeCreateNotification(&KiTimerIrqNotification),
	       MmCapTreeDeleteNode(&KiTimerIrqHandler.TreeNode));
    RET_ERR_EX(KiConnectIrqNotification(&KiTimerIrqHandler, &KiTimerIrqNotification),
	       {
		   MmCapTreeDeleteNode(&KiTimerIrqNotification.TreeNode);
		   MmCapTreeDeleteNode(&KiTimerIrqHandler.TreeNode);
	       });
    RET_ERR_EX(PsCreateSystemThread(&KiTimerIrqThread, "NTOS Timer ISR",
				    KiTimerInterruptService),
	       {
		   MmCapTreeDeleteNode(&KiTimerIrqNotification.TreeNode);
		   MmCapTreeDeleteNode(&KiTimerIrqHandler.TreeNode);
	       });
    return STATUS_SUCCESS;
}

static inline VOID KiSignalExpiredTimer(IN PTIMER Timer)
{
    KiSignalDispatcherObject(&Timer->Header);
    if (Timer->ApcRoutine != NULL) {
	KeQueueApcToThread(Timer->ApcThread, (PKAPC_ROUTINE) Timer->ApcRoutine,
			   Timer->ApcContext, (PVOID)((ULONG_PTR)Timer->DueTime.LowPart),
			   (PVOID)((ULONG_PTR)Timer->DueTime.HighPart));
    }
}

VOID KiSignalExpiredTimerList()
{
    KiAcquireTimerDatabaseLock();
    LoopOverList(Timer, &KiExpiredTimerList, TIMER, ExpiredListEntry) {
	/* TODO: For periodic timer, we should compute the new DueTime
	 * and reinsert it into the timer queue */
	RemoveEntryList(&Timer->ExpiredListEntry);
	Timer->State = FALSE;
	KiSignalExpiredTimer(Timer);
    }
    KiReleaseTimerDatabaseLock();
}

NTSTATUS KiInitTimer()
{
    extern CNODE MiNtosCNode;
    InitializeListHead(&KiTimerList);
    InitializeListHead(&KiQueuedTimerList);
    InitializeListHead(&KiExpiredTimerList);
    KeCreateMutex(&KiTimerDatabaseLock);
    KeInitializeIpcEndpoint(&KiTimerServiceNotification, &MiNtosCNode, 0,
			    SERVICE_NOTIFICATION);
    RET_ERR(MmCapTreeDeriveBadgedNode(&KiTimerServiceNotification.TreeNode,
				      &KiExecutiveServiceEndpoint.TreeNode,
				      ENDPOINT_RIGHTS_WRITE_GRANTREPLY,
				      SERVICE_NOTIFICATION));

    TIME_FIELDS ClockTime;
    HalQueryRealTimeClock(&ClockTime);
    BOOLEAN RtcTimeOk = RtlTimeFieldsToTime(&ClockTime, &KiInitialSystemTime);
    RET_ERR(KiEnableTimerInterruptService());
    if (!RtcTimeOk || (ClockTime.Weekday < 0) || (ClockTime.Weekday > 6)) {
	HalVgaPrint("Corrupt CMOS clock: %d-%02d-%02d %02d:%02d:%02d\n\n",
		   ClockTime.Year, ClockTime.Month, ClockTime.Day, ClockTime.Hour,
		   ClockTime.Minute, ClockTime.Second);
    } else {
	HalVgaPrint("%d-%02d-%02d %s %02d:%02d:%02d UTC.\n\n",
		   ClockTime.Year, ClockTime.Month, ClockTime.Day,
		   KiWeekdayString[ClockTime.Weekday], ClockTime.Hour,
		   ClockTime.Minute, ClockTime.Second);
    }
    return STATUS_SUCCESS;
}

VOID KeInitializeTimer(IN PTIMER Timer,
		       IN TIMER_TYPE Type)
{
    KiInitializeDispatcherHeader(&Timer->Header, Type == NotificationTimer ?
				 NotificationEvent : SynchronizationEvent);
    InsertTailList(&KiTimerList, &Timer->ListEntry);
}

NTSTATUS KeCreateTimer(IN TIMER_TYPE TimerType,
		       OUT PTIMER *pTimer)
{
    PTIMER Timer = NULL;
    TIMER_OBJ_CREATE_CONTEXT CreaCtx = {
	.Type = TimerType
    };
    RET_ERR(ObCreateObject(OBJECT_TYPE_TIMER, (POBJECT *) &Timer, &CreaCtx));
    assert(Timer != NULL);
    *pTimer = Timer;
    return STATUS_SUCCESS;
}

BOOLEAN KeSetTimer(IN PTIMER Timer,
		   IN LARGE_INTEGER DueTime,
		   IN PTHREAD ApcThread,
		   IN PTIMER_APC_ROUTINE TimerApcRoutine,
		   IN PVOID TimerApcContext,
		   IN LONG Period)
{
    assert(Timer != NULL);
    ULONGLONG AbsoluteDueTime = DueTime.QuadPart;
    /* If DueTime is negative, it is relative to the current system time */
    if (DueTime.QuadPart < 0) {
	ULONGLONG TimerTicks = InterlockedIncrement64((PLONG64)&KiTimerTickCount);
	AbsoluteDueTime = -DueTime.QuadPart + KiTimerTickCountToSystemTime(TimerTicks);
    }
    KiAcquireTimerDatabaseLock();
    Timer->DueTime.QuadPart = AbsoluteDueTime;
    Timer->ApcThread = ApcThread;
    Timer->ApcRoutine = TimerApcRoutine;
    Timer->ApcContext = TimerApcContext;
    Timer->Period = Period;
    /* If the timer is already set, compute the new due time and return TRUE */
    if (Timer->State) {
	KiReleaseTimerDatabaseLock();
	return TRUE;
    }
    /* If the timer is not set, queue it and return FALSE. Note that if the timer
     * state is not set it is guaranteed to be in neither the timer queue or the
     * expired timer list. */
    InsertTailList(&KiQueuedTimerList, &Timer->QueueEntry);
    KiReleaseTimerDatabaseLock();
    return FALSE;
}

NTSTATUS NtCreateTimer(IN ASYNC_STATE State,
                       IN PTHREAD Thread,
                       OUT HANDLE *Handle,
                       IN ACCESS_MASK DesiredAccess,
                       IN OPTIONAL OB_OBJECT_ATTRIBUTES ObjectAttributes,
                       IN TIMER_TYPE TimerType)
{
    NTSTATUS Status = STATUS_NTOS_BUG;
    PTIMER Timer = NULL;

    ASYNC_BEGIN(State);

    RET_ERR(KeCreateTimer(TimerType, &Timer));
    assert(Timer != NULL);

    /* This isn't actually async since TIMER object does not have an open routine */
    AWAIT_EX(ObOpenObjectByPointer, Status, State, Thread, Timer, NULL, NULL, Handle);

    ASYNC_END(Status);
}

NTSTATUS NtSetTimer(IN ASYNC_STATE State,
                    IN PTHREAD Thread,
                    IN HANDLE TimerHandle,
                    IN PLARGE_INTEGER DueTime,
                    IN PTIMER_APC_ROUTINE TimerApcRoutine,
                    IN PVOID TimerContext,
                    IN BOOLEAN ResumeTimer,
                    IN LONG Period,
                    OUT OPTIONAL BOOLEAN *pPreviousState)
{
    assert(Thread != NULL);
    assert(Thread->Process != NULL);
    PTIMER Timer = NULL;
    RET_ERR(ObReferenceObjectByHandle(Thread->Process, TimerHandle, OBJECT_TYPE_TIMER, (POBJECT *)&Timer));
    assert(Timer != NULL);
    BOOLEAN PreviousState = KeSetTimer(Timer, *DueTime, Thread, TimerApcRoutine, TimerContext, Period);
    if (pPreviousState != NULL) {
	*pPreviousState = PreviousState;
    }
    return STATUS_SUCCESS;
}