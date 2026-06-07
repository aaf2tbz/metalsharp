// Decompiled from GPTK: d3d12.dll

/* GetBehaviorValue at 0x180001000 */

void GetBehaviorValue(void)

{
    /* 0x1000  100  GetBehaviorValue */
    /* WARNING: Subroutine does not return */
    __wine_spec_unimplemented_stub(0x180006000, s_GetBehaviorValue_18000600a);
}

/* D3D12CoreCreateLayeredDevice at 0x180001018 */

void D3D12CoreCreateLayeredDevice(void)

{
    /* 0x1018  103  D3D12CoreCreateLayeredDevice */
    /* WARNING: Subroutine does not return */
    __wine_spec_unimplemented_stub(0x180006000, s_D3D12CoreCreateLayeredDevice_18000601b);
}

/* D3D12CoreGetLayeredDeviceSize at 0x180001030 */

void D3D12CoreGetLayeredDeviceSize(void)

{
    /* 0x1030  104  D3D12CoreGetLayeredDeviceSize */
    /* WARNING: Subroutine does not return */
    __wine_spec_unimplemented_stub(0x180006000, s_D3D12CoreGetLayeredDeviceSize_180006038);
}

/* D3D12CoreRegisterLayers at 0x180001048 */

void D3D12CoreRegisterLayers(void)

{
    /* 0x1048  105  D3D12CoreRegisterLayers */
    /* WARNING: Subroutine does not return */
    __wine_spec_unimplemented_stub(0x180006000, s_D3D12CoreRegisterLayers_180006056);
}

/* __wine_spec_relay_entry_point_101 at 0x180001068 */

void __wine_spec_relay_entry_point_101(undefined8 param_1, undefined8 param_2, undefined8 param_3, undefined8 param_4)

{
    (*DAT_180003008)(&DAT_180003000, 1, param_3, param_4, param_4);
    return;
}

/* __wine_spec_relay_entry_point_102 at 0x180001094 */

void __wine_spec_relay_entry_point_102(void)

{
    (*DAT_180003008)(&DAT_180003000, 0x20002);
    return;
}

/* __wine_spec_relay_entry_point_106 at 0x1800010b8 */

void __wine_spec_relay_entry_point_106(undefined8 param_1, undefined8 param_2, undefined8 param_3, undefined8 param_4)

{
    (*DAT_180003008)(&DAT_180003000, 6, param_3, param_4, param_4);
    return;
}

/* __wine_spec_relay_entry_point_107 at 0x1800010e4 */

void __wine_spec_relay_entry_point_107(undefined8 param_1, undefined8 param_2, undefined8 param_3, undefined8 param_4)

{
    (*DAT_180003008)(&DAT_180003000, 7, param_3, param_4, param_4);
    return;
}

/* __wine_spec_relay_entry_point_108 at 0x180001110 */

void __wine_spec_relay_entry_point_108(undefined8 param_1, undefined8 param_2, undefined8 param_3, undefined8 param_4)

{
    (*DAT_180003008)(&DAT_180003000, 8, param_3, param_4, param_4);
    return;
}

/* __wine_spec_relay_entry_point_109 at 0x18000113c */

void __wine_spec_relay_entry_point_109(undefined8 param_1, undefined8 param_2, undefined8 param_3, undefined8 param_4)

{
    (*DAT_180003008)(&DAT_180003000, 9, param_3, param_4, param_4);
    return;
}

/* __wine_spec_relay_entry_point_110 at 0x180001168 */

void __wine_spec_relay_entry_point_110(void)

{
    (*DAT_180003008)(&DAT_180003000, 0x1000a);
    return;
}

/* DllMain at 0x180001190 */

/* WARNING: Enum "D3D12_HEAP_FLAGS": Some values do not have unique names */
/* WARNING: Enum "D3D12_RESOURCE_STATES": Some values do not have unique names */
/* WARNING: Enum "D3D_PRIMITIVE_TOPOLOGY": Some values do not have unique names */

