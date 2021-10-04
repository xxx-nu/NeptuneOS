#include "ldrp.h"

/* TLS ***********************************************************************/

/*
 * Executable always has _tls_index == 0. NTDLL always has _tls_index == 1
 * We will set this during LdrpInitialize
 */
#define SYSTEMDLL_TLS_INDEX	1
ULONG _tls_index;

#define MAX_NUMBER_OF_TLS_SLOTS	500

/* This must be placed at the very beginning ot the .tls section */
__declspec(allocate(".tls")) PVOID THREAD_LOCAL_STORAGE_POINTERS_ARRAY[MAX_NUMBER_OF_TLS_SLOTS];

/* Address for the IPC buffer of the initial thread. */
__thread seL4_IPCBuffer *__sel4_ipc_buffer;

/* GLOBALS *******************************************************************/

#define LDR_HASH_TABLE_ENTRIES 32

UNICODE_STRING NtDllString = RTL_CONSTANT_STRING(L"ntdll.dll");

BOOLEAN LdrpShutdownInProgress;
HANDLE LdrpShutdownThreadId;

PLDR_DATA_TABLE_ENTRY LdrpImageEntry;
PLDR_DATA_TABLE_ENTRY LdrpCurrentDllInitializer;
PLDR_DATA_TABLE_ENTRY LdrpNtDllDataTableEntry;

RTL_BITMAP TlsBitMap;
RTL_BITMAP TlsExpansionBitMap;
RTL_BITMAP FlsBitMap;
BOOLEAN LdrpImageHasTls;
LIST_ENTRY LdrpTlsList;
ULONG LdrpNumberOfTlsEntries;

extern LARGE_INTEGER RtlpTimeout;
PVOID LdrpHeap;

PEB_LDR_DATA PebLdr;

RTL_CRITICAL_SECTION_DEBUG LdrpLoaderLockDebug;
RTL_CRITICAL_SECTION LdrpLoaderLock = {
    &LdrpLoaderLockDebug,
    -1,
    0,
    0,
    0,
    0
};

RTL_CRITICAL_SECTION FastPebLock;

BOOLEAN ShowSnaps;

extern BOOLEAN RtlpUse16ByteSLists;

#ifdef _WIN64
#define DEFAULT_SECURITY_COOKIE 0x00002B992DDFA232ll
#else
#define DEFAULT_SECURITY_COOKIE 0xBB40E64E
#endif

/* FUNCTIONS *****************************************************************/

static NTSTATUS LdrpInitializeTls(VOID)
{
#if 0
    PLIST_ENTRY NextEntry, ListHead;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PIMAGE_TLS_DIRECTORY TlsDirectory;
    PLDRP_TLS_DATA TlsData;
    ULONG Size;

    /* Initialize the TLS List */
    InitializeListHead(&LdrpTlsList);

    /* Loop all the modules */
    ListHead = &NtCurrentPeb()->LdrData->InLoadOrderModuleList;
    NextEntry = ListHead->Flink;
    while (ListHead != NextEntry) {
	/* Get the entry */
	LdrEntry = CONTAINING_RECORD(NextEntry, LDR_DATA_TABLE_ENTRY,
				     InLoadOrderLinks);
	NextEntry = NextEntry->Flink;

	/* Get the TLS directory */
	TlsDirectory = RtlImageDirectoryEntryToData(LdrEntry->DllBase,
						    TRUE,
						    IMAGE_DIRECTORY_ENTRY_TLS,
						    &Size);

	/* Check if we have a directory */
	if (!TlsDirectory)
	    continue;

	/* Check if the image has TLS */
	if (!LdrpImageHasTls)
	    LdrpImageHasTls = TRUE;

	/* Show debug message */
	if (ShowSnaps) {
	    DPRINT1("LDR: Tls Found in %wZ at %p\n",
		    &LdrEntry->BaseDllName, TlsDirectory);
	}

	/* Allocate an entry */
	TlsData = RtlAllocateHeap(RtlGetProcessHeap(), 0, sizeof(LDRP_TLS_DATA));
	if (!TlsData)
	    return STATUS_NO_MEMORY;

	/* Lock the DLL and mark it for TLS Usage */
	LdrEntry->LoadCount = -1;
	LdrEntry->TlsIndex = -1;

	/* Save the cached TLS data */
	TlsData->TlsDirectory = *TlsDirectory;
	InsertTailList(&LdrpTlsList, &TlsData->TlsLinks);

	/* Update the index */
	*(PLONG) TlsData->TlsDirectory.AddressOfIndex = LdrpNumberOfTlsEntries;
	TlsData->TlsDirectory.Characteristics = LdrpNumberOfTlsEntries++;
    }

    /* Done setting up TLS, allocate entries */
    return LdrpAllocateTls();
#endif
    return STATUS_SUCCESS;
}

static PVOID LdrpFetchAddressOfSecurityCookie(IN PVOID BaseAddress,
					      IN ULONG SizeOfImage)
{
    PIMAGE_LOAD_CONFIG_DIRECTORY ConfigDir;
    ULONG DirSize;
    PVOID Cookie = NULL;

    /* Check NT header first */
    if (!RtlImageNtHeader(BaseAddress))
	return NULL;

    /* Get the pointer to the config directory */
    ConfigDir = RtlImageDirectoryEntryToData(BaseAddress,
					     TRUE,
					     IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG,
					     &DirSize);

    /* Check for sanity */
    if (!ConfigDir || (DirSize != 64 && ConfigDir->Size != DirSize) ||
	(ConfigDir->Size < 0x48))
	return NULL;

    /* Now get the cookie */
    Cookie = (PVOID) ConfigDir->SecurityCookie;

    /* Check this cookie */
    if ((PCHAR) Cookie <= (PCHAR) BaseAddress ||
	(PCHAR) Cookie >= (PCHAR) BaseAddress + SizeOfImage) {
	Cookie = NULL;
    }

    /* Return validated security cookie */
    return Cookie;
}

