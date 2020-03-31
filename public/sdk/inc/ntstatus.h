typedef LONG NTSTATUS;
typedef NTSTATUS *PNTSTATUS;

#define STATUS_SUCCESS                    ((NTSTATUS)0x00000000)

#define STATUS_SEVERITY_SUCCESS         0x0
#define STATUS_SEVERITY_INFORMATIONAL   0x1
#define STATUS_SEVERITY_WARNING         0x2
#define STATUS_SEVERITY_ERROR           0x3
#define ERROR_SEVERITY_SUCCESS		0x00000000
#define ERROR_SEVERITY_INFORMATIONAL	0x40000000
#define ERROR_SEVERITY_WARNING		0x80000000
#define ERROR_SEVERITY_ERROR		0xC0000000
/* Status is success if and only if highest bit is zero */
#define NT_SUCCESS(Status)	(((NTSTATUS)(Status)) >= 0)
#define NT_INFORMATION(Status) (((ULONG)(Status) >> 30) == STATUS_SEVERITY_INFORMATIONAL)
#define NT_WARNING(Status)	(((ULONG)(Status) >> 30) == STATUS_SEVERITY_WARNING)
#define NT_ERROR(Status)	(((ULONG)(Status) >> 30) == STATUS_SEVERITY_ERROR)
