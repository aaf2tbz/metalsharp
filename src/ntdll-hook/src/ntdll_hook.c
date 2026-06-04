#include "ntdll_hook.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#ifdef _WIN64

static MS_HOOK_CONTEXT g_ctx = {0};
static SOCKET g_sock = INVALID_SOCKET;
static WSADATA g_wsa = {0};

static UINT32 get_next_request_id(void) {
    return (UINT32)InterlockedIncrement((LONG*)&g_ctx.next_request_id);
}

typedef NTSTATUS(WINAPI* NtOpenProcess_t)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PCLIENT_ID);
typedef NTSTATUS(WINAPI* NtOpenThread_t)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PCLIENT_ID);
typedef NTSTATUS(WINAPI* NtQuerySystemInformation_t)(ULONG, PVOID, ULONG, PULONG);
typedef NTSTATUS(WINAPI* NtQueryInformationProcess_t)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef NTSTATUS(WINAPI* NtClose_t)(HANDLE);
typedef NTSTATUS(WINAPI* NtDeviceIoControlFile_t)(HANDLE, HANDLE, PVOID, PVOID, PIO_STATUS_BLOCK, ULONG, PVOID, ULONG,
                                                  PVOID, ULONG);

static NtOpenProcess_t real_NtOpenProcess = NULL;
static NtOpenThread_t real_NtOpenThread = NULL;
static NtQuerySystemInformation_t real_NtQuerySystemInformation = NULL;
static NtQueryInformationProcess_t real_NtQueryInformationProcess = NULL;
static NtClose_t real_NtClose = NULL;
static NtDeviceIoControlFile_t real_NtDeviceIoControlFile = NULL;

static HANDLE get_real_ntdll_fn(const char* name) {
    static HMODULE ntdll_base = NULL;
    if (!ntdll_base) {
        ntdll_base = GetModuleHandleA("ntdll.dll");
    }
    if (!ntdll_base)
        return NULL;
    return (HANDLE)GetProcAddress(ntdll_base, name);
}

static void load_real_functions(void) {
    if (real_NtOpenProcess)
        return;
    real_NtOpenProcess = (NtOpenProcess_t)get_real_ntdll_fn("NtOpenProcess");
    real_NtOpenThread = (NtOpenThread_t)get_real_ntdll_fn("NtOpenThread");
    real_NtQuerySystemInformation = (NtQuerySystemInformation_t)get_real_ntdll_fn("NtQuerySystemInformation");
    real_NtQueryInformationProcess = (NtQueryInformationProcess_t)get_real_ntdll_fn("NtQueryInformationProcess");
    real_NtClose = (NtClose_t)get_real_ntdll_fn("NtClose");
    real_NtDeviceIoControlFile = (NtDeviceIoControlFile_t)get_real_ntdll_fn("NtDeviceIoControlFile");
}

static BOOL send_all(SOCKET s, const void* buf, int len) {
    const char* p = (const char*)buf;
    int remaining = len;
    while (remaining > 0) {
        int sent = send(s, p, remaining, 0);
        if (sent <= 0)
            return FALSE;
        p += sent;
        remaining -= sent;
    }
    return TRUE;
}

static BOOL recv_all(SOCKET s, void* buf, int len) {
    char* p = (char*)buf;
    int remaining = len;
    while (remaining > 0) {
        int recvd = recv(s, p, remaining, 0);
        if (recvd <= 0)
            return FALSE;
        p += recvd;
        remaining -= recvd;
    }
    return TRUE;
}

