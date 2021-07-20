/* Routines for virtual address space management */

#include "mi.h"

VOID MmInitializeVaddrSpace(IN PMM_VADDR_SPACE Self,
			    IN MWORD VSpaceCap)
{
    Self->VSpaceCap = VSpaceCap;
    Self->CachedVad = NULL;
    MiAvlInitializeTree(&Self->VadTree);
    MiInitializePagingStructure(&Self->RootPagingStructure, NULL, NULL, VSpaceCap,
				VSpaceCap, 0, MM_PAGING_TYPE_ROOT_PAGING_STRUCTURE,
				TRUE, MM_RIGHTS_RW);
}

/* Returns TRUE if the supplied VAD node overlaps with the address window */
static inline BOOLEAN MiVadNodeOverlapsAddrWindow(IN PMM_VAD Node,
						  IN MWORD VirtAddr,
						  IN MWORD WindowSize)
{
    return MiAvlNodeOverlapsAddrWindow(&Node->AvlNode, Node->WindowSize,
				       VirtAddr, WindowSize);
}

/* Returns TRUE if two VAD nodes have non-zero overlap */
static inline BOOLEAN MiVadNodeOverlapsVad(IN PMM_VAD Node0,
					   IN PMM_VAD Node1)
{
    return MiVadNodeOverlapsAddrWindow(Node0, Node1->AvlNode.Key, Node1->WindowSize);
}

/*
 * Insert the given VAD node into the virtual address space
 */
static NTSTATUS MiVSpaceInsertVadNode(IN PMM_VADDR_SPACE VSpace,
				      IN PMM_VAD VadNode)
{
    PMM_AVL_TREE Tree = &VSpace->VadTree;
    PMM_AVL_NODE ParentNode = MiAvlTreeFindNodeOrParent(Tree, VadNode->AvlNode.Key);
    PMM_VAD ParentVad = MM_AVL_NODE_TO_VAD(ParentNode);

    /* Vad nodes within a VAD tree should never overlap. Is a bug if it does. */
    if (ParentVad != NULL && MiVadNodeOverlapsVad(VadNode, ParentVad)) {
	return STATUS_NTOS_BUG;
    }
    MiAvlTreeInsertNode(Tree, ParentNode, &VadNode->AvlNode);
    return STATUS_SUCCESS;
}

/*
 * Returns the VAD node that overlaps with the supplied address window.
 * If multiple VADs overlap with the given address window, return the VAD
 * with the lowest address.
 */
static PMM_VAD MiVSpaceFindOverlappingVadNode(IN PMM_VADDR_SPACE VSpace,
					      IN MWORD VirtAddr,
					      IN MWORD WindowSize)
{
    PMM_AVL_TREE Tree = &VSpace->VadTree;
    if (VSpace->CachedVad != NULL &&
	MiVadNodeOverlapsAddrWindow(VSpace->CachedVad, VirtAddr, WindowSize)) {
	return VSpace->CachedVad;
    }
    PMM_VAD Parent = MM_AVL_NODE_TO_VAD(MiAvlTreeFindNodeOrParent(Tree, VirtAddr));
    if (Parent != NULL && MiVadNodeOverlapsAddrWindow(Parent, VirtAddr, WindowSize)) {
	VSpace->CachedVad = Parent;
	return Parent;
    }
    return NULL;
}

NTSTATUS MmReserveVirtualMemoryEx(IN PMM_VADDR_SPACE VSpace,
				  IN MWORD VirtAddr,
				  IN MWORD WindowSize)
{
    assert(IS_PAGE_ALIGNED(VirtAddr));
    if (MiVSpaceFindOverlappingVadNode(VSpace, VirtAddr, WindowSize) != NULL) {
	/* Enlarge VAD? */
	return STATUS_INVALID_PARAMETER;
    }

    MiAllocatePool(Vad, MM_VAD);
    MiInitializeVadNode(Vad, VirtAddr, WindowSize);
    MiVSpaceInsertVadNode(VSpace, Vad);
    return STATUS_SUCCESS;
}
