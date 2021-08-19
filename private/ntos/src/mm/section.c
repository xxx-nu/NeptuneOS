#include "mi.h"
#include "intsafe.h"

PSECTION MiPhysicalSection;

NTSTATUS MiSectionObjectCreateProc(IN POBJECT Object)
{
    PSECTION Section = (PSECTION) Object;
    MiAvlInitializeNode(&Section->BasedSectionNode, 0);
    Section->Flags.Word = 0;
    Section->ImageSectionObject = NULL;
    return STATUS_SUCCESS;
}

NTSTATUS MmSectionInitialization()
{
    OBJECT_TYPE_INITIALIZER TypeInfo = {
	.CreateProc = MiSectionObjectCreateProc,
	.OpenProc = NULL,
	.ParseProc = NULL,
	.InsertProc = NULL,
    };
    RET_ERR(ObCreateObjectType(OBJECT_TYPE_SECTION,
			       "Section",
			       sizeof(SECTION),
			       TypeInfo));

    /* Create VADs for "hyperspace", which is used for mapping client process
     * pages in the NTOS root task temporarily. The necessary super-structures
     * at starting addresses for 4K page hyperspace and large page hyperspace
     * will be mapped on-demand. */
    RET_ERR(MmReserveVirtualMemory(HYPERSPACE_4K_PAGE_START, LARGE_PAGE_SIZE,
				   MEM_RESERVE_MIRRORED_MEMORY));
    RET_ERR(MmReserveVirtualMemory(HYPERSPACE_LARGE_PAGE_START, LARGE_PAGE_SIZE,
				   MEM_RESERVE_MIRRORED_MEMORY));

    /* Create the singleton physical section. */
    RET_ERR(ObCreateObject(OBJECT_TYPE_SECTION, (POBJECT *) &MiPhysicalSection));
    assert(MiPhysicalSection != NULL);
    MiPhysicalSection->Flags.PhysicalMemory = 1;
    /* Physical section is always committed immediately. */
    MiPhysicalSection->Flags.Reserve = 1;
    MiPhysicalSection->Flags.Commit = 1;

    return STATUS_SUCCESS;
}

static inline VOID MiInitializeImageSection(IN PIMAGE_SECTION_OBJECT ImageSection,
					    IN PFILE_OBJECT File)
{
    memset(ImageSection, 0, sizeof(IMAGE_SECTION_OBJECT));
    InitializeListHead(&ImageSection->SubSectionList);
}

static inline VOID MiInitializeSubSection(IN PSUBSECTION SubSection,
					  IN PIMAGE_SECTION_OBJECT ImageSection)
{
    memset(SubSection, 0, sizeof(SUBSECTION));
    SubSection->ImageSection = ImageSection;
    InitializeListHead(&SubSection->Link);
}

/*
 * This is stolen shamelessly from the ReactOS source code.
 *
 * Parse the PE image headers and populate the given image section object,
 * including the SUBSECTIONs. The number of subsections equals the number of PE
 * sections of the image file plus the zeroth subsection which is the PE image
 * headers.
 *
 * References:
 *  [1] Microsoft Corporation, "Microsoft Portable Executable and Common Object
 *      File Format Specification", revision 6.0 (February 1999)
 */