BOOL ms_ipc_connect(void) {
    struct sockaddr_in addr;

    if (WSAStartup(MAKEWORD(2, 2), &g_wsa) != 0) {
        return FALSE;
    }

    g_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_sock == INVALID_SOCKET) {
        WSACleanup();
        return FALSE;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(MS_IPC_PORT);
    addr.sin_addr.s_addr = inet_addr(MS_IPC_HOST);

    if (connect(g_sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
        WSACleanup();
        return FALSE;
    }

    {
        BOOL nodelay = TRUE;
        setsockopt(g_sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
    }

    return TRUE;
}

void ms_ipc_disconnect(void) {
    if (g_sock != INVALID_SOCKET) {
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
    }
    WSACleanup();
}

NTSTATUS ms_ipc_transact(UINT16 operation, const void* request_body, UINT32 request_size, void* response_body,
                         UINT32 response_size) {
    MS_IPC_HEADER req_hdr;
    MS_IPC_HEADER resp_hdr;
    UINT8* resp_json = NULL;

    if (g_sock == INVALID_SOCKET) {
        return STATUS_NOT_IMPLEMENTED;
    }

    EnterCriticalSection(&g_ctx.lock);

    req_hdr.magic = MS_IPC_MAGIC;
    req_hdr.version = MS_IPC_VERSION;
    req_hdr.operation = operation;
    req_hdr.request_id = get_next_request_id();
    req_hdr.body_size = request_size;

    if (!send_all(g_sock, &req_hdr, sizeof(req_hdr))) {
        LeaveCriticalSection(&g_ctx.lock);
        return STATUS_NOT_IMPLEMENTED;
    }

    if (request_size > 0 && request_body) {
        if (!send_all(g_sock, request_body, (int)request_size)) {
            LeaveCriticalSection(&g_ctx.lock);
            return STATUS_NOT_IMPLEMENTED;
        }
    }

    if (!recv_all(g_sock, &resp_hdr, sizeof(resp_hdr))) {
        LeaveCriticalSection(&g_ctx.lock);
        return STATUS_NOT_IMPLEMENTED;
    }

    if (resp_hdr.magic != MS_IPC_MAGIC || resp_hdr.body_size == 0) {
        LeaveCriticalSection(&g_ctx.lock);
        return STATUS_NOT_IMPLEMENTED;
    }

    if (resp_hdr.body_size > 65536) {
        LeaveCriticalSection(&g_ctx.lock);
        return STATUS_NOT_IMPLEMENTED;
    }

    resp_json = (UINT8*)HeapAlloc(GetProcessHeap(), 0, resp_hdr.body_size);
    if (!resp_json) {
        LeaveCriticalSection(&g_ctx.lock);
        return STATUS_NOT_IMPLEMENTED;
    }

    if (!recv_all(g_sock, resp_json, (int)resp_hdr.body_size)) {
        HeapFree(GetProcessHeap(), 0, resp_json);
        LeaveCriticalSection(&g_ctx.lock);
        return STATUS_NOT_IMPLEMENTED;
    }

    if (response_body && response_size > 0) {
        UINT32 copy_len = response_size < resp_hdr.body_size ? response_size : resp_hdr.body_size;
        memcpy(response_body, resp_json, copy_len);
    }

    HeapFree(GetProcessHeap(), 0, resp_json);
    LeaveCriticalSection(&g_ctx.lock);

    return STATUS_SUCCESS;
}

NTSTATUS ms_hook_nt_open_process(PHANDLE ProcessHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
                                 PCLIENT_ID ClientId) {
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

        NTSTATUS status = ms_ipc_transact(MS_OP_NT_OPEN_PROCESS, &req, sizeof(req), &resp, sizeof(resp));

        if (status == STATUS_SUCCESS && resp.nt_status == STATUS_SUCCESS) {
            *ProcessHandle = (HANDLE)(ULONG_PTR)resp.handle;
            return STATUS_SUCCESS;
        }
    }

    if (real_NtOpenProcess)
        return real_NtOpenProcess(ProcessHandle, DesiredAccess, ObjectAttributes, ClientId);

    return STATUS_ACCESS_DENIED;
}

NTSTATUS ms_hook_nt_open_thread(PHANDLE ThreadHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
                                PCLIENT_ID ClientId) {
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

        NTSTATUS status = ms_ipc_transact(MS_OP_NT_OPEN_THREAD, &req, sizeof(req), &resp, sizeof(resp));

        if (status == STATUS_SUCCESS && resp.nt_status == STATUS_SUCCESS) {
            *ThreadHandle = (HANDLE)(ULONG_PTR)resp.handle;
            return STATUS_SUCCESS;
        }
    }

    if (real_NtOpenThread)
        return real_NtOpenThread(ThreadHandle, DesiredAccess, ObjectAttributes, ClientId);

    return STATUS_ACCESS_DENIED;
}

