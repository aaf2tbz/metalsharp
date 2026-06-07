// Decompiled from GPTK: d3d10.dll

/* __wine_spec_relay_entry_point_1 at 0x18000100c */

void __wine_spec_relay_entry_point_1(undefined8 param_1, undefined8 param_2, undefined8 param_3, undefined8 param_4)

{
    (*DAT_180003008)(&DAT_180003000, 0, param_3, param_4, param_4);
    return;
}

/* __wine_spec_relay_entry_point_2 at 0x180001038 */

void __wine_spec_relay_entry_point_2(void)

{
    (*DAT_180003008)(&DAT_180003000, 0x40001);
    return;
}

/* DllMain at 0x180001060 */

BOOL DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)

{
    if (fdwReason == 1) {
        NtQueryVirtualMemory(0xffffffffffffffff, hinstDLL, 1000, &shared_handle, 8, 0);
        DisableThreadLibraryCalls((HMODULE)hinstDLL);
        LoadLibraryA("d3d10.dll");
        LoadLibraryA("dxgi.dll");
        __wine_unix_call(shared_handle, 0, &gGFXTDispatch);
    }
    return 1;
}

/* D3D10CreateDevice at 0x1800010e0 */

HRESULT D3D10CreateDevice(void* adapter, UINT driverType, HMODULE software, UINT flags, UINT sdkVersion,
                          void** ppDevice)

{
    /* 0x10e0  1  D3D10CreateDevice */
    puts("!!!!!!!D3D10CreateDevice Requested!!!!!!!!!");
    return -0x7fffbffb;
}

/* D3D10CreateBlob at 0x180001100 */

HRESULT D3D10CreateBlob(SIZE_T NumBytes, void** ppBuffer)

{
    HRESULT HVar1;

    /* 0x1100  2  D3D10CreateBlob */
    /* WARNING: Could not recover jumptable at 0x000180001107. Too many branches */
    /* WARNING: Treating indirect jump as call */
    HVar1 = (*gGFXTDispatch.D3D10CreateBlob)(NumBytes, ppBuffer);
    return HVar1;
}

/* DllMainCRTStartup at 0x180001110 */

BOOL DllMainCRTStartup(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)

{
    if (fdwReason == 1) {
        NtQueryVirtualMemory(0xffffffffffffffff, hinstDLL, 1000, &shared_handle, 8, 0);
        DisableThreadLibraryCalls((HMODULE)hinstDLL);
        LoadLibraryA("d3d10.dll");
        LoadLibraryA("dxgi.dll");
        __wine_unix_call(shared_handle, 0, &gGFXTDispatch);
    }
    return 1;
}

/* puts at 0x180001120 */

int __cdecl puts(char* _Str)

{
    int iVar1;

    /* WARNING: Could not recover jumptable at 0x000180001120. Too many branches */
    /* WARNING: Treating indirect jump as call */
    iVar1 = puts(_Str);
    return iVar1;
}
