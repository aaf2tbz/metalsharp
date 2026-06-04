#ifndef MS_NTDLL_HOOK_H
#define MS_NTDLL_HOOK_H

#include <windows.h>

typedef LONG NTSTATUS;
typedef NTSTATUS* PNTSTATUS;

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
#ifndef STATUS_NOT_IMPLEMENTED
#define STATUS_NOT_IMPLEMENTED ((NTSTATUS)0xC0000002L)
#endif
#ifndef STATUS_ACCESS_DENIED
#define STATUS_ACCESS_DENIED ((NTSTATUS)0xC0000022L)
#endif
#ifndef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000DL)
#endif
#ifndef STATUS_INVALID_HANDLE
#define STATUS_INVALID_HANDLE ((NTSTATUS)0xC0000008L)
#endif
#ifndef STATUS_INFO_LENGTH_MISMATCH
#define STATUS_INFO_LENGTH_MISMATCH ((NTSTATUS)0xC0000004L)
#endif
#ifndef STATUS_BUFFER_TOO_SMALL
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023L)
#endif
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

typedef struct _CLIENT_ID {
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
} CLIENT_ID, *PCLIENT_ID;

typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

typedef struct _OBJECT_ATTRIBUTES {
    ULONG Length;
    HANDLE RootDirectory;
    PUNICODE_STRING ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

typedef struct _IO_STATUS_BLOCK {
    NTSTATUS Status;
    ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

typedef ULONG SYSTEM_INFORMATION_CLASS;

#define MS_IPC_HOST    "127.0.0.1"
#define MS_IPC_PORT    19384
#define MS_IPC_MAGIC   0x4D534B54
#define MS_IPC_VERSION 1

#define MS_OP_NT_OPEN_PROCESS           0x0001
#define MS_OP_NT_OPEN_THREAD            0x0002
#define MS_OP_NT_QUERY_SYSTEM_INFO      0x0003
#define MS_OP_NT_QUERY_INFORMATION_PROC 0x0004
#define MS_OP_NT_QUERY_OBJECT           0x0005
#define MS_OP_NT_SET_INFORMATION_THREAD 0x0006
#define MS_OP_NT_CLOSE                  0x0007
#define MS_OP_NT_DUPLICATE_OBJECT       0x0008
#define MS_OP_NT_QUERY_VIRTUAL_MEMORY   0x0009
#define MS_OP_NT_QUERY_INFORMATION_FILE 0x000A
#define MS_OP_NT_CREATE_FILE            0x000B
#define MS_OP_NT_READ_FILE              0x000C
#define MS_OP_NT_DEVICE_IO_CONTROL      0x000D
#define MS_OP_NT_LOAD_DRIVER            0x000E
#define MS_OP_NT_UNLOAD_DRIVER          0x000F

#pragma pack(push, 1)
typedef struct _MS_IPC_HEADER {
    UINT32 magic;
    UINT16 version;
    UINT16 operation;
    UINT32 request_id;
    UINT32 body_size;
} MS_IPC_HEADER;

typedef struct _MS_IPC_NT_OPEN_PROCESS_REQ {
    ACCESS_MASK desired_access;
    UINT32 inherit_handle;
    UINT32 process_id;
} MS_IPC_NT_OPEN_PROCESS_REQ;

typedef struct _MS_IPC_NT_OPEN_PROCESS_RESP {
    INT32 nt_status;
    UINT64 handle;
} MS_IPC_NT_OPEN_PROCESS_RESP;

typedef struct _MS_IPC_NT_OPEN_THREAD_REQ {
    ACCESS_MASK desired_access;
    UINT32 inherit_handle;
    UINT32 thread_id;
} MS_IPC_NT_OPEN_THREAD_REQ;

typedef struct _MS_IPC_NT_OPEN_THREAD_RESP {
    INT32 nt_status;
    UINT64 handle;
} MS_IPC_NT_OPEN_THREAD_RESP;

typedef struct _MS_IPC_NT_QUERY_SYSTEM_INFO_REQ {
    UINT32 info_class;
    UINT32 buffer_length;
} MS_IPC_NT_QUERY_SYSTEM_INFO_REQ;

typedef struct _MS_IPC_NT_QUERY_SYSTEM_INFO_RESP {
    INT32 nt_status;
    UINT32 return_length;
    UINT32 buffer_length;
} MS_IPC_NT_QUERY_SYSTEM_INFO_RESP;

typedef struct _MS_IPC_NT_CLOSE_REQ {
    UINT64 handle;
} MS_IPC_NT_CLOSE_REQ;

typedef struct _MS_IPC_NT_CLOSE_RESP {
    INT32 nt_status;
} MS_IPC_NT_CLOSE_RESP;

typedef struct _MS_IPC_NT_QUERY_INFORMATION_PROC_REQ {
    UINT64 handle;
    UINT32 info_class;
    UINT32 buffer_length;
} MS_IPC_NT_QUERY_INFORMATION_PROC_REQ;

typedef struct _MS_IPC_NT_QUERY_INFORMATION_PROC_RESP {
    INT32 nt_status;
    UINT32 return_length;
    UINT32 buffer_length;
} MS_IPC_NT_QUERY_INFORMATION_PROC_RESP;

typedef struct _MS_IPC_NT_DEVICE_IO_CONTROL_REQ {
    UINT64 handle;
    UINT32 io_control_code;
    UINT32 input_buffer_length;
    UINT32 output_buffer_length;
} MS_IPC_NT_DEVICE_IO_CONTROL_REQ;

typedef struct _MS_IPC_NT_DEVICE_IO_CONTROL_RESP {
    INT32 nt_status;
    UINT32 bytes_returned;
    UINT32 output_buffer_length;
} MS_IPC_NT_DEVICE_IO_CONTROL_RESP;

typedef struct _MS_IPC_GENERIC_RESP {
    INT32 nt_status;
    UINT32 data_length;
} MS_IPC_GENERIC_RESP;
#pragma pack(pop)

typedef struct _MS_HOOK_CONTEXT {
    volatile LONG initialized;
    volatile LONG ipc_connected;
    CRITICAL_SECTION lock;
    HANDLE ipc_thread;
    UINT32 next_request_id;
} MS_HOOK_CONTEXT;

NTSTATUS ms_ipc_transact(UINT16 operation, const void* request_body, UINT32 request_size, void* response_body,
                         UINT32 response_size);

BOOL ms_ipc_connect(void);
void ms_ipc_disconnect(void);

NTSTATUS ms_hook_nt_open_process(PHANDLE ProcessHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
                                 PCLIENT_ID ClientId);

NTSTATUS ms_hook_nt_open_thread(PHANDLE ThreadHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
                                PCLIENT_ID ClientId);

NTSTATUS ms_hook_nt_query_system_information(ULONG SystemInformationClass, PVOID SystemInformation,
                                             ULONG SystemInformationLength, PULONG ReturnLength);

NTSTATUS ms_hook_nt_query_information_process(HANDLE ProcessHandle, ULONG ProcessInformationClass,
                                              PVOID ProcessInformation, ULONG ProcessInformationLength,
                                              PULONG ReturnLength);

NTSTATUS ms_hook_nt_close(HANDLE Handle);

NTSTATUS ms_hook_nt_device_io_control_file(HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext,
                                           PIO_STATUS_BLOCK IoStatusBlock, ULONG IoControlCode, PVOID InputBuffer,
                                           ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength);

#endif
