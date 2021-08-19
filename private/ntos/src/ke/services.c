#include "ki.h"

static IPC_ENDPOINT KiSystemServiceEndpoint;

static inline VOID KiInitializeIpcEndpoint(IN PIPC_ENDPOINT Self,
					   IN PCNODE CSpace,
					   IN MWORD Badge)
{
    assert(Self != NULL);
    assert(CSpace != NULL);
    MmInitializeCapTreeNode(&Self->TreeNode, CAP_TREE_NODE_ENDPOINT, 0,
			    CSpace, NULL);
    Self->Badge = Badge;
}

/*
 * Create the IPC endpoint for the system services. This IPC endpoint
 * is then badged via seL4_CNode_Mint.
 */
NTSTATUS KiCreateSystemServicesEndpoint()
{
    PUNTYPED Untyped = NULL;
    RET_ERR(MmRequestUntyped(seL4_EndpointBits, &Untyped));
    assert(Untyped != NULL);
    KiInitializeIpcEndpoint(&KiSystemServiceEndpoint, Untyped->TreeNode.CSpace, 0);
    RET_ERR(MmRetypeIntoObject(Untyped, seL4_EndpointObject, seL4_EndpointBits,
			       &KiSystemServiceEndpoint.TreeNode));
    assert(KiSystemServiceEndpoint.TreeNode.Cap != 0);
    return STATUS_SUCCESS;
}

NTSTATUS KeEnableSystemServices(IN PPROCESS Process,
				IN PTHREAD Thread)
{
    assert(Process != NULL);
    assert(Thread != NULL);
    KiAllocatePool(SystemServiceEndpoint, IPC_ENDPOINT);
    MWORD IPCBadge = OBJECT_TO_GLOBAL_HANDLE(Thread);
    KiInitializeIpcEndpoint(SystemServiceEndpoint, Process->CSpace, IPCBadge);
    RET_ERR(MmCapTreeDeriveBadgedNode(&SystemServiceEndpoint->TreeNode,
				      &KiSystemServiceEndpoint.TreeNode,
				      ENDPOINT_RIGHTS_WRITE_GRANTREPLY,
				      IPCBadge));
    Thread->SystemServiceEndpoint = SystemServiceEndpoint;
    assert(SystemServiceEndpoint->TreeNode.Cap == SYSSVC_IPC_CAP);
    return STATUS_SUCCESS;
}

VOID KiDispatchSystemServices()
{
    while (TRUE) {
	MWORD Badge = 0;
	seL4_MessageInfo_t Reply = seL4_Recv(KiSystemServiceEndpoint.TreeNode.Cap, &Badge);
	ULONG SvcNum = seL4_MessageInfo_get_label(Reply);
	ULONG SvcMsgLength = seL4_MessageInfo_get_length(Reply);
	ULONG NumUnwrappedCaps = seL4_MessageInfo_get_capsUnwrapped(Reply);
	ULONG NumExtraCaps = seL4_MessageInfo_get_extraCaps(Reply);
	DbgTrace("Got message label 0x%x length %d unwrapped caps %d extra caps %d badge 0x%zx\n",
		 SvcNum, SvcMsgLength, NumUnwrappedCaps, NumExtraCaps, Badge);
	/* Reject the system service call since the service message is invalid. */
	if (Badge == 0 || NumUnwrappedCaps != 0 || NumExtraCaps != 0
	    || SvcNum >= NUMBER_OF_SYSTEM_SERVICES) {
	    break;
	}
	PTHREAD Thread = GLOBAL_HANDLE_TO_OBJECT(Badge);
    }
}

PCSTR KiDbgErrorCodeToStr(IN int Error)
{
    switch (Error) {
    case seL4_InvalidArgument:
	return "Invalid argument";
    case seL4_InvalidCapability:
	return "Invalid capability";
    case seL4_IllegalOperation:
	return "Illegal operation";
    case seL4_RangeError:
	return "Range error";
    case seL4_AlignmentError:
	return "Alignment error";
    case seL4_FailedLookup:
	return "Failed lookup";
    case seL4_TruncatedMessage:
	return "Truncated Message";
    case seL4_DeleteFirst:
	return "Delete first";
    case seL4_RevokeFirst:
	return "Revoke first";
    case seL4_NotEnoughMemory:
	return "Not enough memory";
    }

    return "Unknown error";
}

VOID KiDbgDumpFailedLookupError()
{
    DbgPrint("    Lookup failed for a %s capability.\n", seL4_GetMR(0) ? "source" : "destination");
    DbgPrint("    Failure type is ");
    switch (seL4_GetMR(1)) {
    case seL4_InvalidRoot:
	DbgPrint("invalid root\n");
	break;
    case seL4_MissingCapability:
	DbgPrint("missing capability (%zd bits left)\n", seL4_GetMR(2));
	break;
    case seL4_DepthMismatch:
	DbgPrint("depth mismatch (%zd bits of CPTR remaining to be decoded,"
		 " %zd bits that the current CNode being traversed resolved)\n",
		 seL4_GetMR(2), seL4_GetMR(3));
	break;
    case seL4_GuardMismatch:
	DbgPrint("guard mismatch (%zd bits of CPTR remaining to be decoded,"
		 " CNode guard is 0x%zx, CNode guard size is %zd)\n",
		 seL4_GetMR(2), seL4_GetMR(3), seL4_GetMR(4));
	break;
    }
}

VOID KeDbgDumpIPCError(IN int Error)
{
    DbgPrint("IPC Error code %d (%s):\n", Error, KiDbgErrorCodeToStr(Error));

    switch (Error) {
    case seL4_InvalidArgument:
	DbgPrint("    Invalid argument number is %zd\n", seL4_GetMR(0));
	break;
    case seL4_InvalidCapability:
	DbgPrint("    Invalid capability is 0x%zx\n", seL4_GetMR(0));
	break;
    case seL4_IllegalOperation:
	break;
    case seL4_RangeError:
	DbgPrint("    Allowed range: [0x%zx, 0x%zx]\n", seL4_GetMR(0), seL4_GetMR(1));
	break;
    case seL4_AlignmentError:
	break;
    case seL4_FailedLookup:
	KiDbgDumpFailedLookupError();
	break;
    case seL4_TruncatedMessage:
	break;
    case seL4_DeleteFirst:
	break;
    case seL4_RevokeFirst:
	break;
    case seL4_NotEnoughMemory:
	DbgPrint("    Amount of memory available: 0x%zx bytes\n", seL4_GetMR(0));
	break;
    }
}
