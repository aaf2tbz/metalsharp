#include "ntdll_hook.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN64

static MS_HOOK_CONTEXT g_ctx = {0};
static const OBJECT_ATTRIBUTES null_obj_attrs = {0};

static UINT32 get_next_request_id(void) {
    return (UINT32)InterlockedIncrement((LONG *)&g_ctx.next_request_id);
}

static HANDLE get_real_ntdll_fn(const char *name) {
    static HMODULE ntdll_base = NULL;
    if (!ntdll_base) {
        ntdll_base = GetModuleHandleA("ntdll.dll");
    }
    if (!ntdll_base) return NULL;
    return (HANDLE)GetProcAddress(ntdll_base, name);
}

typedef NTSTATUS (WINAPI *NtOpenProcess_t)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PCLIENT_ID);
typedef NTSTATUS (WINAPI *NtOpenThread_t)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PCLIENT_ID);
typedef NTSTATUS (WINAPI *NtQuerySystemInformation_t)(ULONG, PVOID, ULONG, PULONG);
typedef NTSTATUS (WINAPI *NtQueryInformationProcess_t)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef NTSTATUS (WINAPI *NtClose_t)(HANDLE);
typedef NTSTATUS (WINAPI *NtDeviceIoControlFile_t)(HANDLE, HANDLE, PVOID, PVOID, PIO_STATUS_BLOCK, ULONG, PVOID, ULONG, PVOID, ULONG);

static NtOpenProcess_t real_NtOpenProcess = NULL;
static NtOpenThread_t real_NtOpenThread = NULL;
static NtQuerySystemInformation_t real_NtQuerySystemInformation = NULL;
static NtQueryInformationProcess_t real_NtQueryInformationProcess = NULL;
static NtClose_t real_NtClose = NULL;
static NtDeviceIoControlFile_t real_NtDeviceIoControlFile = NULL;

static void load_real_functions(void) {
    if (real_NtOpenProcess) return;
    real_NtOpenProcess = (NtOpenProcess_t)get_real_ntdll_fn("NtOpenProcess");
    real_NtOpenThread = (NtOpenThread_t)get_real_ntdll_fn("NtOpenThread");
    real_NtQuerySystemInformation = (NtQuerySystemInformation_t)get_real_ntdll_fn("NtQuerySystemInformation");
    real_NtQueryInformationProcess = (NtQueryInformationProcess_t)get_real_ntdll_fn("NtQueryInformationProcess");
    real_NtClose = (NtClose_t)get_real_ntdll_fn("NtClose");
    real_NtDeviceIoControlFile = (NtDeviceIoControlFile_t)get_real_ntdll_fn("NtDeviceIoControlFile");
}

BOOL ms_ipc_connect(void) {
    return FALSE;
}

void ms_ipc_disconnect(void) {
}