#define DIE(...) { DbgTrace(__VA_ARGS__); return Status; }
static NTSTATUS MiParseImageHeaders(IN PVOID FileBuffer,
				    IN ULONG FileBufferSize,
				    IN PIMAGE_SECTION_OBJECT ImageSection)
{
    assert(FileBuffer);
    assert(FileBufferSize);
    assert(ImageSection != NULL);
    assert(Intsafe_CanOffsetPointer(FileBuffer, FileBufferSize));

    /*
     * DOS HEADER
     */
    PIMAGE_DOS_HEADER DosHeader = (PIMAGE_DOS_HEADER) FileBuffer;
    NTSTATUS Status = STATUS_INVALID_IMAGE_NOT_MZ;
    /* Image too small to be an MZ executable */
    if (FileBufferSize < sizeof(IMAGE_DOS_HEADER)) {
        DIE("Too small to be an MZ executable, size is 0x%x\n", FileBufferSize);
    }
    /* No MZ signature */
    if (DosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        DIE("No MZ signature found, e_magic is %hX\n", DosHeader->e_magic);
    }

    /*
     * NT HEADERS, which include COFF header and Optional header
     */
    Status = STATUS_INVALID_IMAGE_PROTECT;
    /* Not a Windows executable */
    if (DosHeader->e_lfanew <= 0) {
        DIE("Not a Windows executable, e_lfanew is %d\n", DosHeader->e_lfanew);
    }

    /* Make sure we have read the whole COFF header into memory. */
    ULONG OptHeaderOffset = 0;	/* File offset of optional header */
    if (!Intsafe_AddULong32(&OptHeaderOffset, DosHeader->e_lfanew,
			    RTL_SIZEOF_THROUGH_FIELD(IMAGE_NT_HEADERS, FileHeader))) {
        DIE("The DOS stub is too large, e_lfanew is %X\n", DosHeader->e_lfanew);
    }
    /* The buffer doesn't contain all of COFF headers: read it from the file */
    if (FileBufferSize < OptHeaderOffset) {
	return STATUS_NTOS_UNIMPLEMENTED;
    }

    /*
     * We already know that Intsafe_CanOffsetPointer(FileBuffer, FileBufferSize),
     * and FileBufferSize >= OptHeaderOffset, so this holds true too.
     */
    assert(Intsafe_CanOffsetPointer(FileBuffer, DosHeader->e_lfanew));
    PIMAGE_NT_HEADERS NtHeader = (PIMAGE_NT_HEADERS)
	((ULONG_PTR)FileBuffer + DosHeader->e_lfanew);

    /* Validate PE signature */
    Status = STATUS_INVALID_IMAGE_PROTECT;
    if (NtHeader->Signature != IMAGE_NT_SIGNATURE) {
	DIE("The file isn't a PE executable, Signature is %X\n", NtHeader->Signature);
    }

    Status = STATUS_INVALID_IMAGE_FORMAT;
    PIMAGE_OPTIONAL_HEADER OptHeader = &NtHeader->OptionalHeader;
    ULONG OptHeaderSize = NtHeader->FileHeader.SizeOfOptionalHeader;

    /* Make sure we have read the whole optional header into memory. */
    ULONG SectionHeadersOffset = 0; /* File offset of beginning of section header table */
    if (!Intsafe_AddULong32(&SectionHeadersOffset, OptHeaderOffset, OptHeaderSize)) {
	DIE("The NT header is too large, SizeOfOptionalHeader is %X\n", OptHeaderSize);
    }
    /* The buffer doesn't contain the full optional header: read it from the file */
    if (FileBufferSize < SectionHeadersOffset) {
	return STATUS_NTOS_UNIMPLEMENTED;
    }

    /* Read information from the optional header */
    Status = STATUS_INVALID_IMAGE_FORMAT;
    if (!RTL_CONTAINS_FIELD(OptHeader, OptHeaderSize, Magic)) {
        DIE("The optional header does not contain the Magic field,"
	    " SizeOfOptionalHeader is %X\n", OptHeaderSize);
    }

    /* We only process PE files that are native to the architecture, which for
     * i386 is PE32, and for amd64 is PE32+ */
    if (OptHeader->Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC) {
	DIE("Invalid optional header, magic is 0x%x\n", OptHeader->Magic);
    }

    /* Validate section alignment and file alignment */
    if (!RTL_CONTAINS_FIELD(OptHeader, OptHeaderSize, SectionAlignment) ||
	!RTL_CONTAINS_FIELD(OptHeader, OptHeaderSize, FileAlignment)) {
	DIE("Optional header does not contain section alignment or file alignment\n");
    }
    /* See [1], section 3.4.2 */
    if(OptHeader->SectionAlignment < PAGE_SIZE) {
	if(OptHeader->FileAlignment != OptHeader->SectionAlignment) {
	    DIE("Sections aren't page-aligned and the file alignment isn't the same\n");
	}
    } else if(OptHeader->SectionAlignment < OptHeader->FileAlignment) {
	DIE("The section alignment is smaller than the file alignment\n");
    }
    ULONG SectionAlignment = OptHeader->SectionAlignment;
    ULONG FileAlignment = OptHeader->FileAlignment;
    if (!IsPowerOf2(SectionAlignment) || !IsPowerOf2(FileAlignment)) {
	DIE("The section alignment (%u) and file alignment (%u)"
	    " must both be powers of two\n", SectionAlignment, FileAlignment);
    }
    if (SectionAlignment < PAGE_SIZE) {
	SectionAlignment = PAGE_SIZE;
    }

    /* Validate image base */
    if (!RTL_CONTAINS_FIELD(OptHeader, OptHeaderSize, ImageBase)) {
	DIE("Optional header does not contain ImageBase\n");
    }
    /* [1], section 3.4.2 */
    if (OptHeader->ImageBase % 0x10000) {
	DIE("ImageBase is not aligned on a 64KB boundary\n");
    }
    ImageSection->ImageBase = OptHeader->ImageBase;

    /* Populate image information of the image section object. */
    PSECTION_IMAGE_INFORMATION ImageInformation = &ImageSection->ImageInformation;
    if (RTL_CONTAINS_FIELD(OptHeader, OptHeaderSize, SizeOfImage)) {
	ImageInformation->ImageFileSize = OptHeader->SizeOfImage;
    }
    if (RTL_CONTAINS_FIELD(OptHeader, OptHeaderSize, SizeOfStackReserve)) {
	ImageInformation->MaximumStackSize = OptHeader->SizeOfStackReserve;
    }
    if (RTL_CONTAINS_FIELD(OptHeader, OptHeaderSize, SizeOfStackCommit)) {
	ImageInformation->CommittedStackSize = OptHeader->SizeOfStackCommit;
    }
    if (RTL_CONTAINS_FIELD(OptHeader, OptHeaderSize, Subsystem)) {
	ImageInformation->SubSystemType = OptHeader->Subsystem;
	if (RTL_CONTAINS_FIELD(OptHeader, OptHeaderSize, MinorSubsystemVersion) &&
	    RTL_CONTAINS_FIELD(OptHeader, OptHeaderSize, MajorSubsystemVersion)) {
	    ImageInformation->SubSystemMinorVersion = OptHeader->MinorSubsystemVersion;
	    ImageInformation->SubSystemMajorVersion = OptHeader->MajorSubsystemVersion;
	}
    }
    if (RTL_CONTAINS_FIELD(OptHeader, OptHeaderSize, AddressOfEntryPoint)) {
	ImageInformation->TransferAddress =
	    (PVOID) ((MWORD) (OptHeader->ImageBase + OptHeader->AddressOfEntryPoint));
    }
    if (RTL_CONTAINS_FIELD(OptHeader, OptHeaderSize, SizeOfCode)) {
	ImageInformation->ImageContainsCode = (OptHeader->SizeOfCode != 0);
    } else {
	ImageInformation->ImageContainsCode = TRUE;
    }
    if (RTL_CONTAINS_FIELD(OptHeader, OptHeaderSize, AddressOfEntryPoint) &&
	(OptHeader->AddressOfEntryPoint == 0)) {
	ImageInformation->ImageContainsCode = FALSE;
    }
    if (RTL_CONTAINS_FIELD(OptHeader, OptHeaderSize, LoaderFlags)) {
	ImageInformation->LoaderFlags = OptHeader->LoaderFlags;
    }
    if (RTL_CONTAINS_FIELD(OptHeader, OptHeaderSize, DllCharacteristics)) {
	ImageInformation->DllCharacteristics = OptHeader->DllCharacteristics;
    }

    /*
     * Since we don't really implement SxS yet and LD doesn't support
     * /ALLOWISOLATION:NO, hard-code this flag here, which will prevent
     * the loader and other code from doing any .manifest or SxS magic
     * to any binary.
     *
     * This will break applications that depend on SxS when running with
     * real Windows Kernel32/SxS/etc but honestly that's not tested. It
     * will also break them when running on ReactOS once we implement
     * the SxS support -- at which point, duh, this should be removed.
     *
     * But right now, any app depending on SxS is already broken anyway,
     * so this flag only helps.
     */
    ImageInformation->DllCharacteristics |= IMAGE_DLLCHARACTERISTICS_NO_ISOLATION;

    ImageInformation->ImageCharacteristics = NtHeader->FileHeader.Characteristics;
    ImageInformation->Machine = NtHeader->FileHeader.Machine;
    ImageInformation->GpValue = 0;
    ImageInformation->ZeroBits = 0;

    /*
     * SECTION HEADERS
     */
    Status = STATUS_INVALID_IMAGE_FORMAT;
    /* See [1], section 3.3 */
    if (NtHeader->FileHeader.NumberOfSections > 96) {
        DIE("Too many sections, NumberOfSections is %u\n",
	    NtHeader->FileHeader.NumberOfSections);
    }

    /* Size of the executable's headers. This includes the DOS header and
     * NT headers (the COFF header and the optional header), as well as
     * the section tables. */
    if (!RTL_CONTAINS_FIELD(OptHeader, OptHeaderSize, SizeOfHeaders)) {
	DIE("Optional header does not contain SizeOfHeaders member\n");
    }
    ULONG AllHeadersSize = OptHeader->SizeOfHeaders;
    if (!IS_ALIGNED(AllHeadersSize, FileAlignment)) {
        DIE("SizeOfHeaders is not aligned with file alignment\n");
    }
    assert(Intsafe_CanMulULong32(NtHeader->FileHeader.NumberOfSections,
				 sizeof(IMAGE_SECTION_HEADER)));
    if (AllHeadersSize <
	NtHeader->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER)) {
	DIE("SizeOfHeaders is less than number of sections times section header size\n");
    }
    /* Make sure we have read the entire section header table into memory. */
    if (FileBufferSize < AllHeadersSize) {
	return STATUS_NTOS_UNIMPLEMENTED;
    }

    /*
     * We already know that Intsafe_CanOffsetPointer(FileBuffer, FileBufferSize),
     * and FileBufferSize >= FirstSectionOffset, so this holds true too.
     */
    assert(Intsafe_CanOffsetPointer(FileBuffer, SectionHeadersOffset));
    PIMAGE_SECTION_HEADER SectionHeaders = (PIMAGE_SECTION_HEADER)
	((ULONG_PTR)FileBuffer + SectionHeadersOffset);

    /*
     * Parse the section tables and validate their values before we start
     * building SUBSECTIONS for the image section.
     *
     * See [1], section 4
     */
    Status = STATUS_INVALID_IMAGE_FORMAT;
    MWORD PreviousSectionEnd = ALIGN_UP_BY(AllHeadersSize, SectionAlignment);
    for (ULONG i = 0; i < NtHeader->FileHeader.NumberOfSections; i++) {
        ULONG Characteristics;

        /* Validate alignment */
        if (!IS_ALIGNED(SectionHeaders[i].VirtualAddress, SectionAlignment)) {
            DIE("Section %u VirtualAddress is not aligned\n", i);
	}

        /* Sections must be contiguous, ordered by base address and non-overlapping */
        if(SectionHeaders[i].VirtualAddress != PreviousSectionEnd)
            DIE("Memory gap between section %u and the previous\n", i);

        /* Ignore explicit BSS sections */
        if (SectionHeaders[i].SizeOfRawData != 0) {
#if 0
            /* Yes, this should be a multiple of FileAlignment, but there's
             * stuff out there that isn't. We can cope with that
             */
            if (!IS_ALIGNED(SectionHeaders[i].SizeOfRawData, FileAlignment)) {
                DIE("SizeOfRawData[%u] is not aligned\n", i);
	    }
            if (!IS_ALIGNED(SectionHeaders[i].PointerToRawData, FileAlignment)) {
                DIE("PointerToRawData[%u] is not aligned\n", i);
	    }
#endif
            if (!Intsafe_CanAddULong32(SectionHeaders[i].PointerToRawData,
				       SectionHeaders[i].SizeOfRawData)) {
                DIE("SizeOfRawData[%u] too large\n", i);
	    }
	}

	MWORD VirtualSize = SectionHeaders[i].Misc.VirtualSize;
	if (VirtualSize == 0) {
            VirtualSize = SectionHeaders[i].SizeOfRawData;
	}
	if (VirtualSize == 0) {
            DIE("Virtual size of section %d is null\n", i);
	}

        /* Ensure the memory image is no larger than 4GB */
	VirtualSize = ALIGN_UP_BY(VirtualSize, SectionAlignment);
        if (PreviousSectionEnd + VirtualSize < PreviousSectionEnd) {
            DIE("The image is too large\n");
	}
	PreviousSectionEnd += VirtualSize;
    }

    /*
     * Parse the section headers and allocate SUBSECTIONS for the image section object.
     */
    Status = STATUS_INSUFFICIENT_RESOURCES;
    /* One additional subsection for the image headers. */
    LONG NumSubSections = NtHeader->FileHeader.NumberOfSections + 1;
    MiAllocateArray(SubSectionsArray, PSUBSECTION, NumSubSections, {});
    for (LONG i = 0; i < NumSubSections; i++) {
	MiAllocatePoolEx(SubSection, SUBSECTION,
			 {
			     for (LONG j = 0; j < i; j++) {
				 if (SubSectionsArray[j] == NULL) {
				     return STATUS_NTOS_BUG;
				 }
				 ExFreePool(SubSectionsArray[j]);
			     }
			     ExFreePool(SubSectionsArray);
			 });
	MiInitializeSubSection(SubSection, ImageSection);
	SubSectionsArray[i] = SubSection;
    }
    ImageSection->NumSubSections = NumSubSections;

    /* The zeroth subsection of the image section is the image headers */
    SubSectionsArray[0]->SubSectionSize = ALIGN_UP_BY(AllHeadersSize, SectionAlignment);
    memcpy(SubSectionsArray[0]->Name, "PEIMGHDR", IMAGE_SIZEOF_SHORT_NAME);
    SubSectionsArray[0]->Name[IMAGE_SIZEOF_SHORT_NAME] = '\0';
    SubSectionsArray[0]->FileOffset = 0;
    SubSectionsArray[0]->RawDataSize = AllHeadersSize;
    SubSectionsArray[0]->SubSectionBase = 0;
    SubSectionsArray[0]->Characteristics = 0;
    InsertTailList(&ImageSection->SubSectionList, &SubSectionsArray[0]->Link);

    for (LONG i = 1; i < NumSubSections; i++) {
	SubSectionsArray[i]->RawDataSize = SectionHeaders[i-1].SizeOfRawData;
	SubSectionsArray[i]->FileOffset = SectionHeaders[i-1].PointerToRawData;

        ULONG Characteristics = SectionHeaders[i-1].Characteristics;
        /* Image has no explicit protection. */
        if ((Characteristics & (IMAGE_SCN_MEM_EXECUTE |
				IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE)) == 0) {
            if (Characteristics & IMAGE_SCN_CNT_CODE) {
                Characteristics |= IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
	    }
            if (Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA) {
                Characteristics |= IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
	    }
            if (Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA) {
                Characteristics |= IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
	    }
        }
	SubSectionsArray[i]->Characteristics = Characteristics;

	ULONG VirtualSize = SectionHeaders[i-1].Misc.VirtualSize;
	if (VirtualSize == 0) {
            VirtualSize = SectionHeaders[i-1].SizeOfRawData;
	}
	VirtualSize = ALIGN_UP_BY(VirtualSize, SectionAlignment);

        SubSectionsArray[i]->SubSectionSize = VirtualSize;
        SubSectionsArray[i]->SubSectionBase = SectionHeaders[i-1].VirtualAddress;
	memcpy(SubSectionsArray[i]->Name, SectionHeaders[i-1].Name, IMAGE_SIZEOF_SHORT_NAME);
	SubSectionsArray[i]->Name[IMAGE_SIZEOF_SHORT_NAME] = '\0';

	InsertTailList(&ImageSection->SubSectionList, &SubSectionsArray[i]->Link);
    }

    ExFreePool(SubSectionsArray);
    return STATUS_SUCCESS;
}
#undef DIE

