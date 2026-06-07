// Decompiled from GPTK: d3d11.dll

/* __wine_spec_relay_entry_point_1 at 0x18000100c */

void __wine_spec_relay_entry_point_1
               (undefined8 param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4)

{
  (*DAT_180003008)(&DAT_180003000,0x20000,param_3,param_4,param_4);
  return;
}



/* __wine_spec_relay_entry_point_2 at 0x180001038 */

void __wine_spec_relay_entry_point_2
               (undefined8 param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4)

{
  (*DAT_180003008)(&DAT_180003000,1,param_3,param_4,param_4);
  return;
}



/* __wine_spec_relay_entry_point_3 at 0x180001064 */

void __wine_spec_relay_entry_point_3
               (undefined8 param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4)

{
  (*DAT_180003008)(&DAT_180003000,0x30002,param_3,param_4,param_4);
  return;
}



/* DllMain at 0x180001090 */

/* WARNING: Enum "D3D_SRV_DIMENSION": Some values do not have unique names */
/* WARNING: Enum "D3D_PRIMITIVE_TOPOLOGY": Some values do not have unique names */

BOOL DllMain(HINSTANCE hinstDLL,DWORD fdwReason,LPVOID lpvReserved)

{
  int iVar1;
  BOOL BVar2;
  longlong lVar3;
  void **ppvVar4;
  DWORD local_c;
  
                    /* Unresolved local var: DWORD oldProt@[???]
                       Unresolved local var: uint64_t table_offset@[???]
                       Unresolved local var: HRESULT status@[???] */
  if (fdwReason == 1) {
    NtQueryVirtualMemory(0xffffffffffffffff,hinstDLL,1000,&shared_handle,8,0);
    DisableThreadLibraryCalls((HMODULE)hinstDLL);
    LoadLibraryA("d3d11.dll");
    LoadLibraryA("dxgi.dll");
    iVar1 = __wine_unix_call(shared_handle,0,&gGFXTDispatch);
    if (iVar1 != 0) {
      return 0;
    }
    lVar3 = 0x18;
    while( true ) {
      *(undefined2 *)(lVar3 + 0x180004fe8) = 0x25ff;
      *(undefined4 *)(lVar3 + 0x180004fea) = 2;
      *(undefined2 *)(lVar3 + 0x180004fee) = 0;
      *(undefined8 *)(lVar3 + 0x180004ff0) = 0xbad;
      if (lVar3 == 0x958) break;
      *(undefined2 *)(lVar3 + 0x180004ff8) = 0x25ff;
      *(undefined4 *)(lVar3 + 0x180004ffa) = 2;
      *(undefined2 *)(lVar3 + 0x180004ffe) = 0;
      *(undefined8 *)((longlong)&jump_table[0].instruction + lVar3) = 0xbad;
      lVar3 = lVar3 + 0x20;
    }
                    /* Unresolved local var: uint64_t * vtable@[???] */
    ppvVar4 = (*gGFXTDispatch.D3D11DeferredDeviceContext_GetVTable)();
                    /* Unresolved local var: uint64_t i@[???] */
    ppvVar4 = ppvVar4 + 3;
    lVar3 = 0;
    while( true ) {
      *(void **)((longlong)&jump_table[0].address + lVar3) = ppvVar4[-3];
      ppvVar4[-3] = (void *)((longlong)&jump_table[0].instruction + lVar3);
      if (lVar3 == 0x940) break;
      *(void **)((longlong)&jump_table[1].address + lVar3) = ppvVar4[-2];
      ppvVar4[-2] = (void *)((longlong)&jump_table[1].instruction + lVar3);
      *(void **)((longlong)&jump_table[2].address + lVar3) = ppvVar4[-1];
      ppvVar4[-1] = (void *)((longlong)&jump_table[2].instruction + lVar3);
      *(void **)((longlong)&jump_table[3].address + lVar3) = *ppvVar4;
      *ppvVar4 = (void *)((longlong)&jump_table[3].instruction + lVar3);
      ppvVar4 = ppvVar4 + 4;
      lVar3 = lVar3 + 0x40;
    }
    BVar2 = VirtualProtect(jump_table,0x950,0x20,&local_c);
    if (BVar2 == 0) {
      return 0;
    }
  }
  return 1;
}



/* D3D11CreateDevice at 0x180001210 */

/* WARNING: Enum "D3D_SRV_DIMENSION": Some values do not have unique names */
/* WARNING: Enum "D3D_PRIMITIVE_TOPOLOGY": Some values do not have unique names */

HRESULT D3D11CreateDevice(IDXGIAdapter *adapter,D3D_DRIVER_TYPE driverType,HMODULE software,
                         UINT Flags,D3D_FEATURE_LEVEL *features,UINT numFeatures,UINT SDKVersion,
                         ID3D11Device **ppDevice,D3D_FEATURE_LEVEL *outFeatureLevel,
                         ID3D11DeviceContext **immediateContext)