NTSTATUS ms_ipc_transact(
    UINT16 operation,
    const void *request_body,
    UINT32 request_size,
    void *response_body,
    UINT32 response_size
) {
    (void)operation;
    (void)request_body;
    (void)request_size;
    (void)response_body;
    (void)response_size;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS ms_hook_nt_open_process(
    PHANDLE ProcessHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    PCLIENT_ID ClientId
) {
    if (!ClientId || !ProcessHandle) {
        if (real_NtOpenProcess)
            return real_NtOpenProcess(ProcessHandle, DesiredAccess, ObjectAttributes, ClientId);
        return STATUS_INVALID_PARAMETER;
    }

    ULONG pid = (ULONG)(ULONG_PTR)ClientId->UniqueProcess;

    if (g_ctx.ipc_connected) {
        MS_IPC_NT_OPEN_PROCESS_REQ req = {
            .desired_access = DesiredAccess,
            .inherit_handle = ObjectAttributes ? ((ObjectAttributes->Attributes & 2) ? 1 : 0) : 0,
            .process_id = pid,
        };
        MS_IPC_NT_OPEN_PROCESS_RESP resp = {0};

        NTSTATUS status = ms_ipc_transact(
            MS_OP_NT_OPEN_PROCESS,
            &req, sizeof(req),
            &resp, sizeof(resp)
        );

        if (status == STATUS_SUCCESS && resp.nt_status == STATUS_SUCCESS) {
            *ProcessHandle = (HANDLE)(ULONG_PTR)resp.handle;
            return STATUS_SUCCESS;
        }
    }

    if (real_NtOpenProcess)
        return real_NtOpenProcess(ProcessHandle, DesiredAccess, ObjectAttributes, ClientId);

    return STATUS_ACCESS_DENIED;
}

NTSTATUS ms_hook_nt_open_thread(
    PHANDLE ThreadHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    PCLIENT_ID ClientId
) {
    if (!ClientId || !ThreadHandle) {
        if (real_NtOpenThread)
            return real_NtOpenThread(ThreadHandle, DesiredAccess, ObjectAttributes, ClientId);
        return STATUS_INVALID_PARAMETER;
    }

    if (g_ctx.ipc_connected) {
        MS_IPC_NT_OPEN_THREAD_REQ req = {
            .desired_access = DesiredAccess,
            .inherit_handle = ObjectAttributes ? ((ObjectAttributes->Attributes & 2) ? 1 : 0) : 0,
            .thread_id = (ULONG)(ULONG_PTR)ClientId->UniqueThread,
        };
        MS_IPC_NT_OPEN_THREAD_RESP resp = {0};

        NTSTATUS status = ms_ipc_transact(
            MS_OP_NT_OPEN_THREAD,
            &req, sizeof(req),
            &resp, sizeof(resp)
        );

        if (status == STATUS_SUCCESS && resp.nt_status == STATUS_SUCCESS) {
            *ThreadHandle = (HANDLE)(ULONG_PTR)resp.handle;
            return STATUS_SUCCESS;
        }
    }

    if (real_NtOpenThread)
        return real_NtOpenThread(ThreadHandle, DesiredAccess, ObjectAttributes, ClientId);

    return STATUS_ACCESS_DENIED;
}

NTSTATUS ms_hook_nt_query_system_information(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength
) {
    if (g_ctx.ipc_connected) {
        MS_IPC_NT_QUERY_SYSTEM_INFO_REQ req = {
            .info_class = SystemInformationClass,
            .buffer_length = SystemInformationLength,
        };
        MS_IPC_NT_QUERY_SYSTEM_INFO_RESP resp = {0};

        NTSTATUS status = ms_ipc_transact(
            MS_OP_NT_QUERY_SYSTEM_INFO,
            &req, sizeof(req),
            &resp, sizeof(resp)
        );

        if (status == STATUS_SUCCESS && resp.nt_status == STATUS_SUCCESS) {
            if (ReturnLength) *ReturnLength = resp.return_length;
            return STATUS_SUCCESS;
        }
    }

    if (real_NtQuerySystemInformation)
        return real_NtQuerySystemInformation(SystemInformationClass, SystemInformation, SystemInformationLength, ReturnLength);

    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS ms_hook_nt_query_information_process(
    HANDLE ProcessHandle,
    ULONG ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength,
    PULONG ReturnLength
) {
    if (g_ctx.ipc_connected) {
        MS_IPC_NT_QUERY_INFORMATION_PROC_REQ req = {
            .handle = (UINT64)(ULONG_PTR)ProcessHandle,
            .info_class = ProcessInformationClass,
            .buffer_length = ProcessInformationLength,
        };
        MS_IPC_NT_QUERY_INFORMATION_PROC_RESP resp = {0};

        NTSTATUS status = ms_ipc_transact(
            MS_OP_NT_QUERY_INFORMATION_PROC,
            &req, sizeof(req),
            &resp, sizeof(resp)
        );

        if (status == STATUS_SUCCESS && resp.nt_status == STATUS_SUCCESS) {
            if (ReturnLength) *ReturnLength = resp.return_length;
            return STATUS_SUCCESS;
        }
    }

    if (real_NtQueryInformationProcess)
        return real_NtQueryInformationProcess(ProcessHandle, ProcessInformationClass, ProcessInformation, ProcessInformationLength, ReturnLength);

    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS ms_hook_nt_close(HANDLE Handle) {
    if (g_ctx.ipc_connected) {
        MS_IPC_NT_CLOSE_REQ req = { .handle = (UINT64)(ULONG_PTR)Handle };
        MS_IPC_NT_CLOSE_RESP resp = {0};

        NTSTATUS status = ms_ipc_transact(
            MS_OP_NT_CLOSE,
            &req, sizeof(req),
            &resp, sizeof(resp)
        );

        if (status == STATUS_SUCCESS && resp.nt_status == STATUS_SUCCESS) {
            return STATUS_SUCCESS;
        }
    }

    if (real_NtClose)
        return real_NtClose(Handle);

    return STATUS_INVALID_HANDLE;
}

NTSTATUS ms_hook_nt_device_io_control_file(
    HANDLE FileHandle,
    HANDLE Event,
    PVOID ApcRoutine,
    PVOID ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock,
    ULONG IoControlCode,
    PVOID InputBuffer,
    ULONG InputBufferLength,
    PVOID OutputBuffer,
    ULONG OutputBufferLength
) {
    if (g_ctx.ipc_connected) {
        MS_IPC_NT_DEVICE_IO_CONTROL_REQ req = {
            .handle = (UINT64)(ULONG_PTR)FileHandle,
            .io_control_code = IoControlCode,
            .input_buffer_length = InputBufferLength,
            .output_buffer_length = OutputBufferLength,
        };

        UINT32 total_req = sizeof(req) + InputBufferLength;
        UINT8 *req_buf = (UINT8 *)HeapAlloc(GetProcessHeap(), 0, total_req);
        if (req_buf) {
            memcpy(req_buf, &req, sizeof(req));
            if (InputBuffer && InputBufferLength > 0)
                memcpy(req_buf + sizeof(req), InputBuffer, InputBufferLength);

            UINT32 total_resp = sizeof(MS_IPC_NT_DEVICE_IO_CONTROL_RESP) + OutputBufferLength;
            UINT8 *resp_buf = (UINT8 *)HeapAlloc(GetProcessHeap(), 0, total_resp);
            if (resp_buf) {
                NTSTATUS status = ms_ipc_transact(
                    MS_OP_NT_DEVICE_IO_CONTROL,
                    req_buf, total_req,
                    resp_buf, total_resp
                );

                if (status == STATUS_SUCCESS) {
                    MS_IPC_NT_DEVICE_IO_CONTROL_RESP *dev_resp = (MS_IPC_NT_DEVICE_IO_CONTROL_RESP *)resp_buf;
                    if (dev_resp->nt_status == STATUS_SUCCESS) {
                        if (IoStatusBlock) {
                            IoStatusBlock->Status = STATUS_SUCCESS;
                            IoStatusBlock->Information = (ULONG_PTR)dev_resp->bytes_returned;
                        }
                        if (OutputBuffer && dev_resp->output_buffer_length > 0) {
                            memcpy(OutputBuffer, resp_buf + sizeof(MS_IPC_NT_DEVICE_IO_CONTROL_RESP),
                                   dev_resp->output_buffer_length);
                        }
                        HeapFree(GetProcessHeap(), 0, resp_buf);
                        HeapFree(GetProcessHeap(), 0, req_buf);
                        return STATUS_SUCCESS;
                    }
                }
                HeapFree(GetProcessHeap(), 0, resp_buf);
            }
            HeapFree(GetProcessHeap(), 0, req_buf);
        }
    }

    if (real_NtDeviceIoControlFile)
        return real_NtDeviceIoControlFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, IoControlCode, InputBuffer, InputBufferLength, OutputBuffer, OutputBufferLength);

    return STATUS_NOT_IMPLEMENTED;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)hinstDLL;
    (void)lpvReserved;

    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        InitializeCriticalSection(&g_ctx.lock);
        load_real_functions();
        g_ctx.ipc_connected = ms_ipc_connect() ? 1 : 0;
        break;
    case DLL_PROCESS_DETACH:
        ms_ipc_disconnect();
        DeleteCriticalSection(&g_ctx.lock);
        break;
    }
    return TRUE;
}

#endif