NTSTATUS ms_hook_nt_query_system_information(ULONG SystemInformationClass, PVOID SystemInformation,
                                             ULONG SystemInformationLength, PULONG ReturnLength) {
    if (g_ctx.ipc_connected) {
        MS_IPC_NT_QUERY_SYSTEM_INFO_REQ req = {
            .info_class = SystemInformationClass,
            .buffer_length = SystemInformationLength,
        };
        MS_IPC_NT_QUERY_SYSTEM_INFO_RESP resp = {0};

        NTSTATUS status = ms_ipc_transact(MS_OP_NT_QUERY_SYSTEM_INFO, &req, sizeof(req), &resp, sizeof(resp));

        if (status == STATUS_SUCCESS && resp.nt_status == STATUS_SUCCESS) {
            if (ReturnLength)
                *ReturnLength = resp.return_length;
            return STATUS_SUCCESS;
        }
    }

    if (real_NtQuerySystemInformation)
        return real_NtQuerySystemInformation(SystemInformationClass, SystemInformation, SystemInformationLength,
                                             ReturnLength);

    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS ms_hook_nt_query_information_process(HANDLE ProcessHandle, ULONG ProcessInformationClass,
                                              PVOID ProcessInformation, ULONG ProcessInformationLength,
                                              PULONG ReturnLength) {
    if (g_ctx.ipc_connected) {
        MS_IPC_NT_QUERY_INFORMATION_PROC_REQ req = {
            .handle = (UINT64)(ULONG_PTR)ProcessHandle,
            .info_class = ProcessInformationClass,
            .buffer_length = ProcessInformationLength,
        };
        MS_IPC_NT_QUERY_INFORMATION_PROC_RESP resp = {0};

        NTSTATUS status = ms_ipc_transact(MS_OP_NT_QUERY_INFORMATION_PROC, &req, sizeof(req), &resp, sizeof(resp));

        if (status == STATUS_SUCCESS && resp.nt_status == STATUS_SUCCESS) {
            if (ReturnLength)
                *ReturnLength = resp.return_length;
            return STATUS_SUCCESS;
        }
    }

    if (real_NtQueryInformationProcess)
        return real_NtQueryInformationProcess(ProcessHandle, ProcessInformationClass, ProcessInformation,
                                              ProcessInformationLength, ReturnLength);

    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS ms_hook_nt_close(HANDLE Handle) {
    if (g_ctx.ipc_connected) {
        MS_IPC_NT_CLOSE_REQ req = {.handle = (UINT64)(ULONG_PTR)Handle};
        MS_IPC_NT_CLOSE_RESP resp = {0};

        NTSTATUS status = ms_ipc_transact(MS_OP_NT_CLOSE, &req, sizeof(req), &resp, sizeof(resp));

        if (status == STATUS_SUCCESS && resp.nt_status == STATUS_SUCCESS) {
            return STATUS_SUCCESS;
        }
    }

    if (real_NtClose)
        return real_NtClose(Handle);

    return STATUS_INVALID_HANDLE;
}

NTSTATUS ms_hook_nt_device_io_control_file(HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext,
                                           PIO_STATUS_BLOCK IoStatusBlock, ULONG IoControlCode, PVOID InputBuffer,
                                           ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength) {
    if (g_ctx.ipc_connected) {
        MS_IPC_NT_DEVICE_IO_CONTROL_REQ req = {
            .handle = (UINT64)(ULONG_PTR)FileHandle,
            .io_control_code = IoControlCode,
            .input_buffer_length = InputBufferLength,
            .output_buffer_length = OutputBufferLength,
        };

        UINT32 total_req = sizeof(req) + InputBufferLength;
        UINT8* req_buf = (UINT8*)HeapAlloc(GetProcessHeap(), 0, total_req);
        if (req_buf) {
            memcpy(req_buf, &req, sizeof(req));
            if (InputBuffer && InputBufferLength > 0)
                memcpy(req_buf + sizeof(req), InputBuffer, InputBufferLength);

            UINT32 total_resp = sizeof(MS_IPC_NT_DEVICE_IO_CONTROL_RESP) + OutputBufferLength;
            UINT8* resp_buf = (UINT8*)HeapAlloc(GetProcessHeap(), 0, total_resp);
            if (resp_buf) {
                NTSTATUS status = ms_ipc_transact(MS_OP_NT_DEVICE_IO_CONTROL, req_buf, total_req, resp_buf, total_resp);

                if (status == STATUS_SUCCESS) {
                    MS_IPC_NT_DEVICE_IO_CONTROL_RESP* dev_resp = (MS_IPC_NT_DEVICE_IO_CONTROL_RESP*)resp_buf;
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
        return real_NtDeviceIoControlFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, IoControlCode,
                                          InputBuffer, InputBufferLength, OutputBuffer, OutputBufferLength);

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
