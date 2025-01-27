#include "cmp.h"

static NTSTATUS CmpCreateKeyType()
{
    OBJECT_TYPE_INITIALIZER TypeInfo = {
	.CreateProc = CmpKeyObjectCreateProc,
	.ParseProc = CmpKeyObjectParseProc,
	.OpenProc = CmpKeyObjectOpenProc,
	.InsertProc = CmpKeyObjectInsertProc,
	.RemoveProc = CmpKeyObjectRemoveProc,
	.DeleteProc = CmpKeyObjectDeleteProc,
    };
    return ObCreateObjectType(OBJECT_TYPE_KEY,
			      "Key",
			      sizeof(CM_KEY_OBJECT),
			      TypeInfo);
}

NTSTATUS CmInitSystemPhase1()
{
    RET_ERR(CmpCreateKeyType());
    RET_ERR(ObCreateDirectory(REGISTRY_OBJECT_DIRECTORY));
    POBJECT RegistryDirectory = NULL;
    RET_ERR(ObReferenceObjectByName(REGISTRY_OBJECT_DIRECTORY,
				    OBJECT_TYPE_DIRECTORY, NULL,
				    FALSE, &RegistryDirectory));
    assert(RegistryDirectory != NULL);
    PCM_KEY_OBJECT Key = NULL;
    KEY_OBJECT_CREATE_CONTEXT Ctx = {
	.Volatile = FALSE,
    };
    RET_ERR(ObCreateObject(OBJECT_TYPE_KEY, (POBJECT *)&Key,
			   RegistryDirectory, "Machine", 0, &Ctx));
    RET_ERR(ObCreateObject(OBJECT_TYPE_KEY, (POBJECT *)&Key,
			   RegistryDirectory, "User", 0, &Ctx));
    ObDereferenceObject(RegistryDirectory);
    return STATUS_SUCCESS;
}
