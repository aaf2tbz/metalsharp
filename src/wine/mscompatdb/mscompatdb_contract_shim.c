/*
 * MetalSharp mscompatdb Darwin shim.
 *
 * This is a clean, contract-aware replacement for the legacy private-symbol
 * probe used by the earlier local runtime. It is loaded as mscompatdb.so by
 * Wine's Unix ntdll and may also be mirrored as mscompatdb.dylib for dyld
 * inspection/signing on macOS.
 */

#include "metalsharp/MscompatdbHookContract.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef int32_t NTSTATUS;
typedef void* HANDLE;
typedef uint32_t ACCESS_MASK;
typedef uint32_t ULONG;
typedef uint64_t ULONG_PTR;

typedef struct _OBJECT_ATTRIBUTES OBJECT_ATTRIBUTES;
typedef struct _RTL_USER_PROCESS_PARAMETERS RTL_USER_PROCESS_PARAMETERS;
typedef struct _PS_CREATE_INFO PS_CREATE_INFO;
typedef struct _PS_ATTRIBUTE_LIST PS_ATTRIBUTE_LIST;

typedef NTSTATUS (*NtCreateUserProcessFn)(HANDLE* process_handle_ptr, HANDLE* thread_handle_ptr,
                                          ACCESS_MASK process_access, ACCESS_MASK thread_access,
                                          OBJECT_ATTRIBUTES* process_attr, OBJECT_ATTRIBUTES* thread_attr,
                                          ULONG process_flags, ULONG thread_flags, RTL_USER_PROCESS_PARAMETERS* params,
                                          PS_CREATE_INFO* info, PS_ATTRIBUTE_LIST* ps_attr);

typedef NTSTATUS (*NtCreateFileFn)(HANDLE* handle, ACCESS_MASK access, OBJECT_ATTRIBUTES* attr, void* io,
                                   void* alloc_size, ULONG attributes, ULONG sharing, ULONG disposition, ULONG options,
                                   void* ea_buffer, ULONG ea_length);

typedef struct MetalSharpServiceTable {
    ULONG_PTR* service_table;
    ULONG_PTR* counter_table;
    ULONG service_limit;
    unsigned char* argument_table;
} MetalSharpServiceTable;

static NtCreateUserProcessFn original_NtCreateUserProcess;
static NtCreateFileFn original_NtCreateFile;
static int patch_attempted;

typedef struct WritablePage {
    void* page;
    size_t size;
} WritablePage;

static void trace_line(const char* fmt, ...) {
    FILE* files[2] = {NULL, NULL};
    const char* home = getenv("HOME");
    char home_path[4096];
    va_list ap;

    files[0] = fopen("/tmp/mscompatdb_debug.log", "a");
    if (home && snprintf(home_path, sizeof(home_path), "%s/.metalsharp/mscompatdb_trace.log", home) > 0)
        files[1] = fopen(home_path, "a");

    for (size_t i = 0; i < 2; i++) {
        if (!files[i])
            continue;
        va_start(ap, fmt);
        fputs("mscompatdb:trace: ", files[i]);
        vfprintf(files[i], fmt, ap);
        fputc('\n', files[i]);
        va_end(ap);
        fclose(files[i]);
    }
}

static int make_slot_writable(void* slot, WritablePage* writable_page) {
    long page_size = sysconf(_SC_PAGESIZE);
    uintptr_t page;

    if (page_size <= 0)
        return 0;
    page = (uintptr_t)slot & ~((uintptr_t)page_size - 1);
    if (mprotect((void*)page, (size_t)page_size, PROT_READ | PROT_WRITE) == 0) {
        if (writable_page) {
            writable_page->page = (void*)page;
            writable_page->size = (size_t)page_size;
        }
        return 1;
    }
    trace_line("mprotect failed for service table slot %p", slot);
    return 0;
}

static void restore_slot_read_only(WritablePage* writable_page) {
    if (!writable_page || !writable_page->page || writable_page->size == 0)
        return;
    if (mprotect(writable_page->page, writable_page->size, PROT_READ) != 0)
        trace_line("mprotect restore failed for service table page %p", writable_page->page);
}

static long find_service_index(MetalSharpServiceTable* table, void* target) {
    if (!table || !table->service_table || !target)
        return -1;
    for (ULONG i = 0; i < table->service_limit; i++) {
        if ((void*)table->service_table[i] == target)
            return (long)i;
    }
    return -1;
}

static NTSTATUS hook_NtCreateUserProcess(HANDLE* process_handle_ptr, HANDLE* thread_handle_ptr,
                                         ACCESS_MASK process_access, ACCESS_MASK thread_access,
                                         OBJECT_ATTRIBUTES* process_attr, OBJECT_ATTRIBUTES* thread_attr,
                                         ULONG process_flags, ULONG thread_flags, RTL_USER_PROCESS_PARAMETERS* params,
                                         PS_CREATE_INFO* info, PS_ATTRIBUTE_LIST* ps_attr) {
    trace_line("hook_NtCreateUserProcess called params=%p info=%p attrs=%p", params, info, ps_attr);
    return original_NtCreateUserProcess(process_handle_ptr, thread_handle_ptr, process_access, thread_access,
                                        process_attr, thread_attr, process_flags, thread_flags, params, info, ps_attr);
}

static void patch_syscalls(void) {
    MetalSharpGetMscompatdbHookContractFn get_contract;
    const MetalSharpMscompatdbHookContract* contract;
    MetalSharpServiceTable* tables;
    WritablePage writable_page = {0};
    long create_process_index;
    long create_file_index;

    if (patch_attempted)
        return;
    patch_attempted = 1;

    trace_line("MetalSharp compatibility database v2.0");
    get_contract = (MetalSharpGetMscompatdbHookContractFn)dlsym(RTLD_DEFAULT, "MetalSharpGetMscompatdbHookContract");
    if (!get_contract) {
        trace_line("contract lookup failed: %s", dlerror());
        return;
    }

    contract = get_contract();
    if (!contract || contract->version != METALSHARP_MSCOMPATDB_HOOK_CONTRACT_VERSION ||
        contract->size < sizeof(*contract)) {
        trace_line("contract invalid version=%u size=%u", contract ? contract->version : 0,
                   contract ? contract->size : 0);
        return;
    }

    tables = (MetalSharpServiceTable*)contract->ke_service_descriptor_table;
    create_process_index = find_service_index(&tables[0], contract->nt_create_user_process);
    create_file_index = find_service_index(&tables[0], contract->nt_create_file);
    trace_line("contract ready process_index=%ld file_index=%ld limit=%u", create_process_index, create_file_index,
               tables ? tables[0].service_limit : 0);

    if (create_process_index < 0) {
        trace_line("NtCreateUserProcess not in syscall table");
        return;
    }

    original_NtCreateUserProcess = (NtCreateUserProcessFn)tables[0].service_table[create_process_index];
    if (!make_slot_writable(&tables[0].service_table[create_process_index], &writable_page))
        return;
    tables[0].service_table[create_process_index] = (ULONG_PTR)hook_NtCreateUserProcess;
    restore_slot_read_only(&writable_page);

    if (create_file_index >= 0)
        original_NtCreateFile = (NtCreateFileFn)tables[0].service_table[create_file_index];

    trace_line("patch_syscalls completed, hook installed");
}

__attribute__((constructor)) static void mscompatdb_init(void) {
    patch_syscalls();
}