static NTSTATUS MiCreateImageFileMap(IN PFILE_OBJECT File,
				     OUT PIMAGE_SECTION_OBJECT *pImageSection)
{
    assert(File != NULL);
    assert(pImageSection != NULL);
    MiAllocatePool(ImageSection, IMAGE_SECTION_OBJECT);
    MiInitializeImageSection(ImageSection, File);

    RET_ERR_EX(MiParseImageHeaders((PVOID) File->BufferPtr, File->Size, ImageSection),
	       ExFreePool(ImageSection));

    File->SectionObject.ImageSectionObject = ImageSection;
    ImageSection->FileObject = File;
    *pImageSection = ImageSection;
    return STATUS_SUCCESS;
}

NTSTATUS MmCreateSection(IN PFILE_OBJECT FileObject,
			 IN MWORD Attribute,
			 OUT PSECTION *SectionObject)
{
    assert(FileObject != NULL);
    assert(SectionObject != NULL);
    *SectionObject = NULL;

    /* Only image section is implemented for now */
    if (!(Attribute & SEC_IMAGE)) {
	return STATUS_NTOS_UNIMPLEMENTED;
    }

    if (!(Attribute & SEC_RESERVE)) {
	return STATUS_NTOS_UNIMPLEMENTED;
    }

    if (!(Attribute & SEC_COMMIT)) {
	return STATUS_NTOS_UNIMPLEMENTED;
    }

    PIMAGE_SECTION_OBJECT ImageSection = FileObject->SectionObject.ImageSectionObject;

    if (ImageSection == NULL) {
	RET_ERR(MiCreateImageFileMap(FileObject, &ImageSection));
	assert(ImageSection != NULL);
    }

    PSECTION Section = NULL;
    RET_ERR(ObCreateObject(OBJECT_TYPE_SECTION, (POBJECT *) &Section));
    assert(Section != NULL);
    Section->Flags.File = 1;
    Section->Flags.Image = 1;
    /* For now all sections are committed immediately. */
    Section->Flags.Reserve = 1;
    Section->Flags.Commit = 1;
    Section->ImageSectionObject = ImageSection;

    *SectionObject = Section;
    return STATUS_SUCCESS;
}