BOOL DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)

{
    if (fdwReason == 1) {
        EnumDisplayMonitors((HDC)0x0, (LPCRECT)0x0, MonitorEnumProc, 0);
        NtQueryVirtualMemory(0xffffffffffffffff, hinstDLL, 1000, &shared_handle, 8, 0);
        DisableThreadLibraryCalls((HMODULE)hinstDLL);
        LoadLibraryA("d3d12.dll");
        LoadLibraryA("dxgi.dll");
        __wine_unix_call(shared_handle, 0, &gGFXTDispatch);
    }
    return 1;
}

/* MonitorEnumProc at 0x180001230 */

int MonitorEnumProc(HMONITOR monitor, HDC hdc, LPRECT rect, void* user)

{
    return 1;
}

/* D3D12GetDebugInterface at 0x180001240 */

HRESULT D3D12GetDebugInterface(IID* iid, void** debug)

{
    /* 0x1240  102  D3D12GetDebugInterface */
    return -0x7fffbffb;
}

/* D3D12CreateDevice at 0x180001250 */

/* WARNING: Enum "D3D12_HEAP_FLAGS": Some values do not have unique names */
/* WARNING: Enum "D3D12_RESOURCE_STATES": Some values do not have unique names */
/* WARNING: Enum "D3D_PRIMITIVE_TOPOLOGY": Some values do not have unique names */

HRESULT D3D12CreateDevice(IUnknown* adapter, D3D_FEATURE_LEVEL featureLevel, IID* riid, void** ppDevice)

{
    HRESULT HVar1;

    /* 0x1250  101  D3D12CreateDevice */
    /* WARNING: Could not recover jumptable at 0x000180001257. Too many branches */
    /* WARNING: Treating indirect jump as call */
    HVar1 = (*gGFXTDispatch.D3D12CreateDevice)(adapter, featureLevel, riid, ppDevice);
    return HVar1;
}

/* D3D12CreateRootSignatureDeserializer at 0x180001260 */

/* WARNING: Enum "D3D12_HEAP_FLAGS": Some values do not have unique names */
/* WARNING: Enum "D3D12_RESOURCE_STATES": Some values do not have unique names */
/* WARNING: Enum "D3D_PRIMITIVE_TOPOLOGY": Some values do not have unique names */

HRESULT D3D12CreateRootSignatureDeserializer(void* data, SIZE_T data_size, IID* iid, void** deserializer)

{
    HRESULT HVar1;

    /* 0x1260  106  D3D12CreateRootSignatureDeserializer */
    /* WARNING: Could not recover jumptable at 0x000180001267. Too many branches */
    /* WARNING: Treating indirect jump as call */
    HVar1 = (*gGFXTDispatch.D3D12CreateRootSignatureDeserializer)(data, data_size, iid, deserializer);
    return HVar1;
}

/* D3D12CreateVersionedRootSignatureDeserializer at 0x180001270 */

/* WARNING: Enum "D3D12_HEAP_FLAGS": Some values do not have unique names */
/* WARNING: Enum "D3D12_RESOURCE_STATES": Some values do not have unique names */
/* WARNING: Enum "D3D_PRIMITIVE_TOPOLOGY": Some values do not have unique names */

HRESULT D3D12CreateVersionedRootSignatureDeserializer(void* data, SIZE_T data_size, IID* iid, void** deserializer)

{
    HRESULT HVar1;

    /* 0x1270  107  D3D12CreateVersionedRootSignatureDeserializer */
    /* WARNING: Could not recover jumptable at 0x000180001277. Too many branches */
    /* WARNING: Treating indirect jump as call */
    HVar1 = (*gGFXTDispatch.D3D12CreateVersionedRootSignatureDeserializer)(data, data_size, iid, deserializer);
    return HVar1;
}

