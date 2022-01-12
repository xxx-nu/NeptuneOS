#pragma once

#include <ntos.h>

#define NTOS_CM_TAG	(EX_POOL_TAG('n','t','c','m'))

#define CmpAllocatePoolEx(Var, Type, Size, OnError)		\
    ExAllocatePoolEx(Var, Type, Size, NTOS_CM_TAG, OnError)

#define CmpAllocateObject(Var, Type)				\
    CmpAllocatePoolEx(Var, Type, sizeof(Type), {})

typedef enum _CM_NODE_TYPE {
    CM_NODE_KEY,
    CM_NODE_VALUE
} CM_NODE_TYPE;

/*
 * Hash table entry for a registry node (ie. key or value)
 *
 * This is the base object (in the OOP-sense) for CM_KEY_OBJECT and
 * CM_REG_VALUE.
 */
typedef struct _CM_NODE {
    PCSTR Name;
    LIST_ENTRY HashLink;
    struct _CM_NODE *Parent;
    CM_NODE_TYPE Type;
} CM_NODE, *PCM_NODE;

/* For now this is set as a fixed number. Eventually we will want to
 * dynamically expand the registry key hashes since some registry keys
 * have a very large number of values or sub-keys. */
#define CM_KEY_HASH_BUCKETS	37

/*
 * Registry Key object
 */
typedef struct _CM_KEY_OBJECT {
    CM_NODE Node;		/* Hash-table entry for the parent key */
    LIST_ENTRY HashBuckets[CM_KEY_HASH_BUCKETS];
    BOOLEAN Volatile;
} CM_KEY_OBJECT, *PCM_KEY_OBJECT;

/*
 * Registry value
 */
typedef struct _CM_REG_VALUE {
    CM_NODE Node;		/* Hash-table entry for the parent key */
    ULONG Type;			/* REG_NONE, REG_SZ, etc */
    union {
	PCSTR String;		/* UTF-8, NUL-terminated */
	ULONG Dword;
	ULONGLONG Qword;
	PCM_NODE RegLink;
	PCHAR Binary;
	PCSTR *StringArray;	/* NULL-terminated */
    };
} CM_REG_VALUE, *PCM_REG_VALUE;

/*
 * Creation context for key object
 */
typedef struct _KEY_OBJECT_CREATE_CONTEXT {
    BOOLEAN Volatile;
    PCSTR Name;
    ULONG NameLength;
    PCM_KEY_OBJECT Parent;
} KEY_OBJECT_CREATE_CONTEXT, *PKEY_OBJECT_CREATE_CONTEXT;

/* key.c */
NTSTATUS CmpKeyObjectCreateProc(IN POBJECT Object,
				IN PVOID CreaCtx);
NTSTATUS CmpKeyObjectParseProc(IN POBJECT Self,
			       IN PCSTR Path,
			       IN POB_PARSE_CONTEXT ParseContext,
			       OUT POBJECT *FoundObject,
			       OUT PCSTR *RemainingPath);
NTSTATUS CmpKeyObjectOpenProc(IN ASYNC_STATE State,
			      IN PTHREAD Thread,
			      IN POBJECT Object,
			      IN PCSTR SubPath,
			      IN POB_PARSE_CONTEXT ParseContext,
			      OUT POBJECT *pOpenedInstance,
			      OUT PCSTR *pRemainingPath);