static NTSTATUS MiMapViewOfImageSection(IN PVIRT_ADDR_SPACE VSpace,
					IN PIMAGE_SECTION_OBJECT ImageSection,
					IN OUT OPTIONAL MWORD *pBaseAddress)
{
    assert(VSpace != NULL);
    assert(ImageSection != NULL);
    MWORD BaseAddress = ImageSection->ImageBase;

    if ((pBaseAddress != NULL) && (*pBaseAddress != 0)) {
	BaseAddress = *pBaseAddress;
    }

    /* For each subsection of the image section object, create a VAD that points
     * to the subsection, and commit the memory pages of that subsection. */
    LoopOverList(SubSection, &ImageSection->SubSectionList, SUBSECTION, Link) {
	PMMVAD Vad = NULL;
	/* For now we always map pages as private. In the future when we have
	 * implemented the cache manager, pages can be shared between the cache
	 * manager and the client processes. For this to happen the pages must
	 * be mapped read-only and the start of the data buffer happens to fall
	 * on page boundary.
	 *
	 * Of course, sections can be shared with other client processes. This is
	 * also left as future work. */
	MWORD Flags = MEM_RESERVE_IMAGE_MAP | MEM_RESERVE_OWNED_MEMORY;
	if (!(SubSection->Characteristics & IMAGE_SCN_MEM_WRITE)) {
	    Flags |= MEM_RESERVE_READ_ONLY;
	}
	/* If PE data section is large enough, use large pages to save resources */
	if (SubSection->RawDataSize >= (LARGE_PAGE_SIZE - PAGE_SIZE)) {
	    Flags |= MEM_RESERVE_LARGE_PAGES;
	}
	RET_ERR_EX(MmReserveVirtualMemoryEx(VSpace, BaseAddress + SubSection->SubSectionBase,
					    0, SubSection->SubSectionSize, Flags, &Vad),
		   {
		       /* TODO: Clean up when error */
		   });
	assert(Vad != NULL);
	Vad->ImageSectionView.SubSection = SubSection;

	if (BaseAddress != ImageSection->ImageBase) {
	    /* TODO: Perform relocation */
	    return STATUS_NTOS_UNIMPLEMENTED;
	}

	RET_ERR_EX(MiCommitImageVad(Vad),
		   {
		       /* TODO: Clean up when error */
		   });
    }

    if (pBaseAddress != NULL) {
	if (*pBaseAddress != 0) {
	    BaseAddress = *pBaseAddress;
	} else {
	    *pBaseAddress = BaseAddress;
	}
    }
    return STATUS_SUCCESS;
}

