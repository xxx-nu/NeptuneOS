@ stdcall NtDisplayString(ptr)
@ stdcall NtLoadDriver(ptr)
@ stdcall NtDisplayStringA(ptr)
@ stdcall NtLoadDriverA(ptr)
@ stdcall NtCreateFile(ptr long ptr ptr long long long ptr long long ptr)
@ stdcall NtOpenFile(ptr long ptr ptr long long)
@ stdcall NtReadFile(long long ptr ptr ptr ptr long ptr ptr)
@ stdcall NtWriteFile(long long ptr ptr ptr ptr long ptr ptr)
@ stdcall NtDeleteFile(ptr)
@ stdcall NtSetInformationFile(ptr ptr ptr long long)
@ stdcall NtTerminateThread(ptr long)
@ stdcall NtTerminateProcess(ptr long)
@ stdcall NtResumeThread(long long)
@ stdcall NtDeviceIoControlFile(long long long long long long long long long long)
@ stdcall NtCreateTimer(ptr long ptr long)
@ stdcall NtSetTimer(long ptr ptr ptr long long ptr)
@ stdcall NtWaitForSingleObject(long long long)
@ stdcall NtCreateEvent(long long long long long)
@ stdcall NtClose(long)
@ stdcall NtQuerySystemInformation(long long long long)
@ stdcall NtQueryDirectoryFile(long long ptr ptr ptr ptr long long long ptr long)
@ stdcall NtQueryInformationFile(ptr ptr ptr long long)
@ stdcall NtQueryAttributesFile(ptr ptr)
@ stdcall NtShutdownSystem(long)
@ stdcall NtSetDefaultLocale(long long)
@ stdcall NtDelayExecution(long ptr)
@ stdcall NtOpenKey(ptr long ptr)
@ stdcall NtCreateKey(ptr long ptr long ptr long long)
@ stdcall NtCreateKeyA(ptr long ptr long ptr long long)
@ stdcall NtDeleteKey(long)
@ stdcall NtQueryValueKey(long long long long long long)
@ stdcall NtSetValueKey(ptr ptr long long ptr long)
@ stdcall NtSetValueKeyA(ptr ptr long long ptr long)
@ stdcall NtDeleteValueKey(long ptr)
@ stdcall NtEnumerateKey (long long long long long long)
@ stdcall NtEnumerateValueKey(long long long long long long)
@ stdcall NtPlugPlayInitialize()
@ stdcall NtPlugPlayControl(ptr ptr long)
@ stdcall NtTestAlert()
@ stdcall LdrGetProcedureAddress(ptr ptr long ptr)
@ stdcall LdrFindEntryForAddress(ptr ptr)
@ varargs DbgPrint(str)
@ varargs DbgPrintEx(long long str)
@ stdcall RtlAssert(ptr ptr long ptr)
@ stdcall RtlCompareMemory(ptr ptr long)
@ stdcall RtlCompareMemoryUlong(ptr long long)
@ stdcall RtlFillMemory(ptr long long)
@ stdcall -arch=i386 RtlFillMemoryUlong(ptr long long)
@ stdcall RtlMoveMemory(ptr ptr long)
@ stdcall RtlZeroMemory(ptr long)
@ stdcall -version=0x600+ RtlTestBit(ptr long)
@ stdcall RtlGetNtGlobalFlags()
@ stdcall RtlSetLastWin32ErrorAndNtStatusFromNtStatus(long)
@ stdcall RtlNtStatusToDosErrorNoTeb(long)
@ stdcall RtlInitializeSListHead(ptr)
@ stdcall RtlPcToFileHeader(ptr ptr)
@ cdecl -arch=x86_64 RtlRestoreContext(ptr ptr)
@ stdcall RtlInitAnsiString(ptr str)
@ stdcall RtlInitAnsiStringEx(ptr str)
@ stdcall RtlAllocateHeap(ptr long ptr)
@ stdcall RtlFreeHeap(long long long)
@ stdcall RtlRaiseStatus(long)
@ stdcall RtlTimeToTimeFields(long long)
@ stdcall RtlFreeUnicodeString(ptr)
@ stdcall RtlCreateUnicodeStringFromAsciiz(ptr str)
@ stdcall RtlGetCurrentDirectory_U(long ptr)
@ stdcall RtlSetCurrentDirectory_U(ptr)
@ stdcall RtlSystemTimeToLocalTime(ptr ptr)
@ stdcall RtlDosPathNameToNtPathName_U(wstr ptr ptr ptr)
@ stdcall RtlInitUnicodeString(ptr wstr)
@ stdcall RtlInitUnicodeStringEx(ptr wstr)
@ stdcall RtlCreateUnicodeString(ptr wstr)
@ stdcall RtlCreateUnicodeStringFromAsciiz(ptr str)
@ stdcall RtlAppendUnicodeStringToString(ptr ptr)
@ stdcall RtlDuplicateUnicodeString(long ptr ptr)
@ stdcall RtlCopyUnicodeString(ptr ptr)
@ stdcall RtlAppendUnicodeToString(ptr wstr)
@ stdcall RtlUTF8ToUnicodeN(ptr long ptr ptr long)
@ stdcall RtlUnicodeToUTF8N(ptr long ptr ptr long)
@ stdcall RtlAnsiStringToUnicodeString(ptr ptr long)
@ stdcall RtlFreeAnsiString(long)
@ stdcall RtlAdjustPrivilege(long long long ptr)
@ stdcall RtlCreateProcessParameters(ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr)
@ stdcall RtlCreateUserProcess(ptr long ptr ptr ptr ptr long ptr ptr ptr)
@ stdcall RtlCreateHeap(long ptr long long ptr ptr)
@ stdcall RtlDestroyHeap(long)
@ stdcall RtlQueryRegistryValues(long ptr ptr ptr ptr)
@ stdcall RtlWriteRegistryValue(long ptr ptr long ptr long)
@ stdcall RtlDeleteRegistryValue(long ptr ptr)
@ stdcall RtlInterlockedPushEntrySList(ptr ptr)
@ stdcall RtlInterlockedPopEntrySList(ptr)
@ stdcall RtlInterlockedFlushSList(ptr)
@ fastcall -arch=i386 RtlInterlockedPushListSList(ptr ptr ptr long)
@ cdecl memcpy(ptr ptr long)
@ cdecl memmove(ptr ptr long)
@ cdecl memset(ptr long long)
@ cdecl strcmp()
@ cdecl strchr(str long)
@ cdecl strlen(str)
@ cdecl strnlen(str long)
@ cdecl strncpy(ptr str long)
@ cdecl strspn(str str)
@ cdecl strpbrk(str str)
@ cdecl wcslen(wstr)
@ cdecl wcscpy_s(wstr long wstr)
@ cdecl wcsncpy(ptr wstr long)
@ cdecl wcsncpy_s(wstr long wstr long)
@ cdecl wcscat_s(wstr long wstr)
@ cdecl wcsncat_s(wstr long wstr long)
@ cdecl _strnicmp(str str long)
@ cdecl _vsnprintf(ptr long str ptr) vsnprintf
@ varargs _snprintf(ptr long str) snprintf
@ varargs _snwprintf(ptr long wstr)
@ varargs swprintf(ptr wstr)
@ cdecl _assert(str str long)
@ cdecl -arch=x86_64,arm __chkstk()
@ extern -arch=i386 _chkstk
@ extern RtlpDbgTraceModuleName
@ extern KiUserExceptionDispatcher