static PVOID LdrpInitSecurityCookie(PLDR_DATA_TABLE_ENTRY LdrEntry)
{
    PULONG_PTR Cookie;
    LARGE_INTEGER Counter;
    ULONG_PTR NewCookie;

    /* Fetch address of the cookie */
    Cookie = LdrpFetchAddressOfSecurityCookie(LdrEntry->DllBase,
					      LdrEntry->SizeOfImage);

    if (Cookie) {
	/* Check if it's a default one */
	if ((*Cookie == DEFAULT_SECURITY_COOKIE) || (*Cookie == 0xBB40)) {
	    /* Make up a cookie from a bunch of values which may uniquely represent
	       current moment of time, environment, etc */
	    NtQueryPerformanceCounter(&Counter, NULL);

	    NewCookie = Counter.LowPart ^ Counter.HighPart;
	    NewCookie ^= (ULONG_PTR) NtCurrentTeb()->ClientId.UniqueProcess;
	    NewCookie ^= (ULONG_PTR) NtCurrentTeb()->ClientId.UniqueThread;

	    /* Loop like it's done in KeQueryTickCount(). We don't want to call it directly. */
	    while (SharedUserData->SystemTime.High1Time != SharedUserData->SystemTime.High2Time) {
		YieldProcessor();
	    };

	    /* Calculate the milliseconds value and xor it to the cookie */
	    NewCookie ^= Int64ShrlMod32(UInt32x32To64(SharedUserData->TickCountMultiplier,
						      SharedUserData->TickCount.LowPart), 24)
		+ SharedUserData->TickCountMultiplier * (SharedUserData->TickCount.High1Time << 8);

	    /* Make the cookie 16bit if necessary */
	    if (*Cookie == 0xBB40)
		NewCookie &= 0xFFFF;

	    /* If the result is 0 or the same as we got, just subtract one from the existing value
	       and that's it */
	    if ((NewCookie == 0) || (NewCookie == *Cookie)) {
		NewCookie = *Cookie - 1;
	    }

	    /* Set the new cookie value */
	    *Cookie = NewCookie;
	}
    }

    return Cookie;
}

static NTSTATUS LdrpRunInitializeRoutines(IN PCONTEXT Context OPTIONAL)
{
#if 0
    PLDR_DATA_TABLE_ENTRY LocalArray[16];
    PLIST_ENTRY ListHead;
    PLIST_ENTRY NextEntry;
    PLDR_DATA_TABLE_ENTRY LdrEntry, *LdrRootEntry, OldInitializer;
    PVOID EntryPoint;
    ULONG Count, i;
    NTSTATUS Status = STATUS_SUCCESS;
    PPEB Peb = NtCurrentPeb();
    ULONG BreakOnDllLoad;
    BOOLEAN DllStatus;

    DPRINT("LdrpRunInitializeRoutines() called for %wZ (%p/%p)\n",
	   &LdrpImageEntry->BaseDllName,
	   NtCurrentTeb()->RealClientId.UniqueProcess,
	   NtCurrentTeb()->RealClientId.UniqueThread);

    /* Get the number of entries to call */
    if ((Count = LdrpClearLoadInProgress())) {
	/* Check if we can use our local buffer */
	if (Count > 16) {
	    /* Allocate space for all the entries */
	    LdrRootEntry = RtlAllocateHeap(LdrpHeap,
					   0,
					   Count * sizeof(*LdrRootEntry));
	    if (!LdrRootEntry)
		return STATUS_NO_MEMORY;
	} else {
	    /* Use our local array */
	    LdrRootEntry = LocalArray;
	}
    } else {
	/* Don't need one */
	LdrRootEntry = NULL;
    }

    /* Show debug message */
    if (ShowSnaps) {
	DPRINT1("[%p,%p] LDR: Real INIT LIST for Process %wZ\n",
		NtCurrentTeb()->RealClientId.UniqueThread,
		NtCurrentTeb()->RealClientId.UniqueProcess,
		&Peb->ProcessParameters->ImagePathName);
    }

    /* Loop in order */
    ListHead = &Peb->LdrData->InInitializationOrderModuleList;
    NextEntry = ListHead->Flink;
    i = 0;
    while (NextEntry != ListHead) {
	/* Get the Data Entry */
	LdrEntry = CONTAINING_RECORD(NextEntry, LDR_DATA_TABLE_ENTRY,
				     InInitializationOrderLinks);

	/* Check if we have a Root Entry */
	if (LdrRootEntry) {
	    /* Check flags */
	    if (!(LdrEntry->Flags & LDRP_ENTRY_PROCESSED)) {
		/* Setup the Cookie for the DLL */
		LdrpInitSecurityCookie(LdrEntry);

		/* Check for valid entrypoint */
		if (LdrEntry->EntryPoint) {
		    /* Write in array */
		    ASSERT(i < Count);
		    LdrRootEntry[i] = LdrEntry;

		    /* Display debug message */
		    if (ShowSnaps) {
			DPRINT1("[%p,%p] LDR: %wZ init routine %p\n",
				NtCurrentTeb()->RealClientId.UniqueThread,
				NtCurrentTeb()->RealClientId.UniqueProcess,
				&LdrEntry->FullDllName,
				LdrEntry->EntryPoint);
		    }
		    i++;
		}
	    }
	}

	/* Set the flag */
	LdrEntry->Flags |= LDRP_ENTRY_PROCESSED;
	NextEntry = NextEntry->Flink;
    }

    Status = STATUS_SUCCESS;

    /* No root entry? return */
    if (!LdrRootEntry)
	return Status;

    /* Loop */
    i = 0;
    while (i < Count) {
	/* Get an entry */
	LdrEntry = LdrRootEntry[i];

	/* FIXME: Verify NX Compat */

	/* Move to next entry */
	i++;

	/* Get its entrypoint */
	EntryPoint = LdrEntry->EntryPoint;

	/* Are we being debugged? */
	BreakOnDllLoad = 0;
	if (Peb->BeingDebugged || Peb->ReadImageFileExecOptions) {
	    /* Check if we should break on load */
	    Status = LdrQueryImageFileExecutionOptions(&LdrEntry->BaseDllName,
						       L"BreakOnDllLoad",
						       REG_DWORD,
						       &BreakOnDllLoad,
						       sizeof(ULONG), NULL);
	    if (!NT_SUCCESS(Status))
		BreakOnDllLoad = 0;

	    /* Reset status back to STATUS_SUCCESS */
	    Status = STATUS_SUCCESS;
	}

	/* Break if aksed */
	if (BreakOnDllLoad) {
	    /* Check if we should show a message */
	    if (ShowSnaps) {
		DPRINT1("LDR: %wZ loaded.", &LdrEntry->BaseDllName);
		DPRINT1(" - About to call init routine at %p\n",
			EntryPoint);
	    }

	    /* Break in debugger */
	    DbgBreakPoint();
	}

	/* Make sure we have an entrypoint */
	if (EntryPoint) {
	    /* Save the old Dll Initializer and write the current one */
	    OldInitializer = LdrpCurrentDllInitializer;
	    LdrpCurrentDllInitializer = LdrEntry;

	    _SEH2_TRY {
		/* Check if it has TLS */
		if (LdrEntry->TlsIndex && Context) {
		    /* Call TLS */
		    LdrpCallTlsInitializers(LdrEntry, DLL_PROCESS_ATTACH);
		}

		/* Call the Entrypoint */
		if (ShowSnaps) {
		    DPRINT1("%wZ - Calling entry point at %p for DLL_PROCESS_ATTACH\n",
			    &LdrEntry->BaseDllName, EntryPoint);
		}
		DllStatus = LdrpCallInitRoutine(EntryPoint,
						LdrEntry->DllBase,
						DLL_PROCESS_ATTACH,
						Context);
	    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
		DllStatus = FALSE;
		DPRINT1("WARNING: Exception 0x%x during LdrpCallInitRoutine(DLL_PROCESS_ATTACH) for %wZ\n",
			_SEH2_GetExceptionCode(), &LdrEntry->BaseDllName);
	    }

	    /* Save the Current DLL Initializer */
	    LdrpCurrentDllInitializer = OldInitializer;

	    /* Mark the entry as processed */
	    LdrEntry->Flags |= LDRP_PROCESS_ATTACH_CALLED;

	    /* Fail if DLL init failed */
	    if (!DllStatus) {
		DPRINT1("LDR: DLL_PROCESS_ATTACH for dll \"%wZ\" (InitRoutine: %p) failed\n",
			&LdrEntry->BaseDllName, EntryPoint);

		Status = STATUS_DLL_INIT_FAILED;
		goto Quickie;
	    }
	}
    }

    /* Loop in order */
    ListHead = &Peb->LdrData->InInitializationOrderModuleList;
    NextEntry = NextEntry->Flink;
    while (NextEntry != ListHead) {
	/* Get the Data Entry */
	LdrEntry = CONTAINING_RECORD(NextEntry, LDR_DATA_TABLE_ENTRY,
				     InInitializationOrderLinks);

	/* FIXME: Verify NX Compat */
	// LdrpCheckNXCompatibility()

	/* Next entry */
	NextEntry = NextEntry->Flink;
    }

    /* Check for TLS */
    if (LdrpImageHasTls && Context) {
	_SEH2_TRY {
	    /* Do TLS callbacks */
	    LdrpCallTlsInitializers(LdrpImageEntry, DLL_PROCESS_ATTACH);
	} _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
	    /* Do nothing */
	}
    }