static NTSTATUS MiMapViewOfPhysicalSection(IN PVIRT_ADDR_SPACE VSpace,
					   IN MWORD PhysicalBase,
					   IN MWORD VirtualBase,
					   IN MWORD WindowSize)
{
    assert(VSpace != NULL);
    PMMVAD Vad = NULL;
    RET_ERR(MmReserveVirtualMemoryEx(VSpace, VirtualBase, 0, WindowSize,
				     MEM_RESERVE_PHYSICAL_MAPPING, &Vad));
    assert(Vad != NULL);
    Vad->PhysicalSectionView.PhysicalBase = PhysicalBase;
    RET_ERR(MmCommitVirtualMemoryEx(VSpace, VirtualBase, WindowSize, 0));
    return STATUS_SUCCESS;
}

NTSTATUS MmMapPhysicalMemory(IN MWORD PhysicalBase,
			     IN MWORD VirtualBase,
			     IN MWORD WindowSize)
{
    return MmMapViewOfSection(&MiNtosVaddrSpace, MiPhysicalSection, &VirtualBase,
			      &PhysicalBase, &WindowSize);
}

/*
 * Map a view of the given section onto the given virtual address space.
 *
 * For now we commit the full view (CommitSize == ViewSize).
 */
NTSTATUS MmMapViewOfSection(IN PVIRT_ADDR_SPACE VSpace,
			    IN PSECTION Section,
			    IN OUT OPTIONAL MWORD *BaseAddress,
			    IN OUT OPTIONAL MWORD *SectionOffset,
			    IN OUT OPTIONAL MWORD *ViewSize)
{
    assert(VSpace != NULL);
    assert(Section != NULL);
    if (Section->Flags.Image) {
	if (SectionOffset != NULL) {
	    return STATUS_INVALID_PARAMETER;
	}
	if (ViewSize != NULL) {
	    return STATUS_INVALID_PARAMETER;
	}
	return MiMapViewOfImageSection(VSpace, Section->ImageSectionObject, BaseAddress);
    } else if (Section->Flags.PhysicalMemory) {
	assert((BaseAddress != NULL) && (*BaseAddress));
	assert((SectionOffset != NULL) && (*SectionOffset));
	assert((ViewSize != NULL) && (*ViewSize));
	return MiMapViewOfPhysicalSection(VSpace, *SectionOffset, *BaseAddress, *ViewSize);
    }
    return STATUS_NTOS_UNIMPLEMENTED;
}