{
  HRESULT HVar1;
  
                    /* 0x1210  1  D3D11CreateDevice */
                    /* WARNING: Could not recover jumptable at 0x000180001217. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  HVar1 = (*gGFXTDispatch.D3D11CreateDevice)
                    (adapter,driverType,software,Flags,features,numFeatures,SDKVersion,ppDevice,
                     outFeatureLevel,immediateContext);
  return HVar1;
}



/* D3D11CreateDeviceAndSwapChain at 0x180001220 */

/* WARNING: Enum "D3D_SRV_DIMENSION": Some values do not have unique names */
/* WARNING: Enum "D3D_PRIMITIVE_TOPOLOGY": Some values do not have unique names */

HRESULT D3D11CreateDeviceAndSwapChain
                  (IDXGIAdapter *pAdapter,D3D_DRIVER_TYPE DriverType,HMODULE Software,UINT Flags,
                  D3D_FEATURE_LEVEL *pFeatureLevels,UINT FeatureLevels,UINT SDKVersion,
                  DXGI_SWAP_CHAIN_DESC *pSwapChainDesc,IDXGISwapChain **ppSwapChain,
                  ID3D11Device **ppDevice,D3D_FEATURE_LEVEL *pFeatureLevel,
                  ID3D11DeviceContext **ppImmediateContext)

{
  HRESULT HVar1;
  
                    /* 0x1220  2  D3D11CreateDeviceAndSwapChain */
                    /* WARNING: Could not recover jumptable at 0x000180001227. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  HVar1 = (*gGFXTDispatch.D3D11CreateDeviceAndSwapChain)
                    (pAdapter,DriverType,Software,Flags,pFeatureLevels,FeatureLevels,SDKVersion,
                     pSwapChainDesc,ppSwapChain,ppDevice,pFeatureLevel,ppImmediateContext);
  return HVar1;
}



/* D3D11On12CreateDevice at 0x180001230 */

HRESULT D3D11On12CreateDevice
                  (IUnknown *pDevice,UINT Flags,D3D_FEATURE_LEVEL *pFeatureLevels,
                  IUnknown ***ppCommandQueues,UINT NumQueues,UINT NodeMask,IUnknown *ppDevice,
                  IUnknown **ppImmediateContext,D3D_FEATURE_LEVEL *pChosenFeatureLevel)

{
                    /* 0x1230  3  D3D11On12CreateDevice */
  return -0x7785fffc;
}



/* DllMainCRTStartup at 0x180001240 */

/* WARNING: Enum "D3D_SRV_DIMENSION": Some values do not have unique names */
/* WARNING: Enum "D3D_PRIMITIVE_TOPOLOGY": Some values do not have unique names */

BOOL DllMainCRTStartup(HINSTANCE hinstDLL,DWORD fdwReason,LPVOID lpvReserved)

{
  int iVar1;
  BOOL BVar2;
  longlong lVar3;
  void **ppvVar4;
  DWORD DStack_c;
  
  if (fdwReason == 1) {
    NtQueryVirtualMemory(0xffffffffffffffff,hinstDLL,1000,&shared_handle,8,0);
    DisableThreadLibraryCalls((HMODULE)hinstDLL);
    LoadLibraryA("d3d11.dll");
    LoadLibraryA("dxgi.dll");
    iVar1 = __wine_unix_call(shared_handle,0,&gGFXTDispatch);
    if (iVar1 != 0) {
      return 0;
    }
    lVar3 = 0x18;
    while( true ) {
      *(undefined2 *)(lVar3 + 0x180004fe8) = 0x25ff;
      *(undefined4 *)(lVar3 + 0x180004fea) = 2;
      *(undefined2 *)(lVar3 + 0x180004fee) = 0;
      *(undefined8 *)(lVar3 + 0x180004ff0) = 0xbad;
      if (lVar3 == 0x958) break;
      *(undefined2 *)(lVar3 + 0x180004ff8) = 0x25ff;
      *(undefined4 *)(lVar3 + 0x180004ffa) = 2;
      *(undefined2 *)(lVar3 + 0x180004ffe) = 0;
      *(undefined8 *)((longlong)&jump_table[0].instruction + lVar3) = 0xbad;
      lVar3 = lVar3 + 0x20;
    }
    ppvVar4 = (*gGFXTDispatch.D3D11DeferredDeviceContext_GetVTable)();
    ppvVar4 = ppvVar4 + 3;
    lVar3 = 0;
    while( true ) {
      *(void **)((longlong)&jump_table[0].address + lVar3) = ppvVar4[-3];
      ppvVar4[-3] = (void *)((longlong)&jump_table[0].instruction + lVar3);
      if (lVar3 == 0x940) break;
      *(void **)((longlong)&jump_table[1].address + lVar3) = ppvVar4[-2];
      ppvVar4[-2] = (void *)((longlong)&jump_table[1].instruction + lVar3);
      *(void **)((longlong)&jump_table[2].address + lVar3) = ppvVar4[-1];
      ppvVar4[-1] = (void *)((longlong)&jump_table[2].instruction + lVar3);
      *(void **)((longlong)&jump_table[3].address + lVar3) = *ppvVar4;
      *ppvVar4 = (void *)((longlong)&jump_table[3].instruction + lVar3);
      ppvVar4 = ppvVar4 + 4;
      lVar3 = lVar3 + 0x40;
    }
    BVar2 = VirtualProtect(jump_table,0x950,0x20,&DStack_c);
    if (BVar2 == 0) {
      return 0;
    }
  }
  return 1;
}