Quickie:

    /* Check if the array is in the heap */
    if (LdrRootEntry != LocalArray) {
	/* Free the array */
	RtlFreeHeap(LdrpHeap, 0, LdrRootEntry);
    }

    /* Return to caller */
    DPRINT("LdrpRunInitializeRoutines() done\n");
    return Status;
#endif
    return STATUS_SUCCESS;
}

static NTSTATUS LdrpInitializeProcess(PNTDLL_PROCESS_INIT_INFO InitInfo)
{
    /* Set a NULL SEH Filter */
    RtlSetUnhandledExceptionFilter(NULL);

    /* Set the critical section timeout from PEB */
    PPEB Peb = NtCurrentPeb();
    RtlpTimeout = Peb->CriticalSectionTimeout;

    /* Get the Image Config Directory */
    ULONG ConfigSize;
    PIMAGE_LOAD_CONFIG_DIRECTORY LoadConfig = RtlImageDirectoryEntryToData(Peb->ImageBaseAddress, TRUE,
									   IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG, &ConfigSize);

    /* Setup the Process Heap Parameters */
    RTL_HEAP_PARAMETERS ProcessHeapParams;    
    RtlZeroMemory(&ProcessHeapParams, sizeof(RTL_HEAP_PARAMETERS));
    ULONG ProcessHeapFlags = HEAP_GROWABLE;
    ProcessHeapParams.Length = sizeof(RTL_HEAP_PARAMETERS);

    /* Check if we have Configuration Data */
#define VALID_CONFIG_FIELD(Name) (ConfigSize >= (FIELD_OFFSET(IMAGE_LOAD_CONFIG_DIRECTORY, Name) + sizeof(LoadConfig->Name)))
    /* The 'original' load config ends after SecurityCookie */
    if ((LoadConfig) && ConfigSize && (VALID_CONFIG_FIELD(SecurityCookie) || ConfigSize == LoadConfig->Size)) {
	if (ConfigSize != sizeof(IMAGE_LOAD_CONFIG_DIRECTORY))
	    DPRINT1("WARN: Accepting different LOAD_CONFIG size!\n");
	else
	    DPRINT1("Applying LOAD_CONFIG\n");

	if (VALID_CONFIG_FIELD(GlobalFlagsSet) && LoadConfig->GlobalFlagsSet)
	    Peb->NtGlobalFlag |= LoadConfig->GlobalFlagsSet;

	if (VALID_CONFIG_FIELD(GlobalFlagsClear) && LoadConfig->GlobalFlagsClear)
	    Peb->NtGlobalFlag &= ~LoadConfig->GlobalFlagsClear;

	if (VALID_CONFIG_FIELD(CriticalSectionDefaultTimeout) && LoadConfig->CriticalSectionDefaultTimeout)
	    RtlpTimeout.QuadPart = Int32x32To64(LoadConfig->CriticalSectionDefaultTimeout, -10000000);

	if (VALID_CONFIG_FIELD(DeCommitFreeBlockThreshold) && LoadConfig->DeCommitFreeBlockThreshold)
	    ProcessHeapParams.DeCommitFreeBlockThreshold = LoadConfig->DeCommitFreeBlockThreshold;

	if (VALID_CONFIG_FIELD(DeCommitTotalFreeThreshold) && LoadConfig->DeCommitTotalFreeThreshold)
	    ProcessHeapParams.DeCommitTotalFreeThreshold = LoadConfig->DeCommitTotalFreeThreshold;

	if (VALID_CONFIG_FIELD(MaximumAllocationSize) && LoadConfig->MaximumAllocationSize)
	    ProcessHeapParams.MaximumAllocationSize = LoadConfig->MaximumAllocationSize;

	if (VALID_CONFIG_FIELD(VirtualMemoryThreshold) && LoadConfig->VirtualMemoryThreshold)
	    ProcessHeapParams.VirtualMemoryThreshold = LoadConfig->VirtualMemoryThreshold;

	if (VALID_CONFIG_FIELD(ProcessHeapFlags) && LoadConfig->ProcessHeapFlags)
	    ProcessHeapFlags = LoadConfig->ProcessHeapFlags;
    }

    /* Initialize Critical Section Data */
    LdrpInitCriticalSection(InitInfo->CriticalSectionLockSemaphore);

    /* Initialize VEH Call lists */
    RtlpInitializeVectoredExceptionHandling(InitInfo);

    /* Set TLS/FLS Bitmap data */
    Peb->FlsBitmap = &FlsBitMap;
    Peb->TlsBitmap = &TlsBitMap;
    Peb->TlsExpansionBitmap = &TlsExpansionBitMap;

    /* Initialize FLS Bitmap */
    RtlInitializeBitMap(&FlsBitMap, Peb->FlsBitmapBits, RTL_FLS_MAXIMUM_AVAILABLE);
    RtlSetBit(&FlsBitMap, 0);
    InitializeListHead(&Peb->FlsListHead);

    /* Initialize TLS Bitmap */
    RtlInitializeBitMap(&TlsBitMap, Peb->TlsBitmapBits, TLS_MINIMUM_AVAILABLE);
    RtlSetBit(&TlsBitMap, 0);
    RtlInitializeBitMap(&TlsExpansionBitMap, Peb->TlsExpansionBitmapBits, TLS_EXPANSION_SLOTS);
    RtlSetBit(&TlsExpansionBitMap, 0);

    /* Initialize the Loader Lock. */
    RtlpInitializeCriticalSection(&LdrpLoaderLock, InitInfo->LoaderLockSemaphore, 0);
    Peb->LoaderLock = &LdrpLoaderLock;

    /* Check if User Stack Trace Database support was requested */
    if (Peb->NtGlobalFlag & FLG_USER_STACK_TRACE_DB) {
	DPRINT1("We don't support user stack trace databases yet\n");
    }

    /* Setup the Fast PEB Lock. */
    RtlpInitializeCriticalSection(&FastPebLock, InitInfo->FastPebLockSemaphore, 0);
    Peb->FastPebLock = &FastPebLock;

    /* For old executables, use 16-byte aligned heap */
    PIMAGE_NT_HEADERS NtHeader = RtlImageNtHeader(Peb->ImageBaseAddress);
    if ((NtHeader->OptionalHeader.MajorSubsystemVersion <= 3) &&
	(NtHeader->OptionalHeader.MinorSubsystemVersion < 51)) {
	ProcessHeapFlags |= HEAP_CREATE_ALIGN_16;
    }

    /* Setup the process heap and the loader heap */
    RET_ERR(LdrpInitializeHeapManager(InitInfo, ProcessHeapFlags, &ProcessHeapParams));
    LdrpHeap = (HANDLE) InitInfo->LoaderHeapStart;

#if 0
    ULONG ComSectionSize;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE SymLinkHandle;
    UNICODE_STRING CommandLine, NtSystemRoot, ImagePathName, FullPath, ImageFileName, KnownDllString;
    PPEB Peb = NtCurrentPeb();
    BOOLEAN FreeCurDir = FALSE;
    PRTL_USER_PROCESS_PARAMETERS ProcessParameters;
    UNICODE_STRING CurrentDirectory;
    HANDLE OptionsKey;
    PWSTR NtDllName = NULL;
    NTSTATUS Status, ImportStatus;
    NLSTABLEINFO NlsTable;
    PTEB Teb = NtCurrentTeb();
    PLIST_ENTRY ListHead;
    PLIST_ENTRY NextEntry;
    ULONG i;
    PWSTR ImagePath;
    ULONG DebugProcessHeapOnly = 0;
    PLDR_DATA_TABLE_ENTRY NtLdrEntry;
    PWCHAR Current;
    ULONG ExecuteOptions = 0;
    PVOID ViewBase;

    /* Get the image path */
    ImagePath = Peb->ProcessParameters->ImagePathName.Buffer;

    /* Check if it's not normalized */
    if (!(Peb->ProcessParameters->Flags & RTL_USER_PROCESS_PARAMETERS_NORMALIZED)) {
	/* Normalize it */
	ImagePath = (PWSTR) ((ULONG_PTR) ImagePath + (ULONG_PTR) Peb->ProcessParameters);
    }

    /* Create a unicode string for the Image Path */
    ImagePathName.Length = Peb->ProcessParameters->ImagePathName.Length;
    ImagePathName.MaximumLength = ImagePathName.Length + sizeof(WCHAR);
    ImagePathName.Buffer = ImagePath;

    /* Check if this is a .NET executable */
    if (RtlImageDirectoryEntryToData(Peb->ImageBaseAddress, TRUE,
				     IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR,
				     &ComSectionSize)) {
	DPRINT1("We don't support .NET applications yet\n");
	return STATUS_NOT_IMPLEMENTED;
    }

    /* Normalize the parameters */
    ProcessParameters = RtlNormalizeProcessParams(Peb->ProcessParameters);
    if (ProcessParameters) {
	/* Save the Image and Command Line Names */
	ImageFileName = ProcessParameters->ImagePathName;
	CommandLine = ProcessParameters->CommandLine;
    } else {
	/* It failed, initialize empty strings */
	RtlInitUnicodeString(&ImageFileName, NULL);
	RtlInitUnicodeString(&CommandLine, NULL);
    }

    /* Initialize NLS data */
    RtlInitNlsTables(Peb->AnsiCodePageData,
		     Peb->OemCodePageData,
		     Peb->UnicodeCaseTableData, &NlsTable);

    /* Reset NLS Translations */
    RtlResetRtlTranslations(&NlsTable);



    /* Build the NTDLL Path */
    FullPath.Buffer = StringBuffer;
    FullPath.Length = 0;
    FullPath.MaximumLength = sizeof(StringBuffer);
    RtlInitUnicodeString(&NtSystemRoot, SharedUserData->NtSystemRoot);
    RtlAppendUnicodeStringToString(&FullPath, &NtSystemRoot);
    RtlAppendUnicodeToString(&FullPath, L"\\System32\\");

    /* Setup Loader Data */
    Peb->LdrData = &PebLdr;
    InitializeListHead(&PebLdr.InLoadOrderModuleList);
    InitializeListHead(&PebLdr.InMemoryOrderModuleList);
    InitializeListHead(&PebLdr.InInitializationOrderModuleList);
    PebLdr.Length = sizeof(PEB_LDR_DATA);
    PebLdr.Initialized = TRUE;

    /* Allocate a data entry for the Image */
    LdrpImageEntry = LdrpAllocateDataTableEntry(Peb->ImageBaseAddress);

    /* Set it up */
    LdrpImageEntry->EntryPoint = LdrpFetchAddressOfEntryPoint(LdrpImageEntry->DllBase);
    LdrpImageEntry->LoadCount = -1;
    LdrpImageEntry->EntryPointActivationContext = 0;
    LdrpImageEntry->FullDllName = ImageFileName;

    /* Check if the name is empty */
    if (!ImageFileName.Buffer[0]) {
	/* Use the same Base name */
	LdrpImageEntry->BaseDllName = LdrpImageEntry->FullDllName;
    } else {
	/* Find the last slash */
	Current = ImageFileName.Buffer;
	while (*Current) {
	    if (*Current++ == '\\') {
		/* Set this path */
		NtDllName = Current;
	    }
	}

	/* Did we find anything? */
	if (!NtDllName) {
	    /* Use the same Base name */
	    LdrpImageEntry->BaseDllName = LdrpImageEntry->FullDllName;
	} else {
	    /* Setup the name */
	    LdrpImageEntry->BaseDllName.Length =
		(USHORT) ((ULONG_PTR) ImageFileName.Buffer +
			  ImageFileName.Length - (ULONG_PTR) NtDllName);
	    LdrpImageEntry->BaseDllName.MaximumLength =
		LdrpImageEntry->BaseDllName.Length + sizeof(WCHAR);
	    LdrpImageEntry->BaseDllName.Buffer =
		(PWSTR) ((ULONG_PTR) ImageFileName.Buffer +
			 (ImageFileName.Length -
			  LdrpImageEntry->BaseDllName.Length));
	}
    }

    /* Processing done, insert it */
    LdrpInsertMemoryTableEntry(LdrpImageEntry);
    LdrpImageEntry->Flags |= LDRP_ENTRY_PROCESSED;

    /* Now add an entry for NTDLL */
    NtLdrEntry = LdrpAllocateDataTableEntry(SystemArgument1);
    NtLdrEntry->Flags = LDRP_IMAGE_DLL;
    NtLdrEntry->EntryPoint = LdrpFetchAddressOfEntryPoint(NtLdrEntry->DllBase);
    NtLdrEntry->LoadCount = -1;
    NtLdrEntry->EntryPointActivationContext = 0;

    NtLdrEntry->FullDllName.Length = FullPath.Length;
    NtLdrEntry->FullDllName.MaximumLength = FullPath.MaximumLength;
    NtLdrEntry->FullDllName.Buffer = StringBuffer;
    RtlAppendUnicodeStringToString(&NtLdrEntry->FullDllName, &NtDllString);

    NtLdrEntry->BaseDllName.Length = NtDllString.Length;
    NtLdrEntry->BaseDllName.MaximumLength = NtDllString.MaximumLength;
    NtLdrEntry->BaseDllName.Buffer = NtDllString.Buffer;

    /* Processing done, insert it */
    LdrpNtDllDataTableEntry = NtLdrEntry;
    LdrpInsertMemoryTableEntry(NtLdrEntry);

    /* Let the world know */
    if (ShowSnaps) {
	DPRINT1("LDR: NEW PROCESS\n");
	DPRINT1("     Image Path: %wZ (%wZ)\n",
		&LdrpImageEntry->FullDllName,
		&LdrpImageEntry->BaseDllName);
	DPRINT1("     Current Directory: %wZ\n", &CurrentDirectory);
	DPRINT1("     Search Path: %wZ\n", &LdrpDefaultPath);
    }

    /* Link the Init Order List */
    InsertHeadList(&Peb->LdrData->InInitializationOrderModuleList,
		   &LdrpNtDllDataTableEntry->InInitializationOrderLinks);

    /* Set the current directory */
    Status = RtlSetCurrentDirectory_U(&CurrentDirectory);
    if (!NT_SUCCESS(Status)) {
	/* We failed, check if we should free it */
	if (FreeCurDir)
	    RtlFreeUnicodeString(&CurrentDirectory);

	/* Set it to the NT Root */
	CurrentDirectory = NtSystemRoot;
	RtlSetCurrentDirectory_U(&CurrentDirectory);
    } else {
	/* We're done with it, free it */
	if (FreeCurDir)
	    RtlFreeUnicodeString(&CurrentDirectory);
    }

    /* Walk the IAT and load all the DLLs */
    ImportStatus = LdrpWalkImportDescriptor(LdrpDefaultPath.Buffer, LdrpImageEntry);

    /* Check if relocation is needed */
    if (Peb->ImageBaseAddress != (PVOID) NtHeader->OptionalHeader.ImageBase) {
	DPRINT1("LDR: Performing EXE relocation\n");

	/* Change the protection to prepare for relocation */
	ViewBase = Peb->ImageBaseAddress;
	Status = LdrpSetProtection(ViewBase, FALSE);
	if (!NT_SUCCESS(Status))
	    return Status;

	/* Do the relocation */
	Status = LdrRelocateImageWithBias(ViewBase,
					  0LL,
					  NULL,
					  STATUS_SUCCESS,
					  STATUS_CONFLICTING_ADDRESSES,
					  STATUS_INVALID_IMAGE_FORMAT);
	if (!NT_SUCCESS(Status)) {
	    DPRINT1("LdrRelocateImageWithBias() failed\n");
	    return Status;
	}

	/* Check if a start context was provided */
	if (Context) {
	    DPRINT1("WARNING: Relocated EXE Context");
	    UNIMPLEMENTED;	// We should support this
	    return STATUS_INVALID_IMAGE_FORMAT;
	}

	/* Restore the protection */
	Status = LdrpSetProtection(ViewBase, TRUE);
	if (!NT_SUCCESS(Status))
	    return Status;
    }

    /* Lock the DLLs */
    ListHead = &Peb->LdrData->InLoadOrderModuleList;
    NextEntry = ListHead->Flink;
    while (ListHead != NextEntry) {
	NtLdrEntry = CONTAINING_RECORD(NextEntry, LDR_DATA_TABLE_ENTRY,
				       InLoadOrderLinks);
	NtLdrEntry->LoadCount = -1;
	NextEntry = NextEntry->Flink;
    }

    /* Check whether all static imports were properly loaded and return here */
    if (!NT_SUCCESS(ImportStatus))
	return ImportStatus;

    /* Initialize TLS */
    Status = LdrpInitializeTls();
    if (!NT_SUCCESS(Status)) {
	DPRINT1("LDR: LdrpProcessInitialization failed to initialize TLS slots; status %x\n",
		Status);
	return Status;
    }

    /* FIXME Mark the DLL Ranges for Stack Traces later */

    /* Notify the debugger now */
    if (Peb->BeingDebugged) {
	/* Break */
	DbgBreakPoint();

	/* Update show snaps again */
	ShowSnaps = Peb->NtGlobalFlag & FLG_SHOW_LDR_SNAPS;
    }

    /* Check NX Options */
    if (SharedUserData->NXSupportPolicy == 1) {
	ExecuteOptions = 0xD;
    } else if (!SharedUserData->NXSupportPolicy) {
	ExecuteOptions = 0xA;
    }

    /* Let Mm know */
    NtSetInformationProcess(NtCurrentProcess(),
			    ProcessExecuteFlags,
			    &ExecuteOptions, sizeof(ULONG));

    /* Now call the Init Routines */
    Status = LdrpRunInitializeRoutines(Context);
    if (!NT_SUCCESS(Status)) {
	DPRINT1("LDR: LdrpProcessInitialization failed running initialization routines; status %x\n",
		Status);
	return Status;
    }

    /* Check if we have a user-defined Post Process Routine */
    if (NT_SUCCESS(Status) && Peb->PostProcessInitRoutine) {
	/* Call it */
	Peb->PostProcessInitRoutine();
    }

    /* Return status */
    return Status;
#endif
    return STATUS_SUCCESS;
}