static VOID MiDbgDumpSubSection(IN PSUBSECTION SubSection)
{
    DbgPrint("Dumping sub-section %p\n", SubSection);
    if (SubSection == NULL) {
	DbgPrint("    (nil)\n");
	return;
    }
    DbgPrint("    parent image section object = %p\n", SubSection->ImageSection);
    DbgPrint("    sub-section name = %s\n", SubSection->Name);
    DbgPrint("    sub-section size = 0x%x\n", SubSection->SubSectionSize);
    DbgPrint("    starting file offset = 0x%x\n", SubSection->FileOffset);
    DbgPrint("    raw data size = 0x%x\n", SubSection->RawDataSize);
    DbgPrint("    sub-section base = 0x%x\n", SubSection->SubSectionBase);
    DbgPrint("    characteristics = 0x%x\n", SubSection->Characteristics);
}

static VOID MiDbgDumpImageSectionObject(IN PIMAGE_SECTION_OBJECT ImageSection)
{
    DbgPrint("Dumping image section object %p\n", ImageSection);
    if (ImageSection == NULL) {
	DbgPrint("    (nil)\n");
	return;
    }
    DbgPrint("    Number of subsections = %d\n", ImageSection->NumSubSections);
    DbgPrint("    Image base = %p\n", (PVOID) ImageSection->ImageBase);
    DbgPrint("    TransferAddress = %p\n",
	     (PVOID) ImageSection->ImageInformation.TransferAddress);
    DbgPrint("    ZeroBits = 0x%x\n",
	     ImageSection->ImageInformation.ZeroBits);
    DbgPrint("    MaximumStackSize = 0x%zx\n",
	     (MWORD) ImageSection->ImageInformation.MaximumStackSize);
    DbgPrint("    CommittedStackSize = 0x%zx\n",
	     (MWORD) ImageSection->ImageInformation.CommittedStackSize);
    DbgPrint("    SubSystemType = 0x%x\n",
	     ImageSection->ImageInformation.SubSystemType);
    DbgPrint("    SubSystemMinorVersion = 0x%x\n",
	     ImageSection->ImageInformation.SubSystemMinorVersion);
    DbgPrint("    SubSystemMajorVersion = 0x%x\n",
	     ImageSection->ImageInformation.SubSystemMajorVersion);
    DbgPrint("    GpValue = 0x%x\n",
	     ImageSection->ImageInformation.GpValue);
    DbgPrint("    ImageCharacteristics = 0x%x\n",
	     ImageSection->ImageInformation.ImageCharacteristics);
    DbgPrint("    DllCharacteristics = 0x%x\n",
	     ImageSection->ImageInformation.DllCharacteristics);
    DbgPrint("    Machine = 0x%x\n",
	     ImageSection->ImageInformation.Machine);
    if (ImageSection->ImageInformation.ImageContainsCode) {
	DbgPrint("    Image contains code\n");
    } else {
	DbgPrint("    Image does not contain code\n");
    }
    DbgPrint("    ImageFlags = 0x%x\n",
	     ImageSection->ImageInformation.ImageFlags);
    DbgPrint("    LoaderFlags = 0x%x\n",
	     ImageSection->ImageInformation.LoaderFlags);
    DbgPrint("    ImageFileSize = 0x%x\n",
	     ImageSection->ImageInformation.ImageFileSize);
    DbgPrint("    CheckSum = 0x%x\n",
	     ImageSection->ImageInformation.CheckSum);
    DbgPrint("    FileObject = %p\n", ImageSection->FileObject);
    IoDbgDumpFileObject(ImageSection->FileObject);
    LoopOverList(SubSection, &ImageSection->SubSectionList, SUBSECTION, Link) {
	MiDbgDumpSubSection(SubSection);
    }
}

static VOID MiDbgDumpDataSectionObject(IN PDATA_SECTION_OBJECT DataSection)
{
    DbgPrint("Dumping data section object %p\n", DataSection);
    if (DataSection == NULL) {
	DbgPrint("    (nil)\n");
	return;
    }
    DbgPrint("    UNIMPLEMENTED\n");
}

VOID MmDbgDumpSection(PSECTION Section)
{
    DbgPrint("Dumping section %p\n", Section);
    if (Section == NULL) {
	DbgPrint("    (nil)\n");
    }
    DbgPrint("    Flags: %s%s%s%s%s%s\n",
	     Section->Flags.Image ? " image" : "",
	     Section->Flags.Based ? " based" : "",
	     Section->Flags.File ? " file" : "",
	     Section->Flags.PhysicalMemory ? " physical-memory" : "",
	     Section->Flags.Reserve ? " reserve" : "",
	     Section->Flags.Commit ? " commit" : "");
    if (Section->Flags.Image) {
	MiDbgDumpImageSectionObject(Section->ImageSectionObject);
    } else {
	MiDbgDumpDataSectionObject(Section->DataSectionObject);
    }
}
