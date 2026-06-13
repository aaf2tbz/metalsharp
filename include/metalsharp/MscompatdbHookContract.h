#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define METALSHARP_MSCOMPATDB_HOOK_CONTRACT_VERSION 1u

typedef struct MetalSharpMscompatdbHookContract {
    uint32_t version;
    uint32_t size;

    /*
     * Darwin/Wine ntdll-side pointers. MetalSharp Wine builds that expose this
     * contract let mscompatdb avoid scraping private Mach-O symbols.
     */
    void* ke_service_descriptor_table;
    void* ke_add_system_service_table;
    void* wine_syscall_dispatcher;
    void* nt_create_user_process;
    void* nt_create_file;
} MetalSharpMscompatdbHookContract;

typedef const MetalSharpMscompatdbHookContract* (*MetalSharpGetMscompatdbHookContractFn)(void);
typedef uint32_t (*MetalSharpGetMscompatdbHookContractVersionFn)(void);

const MetalSharpMscompatdbHookContract* MetalSharpGetMscompatdbHookContract(void);
uint32_t MetalSharpGetMscompatdbHookContractVersion(void);

#ifdef __cplusplus
}
#endif