static VOID LdrpInitializeThread()
{
#if 0
    PPEB Peb = NtCurrentPeb();
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PLIST_ENTRY NextEntry, ListHead;
    NTSTATUS Status;
    PVOID EntryPoint;

    DPRINT("LdrpInitializeThread() called for %wZ (%p/%p)\n",
	   &LdrpImageEntry->BaseDllName,
	   NtCurrentTeb()->RealClientId.UniqueProcess,
	   NtCurrentTeb()->RealClientId.UniqueThread);

    /* Make sure we are not shutting down */
    if (LdrpShutdownInProgress)
	return;

    /* Start at the beginning */
    ListHead = &Peb->LdrData->InMemoryOrderModuleList;
    NextEntry = ListHead->Flink;
    while (NextEntry != ListHead) {
	/* Get the current entry */
	LdrEntry = CONTAINING_RECORD(NextEntry, LDR_DATA_TABLE_ENTRY,
				     InMemoryOrderLinks);

	/* Make sure it's not ourselves */
	if (Peb->ImageBaseAddress != LdrEntry->DllBase) {
	    /* Check if we should call */
	    if (!(LdrEntry->Flags & LDRP_DONT_CALL_FOR_THREADS)) {
		/* Get the entrypoint */
		EntryPoint = LdrEntry->EntryPoint;

		/* Check if we are ready to call it */
		if ((EntryPoint) && (LdrEntry->Flags & LDRP_PROCESS_ATTACH_CALLED) &&
		    (LdrEntry->Flags & LDRP_IMAGE_DLL)) {
		    _SEH2_TRY {
			/* Check if it has TLS */
			if (LdrEntry->TlsIndex) {
			    /* Make sure we're not shutting down */
			    if (!LdrpShutdownInProgress) {
				/* Call TLS */
				LdrpCallTlsInitializers(LdrEntry,
							DLL_THREAD_ATTACH);
			    }
			}

			/* Make sure we're not shutting down */
			if (!LdrpShutdownInProgress) {
			    /* Call the Entrypoint */
			    DPRINT("%wZ - Calling entry point at %p for thread attaching, %p/%p\n",
				   &LdrEntry->BaseDllName,
				   LdrEntry->EntryPoint,
				   NtCurrentTeb()->RealClientId.
				   UniqueProcess,
				   NtCurrentTeb()->RealClientId.
				   UniqueThread);
			    LdrpCallInitRoutine(LdrEntry->EntryPoint,
						LdrEntry->DllBase,
						DLL_THREAD_ATTACH, NULL);
			}
		    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
			DPRINT1("WARNING: Exception 0x%x during LdrpCallInitRoutine(DLL_THREAD_ATTACH) for %wZ\n",
				_SEH2_GetExceptionCode(),
				&LdrEntry->BaseDllName);
		    }
		}
	    }
	}

	/* Next entry */
	NextEntry = NextEntry->Flink;
    }

    /* Check for TLS */
    if (LdrpImageHasTls && !LdrpShutdownInProgress) {
	_SEH2_TRY {
	    /* Do TLS callbacks */
	    LdrpCallTlsInitializers(LdrpImageEntry, DLL_THREAD_ATTACH);
	} _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
	    /* Do nothing */
	}
    }
