// Decompiled from GPTK: dxgi.dll

/* __wine_spec_relay_entry_point_1 at 0x18000100c */

void __wine_spec_relay_entry_point_1(void)

{
  (*DAT_180003008)(&DAT_180003000,0x50000);
  return;
}



/* __wine_spec_relay_entry_point_2 at 0x180001030 */

void __wine_spec_relay_entry_point_2(void)

{
  (*DAT_180003008)(&DAT_180003000,0x50001);
  return;
}



/* __wine_spec_relay_entry_point_3 at 0x180001054 */

void __wine_spec_relay_entry_point_3(void)

{
  (*DAT_180003008)(&DAT_180003000,0x40002);
  return;
}



/* __wine_spec_relay_entry_point_4 at 0x18000107c */

void __wine_spec_relay_entry_point_4
               (undefined8 param_1,undefined8 param_2,undefined8 param_3,undefined8 param_4)

{
  (*DAT_180003008)(&DAT_180003000,3,param_3,param_4,param_4);
  return;
}



/* __wine_spec_relay_entry_point_5 at 0x1800010a8 */

void __wine_spec_relay_entry_point_5(void)

{
  (*DAT_180003008)(&DAT_180003000,0x50004);
  return;
}



/* __wine_spec_relay_entry_point_6 at 0x1800010cc */

void __wine_spec_relay_entry_point_6(void)

{
  (*DAT_180003008)(&DAT_180003000,0x40005);
  return;
}



/* __wine_spec_relay_entry_point_7 at 0x1800010f4 */

void __wine_spec_relay_entry_point_7(void)

{
  (*DAT_180003008)(&DAT_180003000,0x70006);
  return;
}



/* DllMain at 0x180001110 */

BOOL DllMain(HINSTANCE hinstDLL,DWORD fdwReason,LPVOID lpvReserved)

{
  if (fdwReason == 0) {
    if (DllMain::gdi32 != (HMODULE)0x0) {
      FreeLibrary((HMODULE)DllMain::gdi32);
    }
  }
  else if (fdwReason == 1) {
    DisableThreadLibraryCalls((HMODULE)hinstDLL);
    EnumDisplayMonitors((HDC)0x0,(LPCRECT)0x0,MonitorEnumProc,0);
    NtQueryVirtualMemory(0xffffffffffffffff,hinstDLL,1000,&shared_handle,8,0);
    LoadLibraryA("dxgi.dll");
    DllMain::gdi32 = (HMODULE)LoadLibraryA("gdi32");
    if (DllMain::gdi32 != (HMODULE)0x0) {
      gWin32Dispatch.D3DKMTEnumAdapters2 =
           (_func_NTSTATUS_LPVOID *)GetProcAddress((HMODULE)DllMain::gdi32,"D3DKMTEnumAdapters2");
    }
    gWin32Dispatch.image.dxgi.address = scratchSpace;
    gWin32Dispatch.image.dxgi.length = 0x1000000;
    __wine_unix_call(shared_handle,1,&gWin32Dispatch);
    __wine_unix_call(shared_handle,0,&gGFXTDispatch);
    gTrampolineThread =
         CreateThread((LPSECURITY_ATTRIBUTES)0x0,0,Thunk_Thread,gGFXTDispatch.GFXT_ThreadContext,0,
                      (LPDWORD)0x0);
  }
  return 1;
}



/* MonitorEnumProc at 0x180001250 */

int MonitorEnumProc(HMONITOR monitor,HDC hdc,LPRECT rect,void *user)

{
  return 1;
}



/* Thunk_Thread at 0x180001260 */

DWORD Thunk_Thread(void *c)

{
  (*gGFXTDispatch.GFXT_ThreadCallback)(c);
  return 0;
}



/* CreateDXGIFactory2 at 0x180001280 */

HRESULT CreateDXGIFactory2(UINT Flags,IID *riid,void **ppFactory)