/* D3D12SerializeRootSignature at 0x180001280 */

/* WARNING: Enum "D3D_ROOT_SIGNATURE_VERSION": Some values do not have unique names */
/* WARNING: Enum "D3D12_HEAP_FLAGS": Some values do not have unique names */
/* WARNING: Enum "D3D12_RESOURCE_STATES": Some values do not have unique names */
/* WARNING: Enum "D3D_PRIMITIVE_TOPOLOGY": Some values do not have unique names */

HRESULT D3D12SerializeRootSignature(D3D12_ROOT_SIGNATURE_DESC* root_signature_desc, D3D_ROOT_SIGNATURE_VERSION version,
                                    ID3DBlob** blob, ID3DBlob** error_blob)

{
    HRESULT HVar1;

    /* 0x1280  109  D3D12SerializeRootSignature */
    /* WARNING: Could not recover jumptable at 0x000180001287. Too many branches */
    /* WARNING: Treating indirect jump as call */
    HVar1 = (*gGFXTDispatch.D3D12SerializeRootSignature)(root_signature_desc, version, blob, error_blob);
    return HVar1;
}

/* D3D12SerializeVersionedRootSignature at 0x180001290 */

/* WARNING: Enum "D3D_ROOT_SIGNATURE_VERSION": Some values do not have unique names */
/* WARNING: Enum "D3D12_HEAP_FLAGS": Some values do not have unique names */
/* WARNING: Enum "D3D12_RESOURCE_STATES": Some values do not have unique names */
/* WARNING: Enum "D3D_PRIMITIVE_TOPOLOGY": Some values do not have unique names */

HRESULT D3D12SerializeVersionedRootSignature(D3D12_VERSIONED_ROOT_SIGNATURE_DESC* desc, ID3DBlob** blob,
                                             ID3DBlob** error_blob)

{
    HRESULT HVar1;

    /* 0x1290  110  D3D12SerializeVersionedRootSignature */
    /* WARNING: Could not recover jumptable at 0x000180001297. Too many branches */
    /* WARNING: Treating indirect jump as call */
    HVar1 = (*gGFXTDispatch.D3D12SerializeVersionedRootSignature)(desc, blob, error_blob);
    return HVar1;
}

/* D3D12EnableExperimentalFeatures at 0x1800012a0 */

HRESULT D3D12EnableExperimentalFeatures(UINT feature_count, IID* iids, void* configurations, UINT* configurations_sizes)

{
    /* 0x12a0  108  D3D12EnableExperimentalFeatures */
    return 0;
}

/* __wine_spec_unimplemented_stub at 0x1800012b0 */

void __wine_spec_unimplemented_stub(ULONG_PTR param_1, undefined8 param_2)

{
    ULONG_PTR local_28;
    undefined8 local_20;

    local_28 = param_1;
    local_20 = param_2;
    do {
        RaiseException(0x80000100, 1, 2, &local_28);
    } while (true);
}

/* DllMainCRTStartup at 0x1800012f0 */

/* WARNING: Enum "D3D12_HEAP_FLAGS": Some values do not have unique names */
/* WARNING: Enum "D3D12_RESOURCE_STATES": Some values do not have unique names */
/* WARNING: Enum "D3D_PRIMITIVE_TOPOLOGY": Some values do not have unique names */

BOOL DllMainCRTStartup(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)

{
    if (fdwReason == 1) {
        EnumDisplayMonitors((HDC)0x0, (LPCRECT)0x0, MonitorEnumProc, 0);
        NtQueryVirtualMemory(0xffffffffffffffff, hinstDLL, 1000, &shared_handle, 8, 0);
        DisableThreadLibraryCalls((HMODULE)hinstDLL);
        LoadLibraryA("d3d12.dll");
        LoadLibraryA("dxgi.dll");
        __wine_unix_call(shared_handle, 0, &gGFXTDispatch);
    }
    return 1;
}