#endif
    DPRINT("LdrpInitializeThread() done\n");
}

/*
 * On process startup, NTDLL_PROCESS_INIT_INFO is placed at the beginning
 * of IpcBuffer.
 */
FASTCALL VOID LdrpInitialize(IN seL4_IPCBuffer *IpcBuffer,
			     IN PPVOID SystemDllTlsRegion,
			     IN PTHREAD_START_ROUTINE StartAddress)
{
    _tls_index = SYSTEMDLL_TLS_INDEX;
    SystemDllTlsRegion[SYSTEMDLL_TLS_INDEX] = SystemDllTlsRegion;
    __sel4_ipc_buffer = IpcBuffer;

    PTEB Teb = NtCurrentTeb();
    NTSTATUS Status, LoaderStatus = STATUS_SUCCESS;
    PPEB Peb = NtCurrentPeb();

    DPRINT("LdrpInitialize() %p/%p\n",
	   NtCurrentTeb()->RealClientId.UniqueProcess,
	   NtCurrentTeb()->RealClientId.UniqueThread);

#ifdef _WIN64
    /* Set the SList header usage */
    RtlpUse16ByteSLists = SharedUserData->ProcessorFeatures[PF_COMPARE_EXCHANGE128];
#endif	/* _WIN64 */

    /* Check if we have already setup LDR data */
    if (!Peb->LdrData) {
	/* At process startup the process init info is placed at the beginning
	 * of the ipc buffer. */
	NTDLL_PROCESS_INIT_INFO InitInfo = *((PNTDLL_PROCESS_INIT_INFO)(IpcBuffer));
	/* Now that the init info has been copied to the stack, clear the original. */
	memset(IpcBuffer, 0, sizeof(NTDLL_PROCESS_INIT_INFO));
	/* Initialize the Process */
	LoaderStatus = LdrpInitializeProcess(&InitInfo);
    } else {
	/* Loader data is there... is this a fork() ? */
	if (Peb->InheritedAddressSpace) {
	    /* Handle the fork() */
	    //LoaderStatus = LdrpForkProcess();
	    LoaderStatus = STATUS_NOT_IMPLEMENTED;
	    UNIMPLEMENTED;
	} else {
	    /* This is a new thread initializing */
	    LdrpInitializeThread();
	}
    }

    /* Bail out if initialization has failed */
    if (!NT_SUCCESS(LoaderStatus)) {
	HARDERROR_RESPONSE Response;

	/* Print a debug message */
	DPRINT1("LDR: Process initialization failure for %wZ; NTSTATUS = %08x\n",
		&Peb->ProcessParameters->ImagePathName, LoaderStatus);

	/* Send HARDERROR_MSG LPC message to CSRSS */
	NtRaiseHardError(STATUS_APP_INIT_FAILURE, 1, 0,
			 (PULONG_PTR) &LoaderStatus, OptionOk, &Response);

	/* Raise a status to terminate the thread. */
	RtlRaiseStatus(LoaderStatus);
    }

    /* Calls the entry point */
    StartAddress(Peb);

    /* The thread entry point should never return. Shutdown the thread if it does. */
    RtlExitUserThread(STATUS_UNSUCCESSFUL);
}

