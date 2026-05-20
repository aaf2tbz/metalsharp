/*
 * MetalSharp mscompatdb hook contract for Wine ntdll.
 *
 * This file is intended to be compiled into MetalSharp's patched Wine ntdll,
 * not into the standalone PE/unixlib shim set built by src/wine/Makefile.
 */

#include "metalsharp/MscompatdbHookContract.h"

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#if __has_attribute(visibility)
#define MS_EXPORT __attribute__((visibility("default")))
#else
#define MS_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern char KeServiceDescriptorTable[];
extern void __wine_syscall_dispatcher(void);
extern void KeAddSystemServiceTable(void);
extern void NtCreateUserProcess(void);
extern void NtCreateFile(void);

static MetalSharpMscompatdbHookContract metalsharp_mscompatdb_hook_contract = {
    METALSHARP_MSCOMPATDB_HOOK_CONTRACT_VERSION,
    sizeof(MetalSharpMscompatdbHookContract),
    (void *)KeServiceDescriptorTable,
    (void *)&KeAddSystemServiceTable,
    (void *)&__wine_syscall_dispatcher,
    (void *)&NtCreateUserProcess,
    (void *)&NtCreateFile,
};

MS_EXPORT uint32_t MetalSharpGetMscompatdbHookContractVersion(void)
{
    return METALSHARP_MSCOMPATDB_HOOK_CONTRACT_VERSION;
}

MS_EXPORT const MetalSharpMscompatdbHookContract *MetalSharpGetMscompatdbHookContract(void)
{
    return &metalsharp_mscompatdb_hook_contract;
}

#ifdef __cplusplus
}
#endif