{
  HRESULT HVar1;
  
                    /* 0x1280  3  CreateDXGIFactory2 */
                    /* WARNING: Could not recover jumptable at 0x000180001287. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  HVar1 = (*gGFXTDispatch.CreateDXGIFactory2)(Flags,riid,ppFactory);
  return HVar1;
}



/* CreateDXGIFactory1 at 0x180001290 */

HRESULT CreateDXGIFactory1(IID *riid,void **ppFactory)

{
  HRESULT HVar1;
  
                    /* 0x1290  2  CreateDXGIFactory1 */
                    /* WARNING: Could not recover jumptable at 0x00018000129f. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  HVar1 = (*gGFXTDispatch.CreateDXGIFactory2)(0,riid,ppFactory);
  return HVar1;
}



/* CreateDXGIFactory at 0x1800012b0 */

HRESULT CreateDXGIFactory(IID *riid,void **ppFactory)

{
  HRESULT HVar1;
  
                    /* 0x12b0  1  CreateDXGIFactory */
                    /* WARNING: Could not recover jumptable at 0x0001800012bf. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  HVar1 = (*gGFXTDispatch.CreateDXGIFactory2)(0,riid,ppFactory);
  return HVar1;
}



/* DXGIDeclareAdapterRemovalSupport at 0x1800012d0 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */

HRESULT DXGIDeclareAdapterRemovalSupport(void)

{
                    /* 0x12d0  7  DXGIDeclareAdapterRemovalSupport */
  return 0;
}



/* DXGID3D10CreateDevice at 0x1800012e0 */

HRESULT DXGID3D10CreateDevice
                  (HMODULE d3d11,void *factory,void *adapter,uint flags,
                  D3D_FEATURE_LEVEL *feature_levels,uint level_count,void **device)

{
                    /* 0x12e0  4  DXGID3D10CreateDevice */
  puts(
      "HRESULT DXGID3D10CreateDevice(HMODULE, void *, void *, unsigned int, const D3D_FEATURE_LEVEL *, unsigned int, void **)"
      );
  return -0x7fffbffb;
}



/* DXGID3D10RegisterLayers at 0x180001300 */

HRESULT DXGID3D10RegisterLayers(void *layers,uint layerCount)

{
                    /* 0x1300  5  DXGID3D10RegisterLayers */
  puts("HRESULT DXGID3D10RegisterLayers(void *, unsigned int)");
  return -0x7fffbffb;
}



/* DXGIGetDebugInterface1 at 0x180001320 */

HRESULT DXGIGetDebugInterface1(UINT flags,IID *iid,void **debug)

{
                    /* 0x1320  6  DXGIGetDebugInterface1 */
  puts("HRESULT DXGIGetDebugInterface1(UINT, const IID *const, void **)");
  return -0x7fffbffb;
}



/* GetProcessHeap at 0x180001340 */

/* WARNING: Unknown calling convention -- yet parameter storage is locked */

HANDLE GetProcessHeap(void)

{
  longlong unaff_GS_OFFSET;
  
  return *(HANDLE *)(*(longlong *)(*(longlong *)(unaff_GS_OFFSET + 0x30) + 0x60) + 0x30);
}



/* DllMainCRTStartup at 0x180001360 */

BOOL DllMainCRTStartup(HINSTANCE hinstDLL,DWORD fdwReason,LPVOID lpvReserved)

{
  if (fdwReason == 0) {
    if (DllMain::gdi32 != (HMODULE)0x0) {
      FreeLibrary((HMODULE)DllMain::gdi32);
    }
  }
  else if (fdwReason == 1) {
    DisableThreadLibraryCalls((HMODULE)hinstDLL);
    EnumDisplayMonitors((HDC)0x0,(LPCRECT)0x0,MonitorEnumProc,0);
    NtQueryVirtualMemory(0xffffffffffffffff,hinstDLL,1000,&shared_handle,8,0);
    LoadLibraryA("dxgi.dll");
    DllMain::gdi32 = (HMODULE)LoadLibraryA("gdi32");
    if (DllMain::gdi32 != (HMODULE)0x0) {
      gWin32Dispatch.D3DKMTEnumAdapters2 =
           (_func_NTSTATUS_LPVOID *)GetProcAddress((HMODULE)DllMain::gdi32,"D3DKMTEnumAdapters2");
    }
    gWin32Dispatch.image.dxgi.address = scratchSpace;
    gWin32Dispatch.image.dxgi.length = 0x1000000;
    __wine_unix_call(shared_handle,1,&gWin32Dispatch);
    __wine_unix_call(shared_handle,0,&gGFXTDispatch);
    gTrampolineThread =
         CreateThread((LPSECURITY_ATTRIBUTES)0x0,0,Thunk_Thread,gGFXTDispatch.GFXT_ThreadContext,0,
                      (LPDWORD)0x0);
  }
  return 1;
}



/* AdjustWindowRectEx at 0x180001370 */

BOOL __stdcall AdjustWindowRectEx(LPRECT lpRect,DWORD dwStyle,BOOL bMenu,DWORD dwExStyle)

{
  BOOL BVar1;
  
                    /* WARNING: Could not recover jumptable at 0x000180001370. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  BVar1 = AdjustWindowRectEx(lpRect,dwStyle,bMenu,dwExStyle);
  return BVar1;
}



/* ChangeDisplaySettingsExW at 0x180001380 */

LONG __stdcall
ChangeDisplaySettingsExW
          (LPCWSTR lpszDeviceName,DEVMODEW *lpDevMode,HWND hwnd,DWORD dwflags,LPVOID lParam)

{
  LONG LVar1;
  
                    /* WARNING: Could not recover jumptable at 0x000180001380. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  LVar1 = ChangeDisplaySettingsExW(lpszDeviceName,lpDevMode,hwnd,dwflags,lParam);
  return LVar1;
}



/* EnumDisplayMonitors at 0x180001390 */

BOOL __stdcall EnumDisplayMonitors(HDC hdc,LPCRECT lprcClip,MONITORENUMPROC lpfnEnum,LPARAM dwData)

{
  BOOL BVar1;
  
                    /* WARNING: Could not recover jumptable at 0x000180001390. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  BVar1 = EnumDisplayMonitors(hdc,lprcClip,lpfnEnum,dwData);
  return BVar1;
}



/* EnumDisplaySettingsW at 0x1800013a0 */

BOOL __stdcall EnumDisplaySettingsW(LPCWSTR lpszDeviceName,DWORD iModeNum,DEVMODEW *lpDevMode)

{
  BOOL BVar1;
  
                    /* WARNING: Could not recover jumptable at 0x0001800013a0. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  BVar1 = EnumDisplaySettingsW(lpszDeviceName,iModeNum,lpDevMode);
  return BVar1;
}



/* GetMonitorInfoW at 0x1800013b0 */

BOOL __stdcall GetMonitorInfoW(HMONITOR hMonitor,LPMONITORINFO lpmi)

{
  BOOL BVar1;
  
                    /* WARNING: Could not recover jumptable at 0x0001800013b0. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  BVar1 = GetMonitorInfoW(hMonitor,lpmi);
  return BVar1;
}



/* GetSystemMetrics at 0x1800013c0 */

int __stdcall GetSystemMetrics(int nIndex)

{
  int iVar1;
  
                    /* WARNING: Could not recover jumptable at 0x0001800013c0. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  iVar1 = GetSystemMetrics(nIndex);
  return iVar1;
}



/* GetWindowLongPtrW at 0x1800013d0 */

LONG_PTR __stdcall GetWindowLongPtrW(HWND hWnd,int nIndex)

{
  LONG_PTR LVar1;
  
                    /* WARNING: Could not recover jumptable at 0x0001800013d0. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  LVar1 = GetWindowLongPtrW(hWnd,nIndex);
  return LVar1;
}



/* GetWindowRect at 0x1800013e0 */

BOOL __stdcall GetWindowRect(HWND hWnd,LPRECT lpRect)

{
  BOOL BVar1;
  
                    /* WARNING: Could not recover jumptable at 0x0001800013e0. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  BVar1 = GetWindowRect(hWnd,lpRect);
  return BVar1;
}



/* MoveWindow at 0x1800013f0 */

BOOL __stdcall MoveWindow(HWND hWnd,int X,int Y,int nWidth,int nHeight,BOOL bRepaint)

{
  BOOL BVar1;
  
                    /* WARNING: Could not recover jumptable at 0x0001800013f0. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  BVar1 = MoveWindow(hWnd,X,Y,nWidth,nHeight,bRepaint);
  return BVar1;
}



/* SetWindowLongPtrW at 0x180001400 */

LONG_PTR __stdcall SetWindowLongPtrW(HWND hWnd,int nIndex,LONG_PTR dwNewLong)

{
  LONG_PTR LVar1;
  
                    /* WARNING: Could not recover jumptable at 0x000180001400. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  LVar1 = SetWindowLongPtrW(hWnd,nIndex,dwNewLong);
  return LVar1;
}



/* SetWindowPos at 0x180001410 */

BOOL __stdcall SetWindowPos(HWND hWnd,HWND hWndInsertAfter,int X,int Y,int cx,int cy,UINT uFlags)

{
  BOOL BVar1;
  
                    /* WARNING: Could not recover jumptable at 0x000180001410. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  BVar1 = SetWindowPos(hWnd,hWndInsertAfter,X,Y,cx,cy,uFlags);
  return BVar1;
}



/* ShowWindow at 0x180001420 */

BOOL __stdcall ShowWindow(HWND hWnd,int nCmdShow)

{
  BOOL BVar1;
  
                    /* WARNING: Could not recover jumptable at 0x000180001420. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  BVar1 = ShowWindow(hWnd,nCmdShow);
  return BVar1;
}



/* puts at 0x180001430 */

int __cdecl puts(char *_Str)

{
  int iVar1;
  
                    /* WARNING: Could not recover jumptable at 0x000180001430. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  iVar1 = puts(_Str);
  return iVar1;
}



/* CloseHandle at 0x180001440 */

BOOL __stdcall CloseHandle(HANDLE hObject)

{
  BOOL BVar1;
  
                    /* WARNING: Could not recover jumptable at 0x000180001440. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  BVar1 = CloseHandle(hObject);
  return BVar1;
}



/* CreateEventExA at 0x180001450 */

HANDLE __stdcall
CreateEventExA(LPSECURITY_ATTRIBUTES lpEventAttributes,LPCSTR lpName,DWORD dwFlags,
              DWORD dwDesiredAccess)

{
  HANDLE pvVar1;
  
                    /* WARNING: Could not recover jumptable at 0x000180001450. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  pvVar1 = CreateEventExA(lpEventAttributes,lpName,dwFlags,dwDesiredAccess);
  return pvVar1;
}



/* CreateSemaphoreExA at 0x180001460 */

HANDLE __stdcall
CreateSemaphoreExA(LPSECURITY_ATTRIBUTES lpSemaphoreAttributes,LONG lInitialCount,LONG lMaximumCount
                  ,LPCSTR lpName,DWORD dwFlags,DWORD dwDesiredAccess)

{
  HANDLE pvVar1;
  
                    /* WARNING: Could not recover jumptable at 0x000180001460. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  pvVar1 = CreateSemaphoreExA(lpSemaphoreAttributes,lInitialCount,lMaximumCount,lpName,dwFlags,
                              dwDesiredAccess);
  return pvVar1;
}



/* CreateThread at 0x180001470 */

HANDLE __stdcall
CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes,SIZE_T dwStackSize,
            LPTHREAD_START_ROUTINE lpStartAddress,LPVOID lpParameter,DWORD dwCreationFlags,
            LPDWORD lpThreadId)

{
  HANDLE pvVar1;
  
                    /* WARNING: Could not recover jumptable at 0x000180001470. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  pvVar1 = CreateThread(lpThreadAttributes,dwStackSize,lpStartAddress,lpParameter,dwCreationFlags,
                        lpThreadId);
  return pvVar1;
}



/* DuplicateHandle at 0x180001490 */

BOOL __stdcall
DuplicateHandle(HANDLE hSourceProcessHandle,HANDLE hSourceHandle,HANDLE hTargetProcessHandle,
               LPHANDLE lpTargetHandle,DWORD dwDesiredAccess,BOOL bInheritHandle,DWORD dwOptions)

{
  BOOL BVar1;
  
                    /* WARNING: Could not recover jumptable at 0x000180001490. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  BVar1 = DuplicateHandle(hSourceProcessHandle,hSourceHandle,hTargetProcessHandle,lpTargetHandle,
                          dwDesiredAccess,bInheritHandle,dwOptions);
  return BVar1;
}



/* FreeLibrary at 0x1800014a0 */

BOOL __stdcall FreeLibrary(HMODULE hLibModule)

{
  BOOL BVar1;
  
                    /* WARNING: Could not recover jumptable at 0x0001800014a0. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  BVar1 = FreeLibrary(hLibModule);
  return BVar1;
}



/* GetModuleFileNameA at 0x1800014b0 */

DWORD __stdcall GetModuleFileNameA(HMODULE hModule,LPSTR lpFilename,DWORD nSize)

{
  DWORD DVar1;
  
                    /* WARNING: Could not recover jumptable at 0x0001800014b0. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  DVar1 = GetModuleFileNameA(hModule,lpFilename,nSize);
  return DVar1;
}



/* GetModuleHandleA at 0x1800014c0 */

HMODULE __stdcall GetModuleHandleA(LPCSTR lpModuleName)

{
  HMODULE pHVar1;
  
                    /* WARNING: Could not recover jumptable at 0x0001800014c0. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  pHVar1 = GetModuleHandleA(lpModuleName);
  return pHVar1;
}



/* GetProcAddress at 0x1800014d0 */

FARPROC __stdcall GetProcAddress(HMODULE hModule,LPCSTR lpProcName)

{
  FARPROC pFVar1;
  
                    /* WARNING: Could not recover jumptable at 0x0001800014d0. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  pFVar1 = GetProcAddress(hModule,lpProcName);
  return pFVar1;
}



/* GetSystemDirectoryW at 0x1800014e0 */

UINT __stdcall GetSystemDirectoryW(LPWSTR lpBuffer,UINT uSize)

{
  UINT UVar1;
  
                    /* WARNING: Could not recover jumptable at 0x0001800014e0. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  UVar1 = GetSystemDirectoryW(lpBuffer,uSize);
  return UVar1;
}



/* HeapFree at 0x1800014f0 */

BOOL __stdcall HeapFree(HANDLE hHeap,DWORD dwFlags,LPVOID lpMem)

{
  BOOL BVar1;
  
                    /* WARNING: Could not recover jumptable at 0x0001800014f0. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  BVar1 = HeapFree(hHeap,dwFlags,lpMem);
  return BVar1;
}



/* LoadLibraryA at 0x180001500 */

HMODULE __stdcall LoadLibraryA(LPCSTR lpLibFileName)

{
  HMODULE pHVar1;
  
                    /* WARNING: Could not recover jumptable at 0x000180001500. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  pHVar1 = LoadLibraryA(lpLibFileName);
  return pHVar1;
}



/* LoadLibraryExA at 0x180001510 */

HMODULE __stdcall LoadLibraryExA(LPCSTR lpLibFileName,HANDLE hFile,DWORD dwFlags)

{
  HMODULE pHVar1;
  
                    /* WARNING: Could not recover jumptable at 0x000180001510. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  pHVar1 = LoadLibraryExA(lpLibFileName,hFile,dwFlags);
  return pHVar1;
}



/* PulseEvent at 0x180001520 */

BOOL __stdcall PulseEvent(HANDLE hEvent)

{
  BOOL BVar1;
  
                    /* WARNING: Could not recover jumptable at 0x000180001520. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  BVar1 = PulseEvent(hEvent);
  return BVar1;
}



/* ReleaseSemaphore at 0x180001530 */

BOOL __stdcall ReleaseSemaphore(HANDLE hSemaphore,LONG lReleaseCount,LPLONG lpPreviousCount)

{
  BOOL BVar1;
  
                    /* WARNING: Could not recover jumptable at 0x000180001530. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  BVar1 = ReleaseSemaphore(hSemaphore,lReleaseCount,lpPreviousCount);
  return BVar1;
}



/* ResetEvent at 0x180001540 */

BOOL __stdcall ResetEvent(HANDLE hEvent)

{
  BOOL BVar1;
  
                    /* WARNING: Could not recover jumptable at 0x000180001540. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  BVar1 = ResetEvent(hEvent);
  return BVar1;
}



/* SetEvent at 0x180001550 */

BOOL __stdcall SetEvent(HANDLE hEvent)

{
  BOOL BVar1;
  
                    /* WARNING: Could not recover jumptable at 0x000180001550. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  BVar1 = SetEvent(hEvent);
  return BVar1;
}



/* VirtualAlloc at 0x180001560 */

LPVOID __stdcall VirtualAlloc(LPVOID lpAddress,SIZE_T dwSize,DWORD flAllocationType,DWORD flProtect)

{
  LPVOID pvVar1;
  
                    /* WARNING: Could not recover jumptable at 0x000180001560. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  pvVar1 = VirtualAlloc(lpAddress,dwSize,flAllocationType,flProtect);
  return pvVar1;
}



/* VirtualFree at 0x180001570 */

BOOL __stdcall VirtualFree(LPVOID lpAddress,SIZE_T dwSize,DWORD dwFreeType)

{
  BOOL BVar1;
  
                    /* WARNING: Could not recover jumptable at 0x000180001570. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  BVar1 = VirtualFree(lpAddress,dwSize,dwFreeType);
  return BVar1;
}



/* VirtualProtect at 0x180001580 */

BOOL __stdcall
VirtualProtect(LPVOID lpAddress,SIZE_T dwSize,DWORD flNewProtect,PDWORD lpflOldProtect)

{
  BOOL BVar1;
  
                    /* WARNING: Could not recover jumptable at 0x000180001580. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  BVar1 = VirtualProtect(lpAddress,dwSize,flNewProtect,lpflOldProtect);
  return BVar1;
}



/* WaitForSingleObject at 0x180001590 */

DWORD __stdcall WaitForSingleObject(HANDLE hHandle,DWORD dwMilliseconds)

{
  DWORD DVar1;
  
                    /* WARNING: Could not recover jumptable at 0x000180001590. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  DVar1 = WaitForSingleObject(hHandle,dwMilliseconds);
  return DVar1;
}



/* RegCloseKey at 0x1800015a0 */

LSTATUS __stdcall RegCloseKey(HKEY hKey)

{
  LSTATUS LVar1;
  
                    /* WARNING: Could not recover jumptable at 0x0001800015a0. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  LVar1 = RegCloseKey(hKey);
  return LVar1;
}



/* RegCreateKeyExA at 0x1800015b0 */

LSTATUS __stdcall
RegCreateKeyExA(HKEY hKey,LPCSTR lpSubKey,DWORD Reserved,LPSTR lpClass,DWORD dwOptions,
               REGSAM samDesired,LPSECURITY_ATTRIBUTES lpSecurityAttributes,PHKEY phkResult,
               LPDWORD lpdwDisposition)

{
  LSTATUS LVar1;
  
                    /* WARNING: Could not recover jumptable at 0x0001800015b0. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  LVar1 = RegCreateKeyExA(hKey,lpSubKey,Reserved,lpClass,dwOptions,samDesired,lpSecurityAttributes,
                          phkResult,lpdwDisposition);
  return LVar1;
}



/* RegDeleteKeyValueA at 0x1800015c0 */

LSTATUS __stdcall RegDeleteKeyValueA(HKEY hKey,LPCSTR lpSubKey,LPCSTR lpValueName)

{
  LSTATUS LVar1;
  
                    /* WARNING: Could not recover jumptable at 0x0001800015c0. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  LVar1 = RegDeleteKeyValueA(hKey,lpSubKey,lpValueName);
  return LVar1;
}



/* RegOpenKeyExA at 0x1800015d0 */

LSTATUS __stdcall
RegOpenKeyExA(HKEY hKey,LPCSTR lpSubKey,DWORD ulOptions,REGSAM samDesired,PHKEY phkResult)

{
  LSTATUS LVar1;
  
                    /* WARNING: Could not recover jumptable at 0x0001800015d0. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  LVar1 = RegOpenKeyExA(hKey,lpSubKey,ulOptions,samDesired,phkResult);
  return LVar1;
}



/* RegQueryValueExA at 0x1800015e0 */

LSTATUS __stdcall
RegQueryValueExA(HKEY hKey,LPCSTR lpValueName,LPDWORD lpReserved,LPDWORD lpType,LPBYTE lpData,
                LPDWORD lpcbData)

{
  LSTATUS LVar1;
  
                    /* WARNING: Could not recover jumptable at 0x0001800015e0. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  LVar1 = RegQueryValueExA(hKey,lpValueName,lpReserved,lpType,lpData,lpcbData);
  return LVar1;
}



/* RegSetValueExA at 0x1800015f0 */

LSTATUS __stdcall
RegSetValueExA(HKEY hKey,LPCSTR lpValueName,DWORD Reserved,DWORD dwType,BYTE *lpData,DWORD cbData)

{
  LSTATUS LVar1;
  
                    /* WARNING: Could not recover jumptable at 0x0001800015f0. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  LVar1 = RegSetValueExA(hKey,lpValueName,Reserved,dwType,lpData,cbData);
  return LVar1;
}