/*
 * @implemented
 */
NTAPI NTSTATUS LdrShutdownProcess(VOID)
{
#if 0
    PPEB Peb = NtCurrentPeb();
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PLIST_ENTRY NextEntry, ListHead;
    PVOID EntryPoint;

    DPRINT("LdrShutdownProcess() called for %wZ\n", &LdrpImageEntry->BaseDllName);
    if (LdrpShutdownInProgress)
	return STATUS_SUCCESS;

    /* Tell the world */
    if (ShowSnaps) {
	DPRINT1("\n");
    }

    /* Set the shutdown variables */
    LdrpShutdownThreadId = NtCurrentTeb()->RealClientId.UniqueThread;
    LdrpShutdownInProgress = TRUE;

    /* Enter the Loader Lock */
    RtlEnterCriticalSection(&LdrpLoaderLock);

    /* Cleanup trace logging data (Etw) */
    if (SharedUserData->TraceLogging) {
	/* FIXME */
	DPRINT1("We don't support Etw yet.\n");
    }

    /* Start at the end */
    ListHead = &Peb->LdrData->InInitializationOrderModuleList;
    NextEntry = ListHead->Blink;
    while (NextEntry != ListHead) {
	/* Get the current entry */
	LdrEntry = CONTAINING_RECORD(NextEntry, LDR_DATA_TABLE_ENTRY,
				     InInitializationOrderLinks);
	NextEntry = NextEntry->Blink;

	/* Make sure it's not ourselves */
	if (Peb->ImageBaseAddress != LdrEntry->DllBase) {
	    /* Get the entrypoint */
	    EntryPoint = LdrEntry->EntryPoint;

	    /* Check if we are ready to call it */
	    if (EntryPoint && (LdrEntry->Flags & LDRP_PROCESS_ATTACH_CALLED) && LdrEntry->Flags) {
		_SEH2_TRY {
		    /* Check if it has TLS */
		    if (LdrEntry->TlsIndex) {
			/* Call TLS */
			LdrpCallTlsInitializers(LdrEntry,
						DLL_PROCESS_DETACH);
		    }

		    /* Call the Entrypoint */
		    DPRINT("%wZ - Calling entry point at %p for thread detaching\n",
			   &LdrEntry->BaseDllName, LdrEntry->EntryPoint);
		    LdrpCallInitRoutine(EntryPoint, LdrEntry->DllBase,
					DLL_PROCESS_DETACH, (PVOID) 1);
		} _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
		    DPRINT1("WARNING: Exception 0x%x during LdrpCallInitRoutine(DLL_PROCESS_DETACH) for %wZ\n",
			    _SEH2_GetExceptionCode(), &LdrEntry->BaseDllName);
		}
	    }
	}
    }

    /* Check for TLS */
    if (LdrpImageHasTls) {
	_SEH2_TRY {
	    /* Do TLS callbacks */
	    LdrpCallTlsInitializers(LdrpImageEntry, DLL_PROCESS_DETACH);
	} _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
	    /* Do nothing */
	}
    }

    /* FIXME: Do Heap detection and Etw final shutdown */

    /* Release the lock */
    RtlLeaveCriticalSection(&LdrpLoaderLock);
    DPRINT("LdrpShutdownProcess() done\n");
#endif
    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
NTAPI NTSTATUS LdrShutdownThread(VOID)
{
#if 0
    PPEB Peb = NtCurrentPeb();
    PTEB Teb = NtCurrentTeb();
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PLIST_ENTRY NextEntry, ListHead;
    PVOID EntryPoint;

    DPRINT("LdrShutdownThread() called for %wZ\n", &LdrpImageEntry->BaseDllName);

    /* Cleanup trace logging data (Etw) */
    if (SharedUserData->TraceLogging) {
	/* FIXME */
	DPRINT1("We don't support Etw yet.\n");
    }

    /* Get the Ldr Lock */
    RtlEnterCriticalSection(&LdrpLoaderLock);

    /* Start at the end */
    ListHead = &Peb->LdrData->InInitializationOrderModuleList;
    NextEntry = ListHead->Blink;
    while (NextEntry != ListHead) {
	/* Get the current entry */
	LdrEntry = CONTAINING_RECORD(NextEntry, LDR_DATA_TABLE_ENTRY,
				     InInitializationOrderLinks);
	NextEntry = NextEntry->Blink;

	/* Make sure it's not ourselves */
	if (Peb->ImageBaseAddress != LdrEntry->DllBase) {
	    /* Check if we should call */
	    if (!(LdrEntry->Flags & LDRP_DONT_CALL_FOR_THREADS) &&
		(LdrEntry->Flags & LDRP_PROCESS_ATTACH_CALLED) &&
		(LdrEntry->Flags & LDRP_IMAGE_DLL)) {
		/* Get the entrypoint */
		EntryPoint = LdrEntry->EntryPoint;

		/* Check if we are ready to call it */
		if (EntryPoint) {
		    _SEH2_TRY {
			/* Check if it has TLS */
			if (LdrEntry->TlsIndex) {
			    /* Make sure we're not shutting down */
			    if (!LdrpShutdownInProgress) {
				/* Call TLS */
				LdrpCallTlsInitializers(LdrEntry,
							DLL_THREAD_DETACH);
			    }
			}

			/* Make sure we're not shutting down */
			if (!LdrpShutdownInProgress) {
			    /* Call the Entrypoint */
			    DPRINT("%wZ - Calling entry point at %p for thread detaching\n",
				   &LdrEntry->BaseDllName,
				   LdrEntry->EntryPoint);
			    LdrpCallInitRoutine(EntryPoint,
						LdrEntry->DllBase,
						DLL_THREAD_DETACH, NULL);
			}
		    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
			DPRINT1("WARNING: Exception 0x%x during LdrpCallInitRoutine(DLL_THREAD_DETACH) for %wZ\n",
				_SEH2_GetExceptionCode(),
				&LdrEntry->BaseDllName);
		    }
		}
	    }
	}
    }

    /* Check for TLS */
    if (LdrpImageHasTls) {
	_SEH2_TRY {
	    /* Do TLS callbacks */
	    LdrpCallTlsInitializers(LdrpImageEntry, DLL_THREAD_DETACH);
	} _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
	    /* Do nothing */
	}
    }

    /* Free TLS */
    LdrpFreeTls();
    RtlLeaveCriticalSection(&LdrpLoaderLock);

    /* Check for expansion slots */
    if (Teb->TlsExpansionSlots) {
	/* Free expansion slots */
	RtlFreeHeap(RtlGetProcessHeap(), 0, Teb->TlsExpansionSlots);
    }

    /* Check for FLS Data */
    if (Teb->FlsData) {
	/* Mimic BaseRundownFls */
	ULONG n, FlsHighIndex;
	PRTL_FLS_DATA pFlsData;
	PFLS_CALLBACK_FUNCTION lpCallback;

	pFlsData = Teb->FlsData;

	RtlAcquirePebLock();
	FlsHighIndex = NtCurrentPeb()->FlsHighIndex;
	RemoveEntryList(&pFlsData->ListEntry);
	RtlReleasePebLock();

	for (n = 1; n <= FlsHighIndex; ++n) {
	    lpCallback = NtCurrentPeb()->FlsCallback[n];
	    if (lpCallback && pFlsData->Data[n]) {
		lpCallback(pFlsData->Data[n]);
	    }
	}

	RtlFreeHeap(RtlGetProcessHeap(), 0, pFlsData);
	Teb->FlsData = NULL;
    }

    /* Check for Fiber data */
    if (Teb->HasFiberData) {
	/* Free Fiber data */
	RtlFreeHeap(RtlGetProcessHeap(), 0, Teb->NtTib.FiberData);
	Teb->NtTib.FiberData = NULL;
    }

    DPRINT("LdrShutdownThread() done\n");
#endif
    return STATUS_SUCCESS;
}
