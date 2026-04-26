#pragma once

#include <metalsharp/Platform.h>

typedef UINT DXGI_FORMAT;

class ID3D11ClassInstance;
class ID3D11ClassLinkage;

class IDXGIObject : public IUnknown {
public:
    STDMETHOD(GetDevice)(const GUID& riid, void** ppDevice) PURE;
    STDMETHOD(GetPrivateData)(const GUID& guid, UINT* pDataSize, void* pData) PURE;
    STDMETHOD(SetPrivateData)(const GUID& guid, UINT DataSize, const void* pData) PURE;
    STDMETHOD(SetPrivateDataInterface)(const GUID& guid, const IUnknown* pData) PURE;
    STDMETHOD(GetParent)(const GUID& riid, void** ppParent) PURE;
};

class IDXGIAdapter;
class IDXGIOutput;
class IDXGISwapChain;

class MIDL_INTERFACE("770aae78-f26f-4dba-a829-253c83d1b387")
IDXGIFactory : public IUnknown {
public:
    STDMETHOD(EnumAdapters)(UINT Adapter, IDXGIAdapter** ppAdapter) PURE;
    STDMETHOD(MakeWindowAssociation)(HWND WindowHandle, UINT Flags) PURE;
    STDMETHOD(GetWindowAssociation)(HWND* pWindowHandle) PURE;
    STDMETHOD(CreateSwapChain)(IUnknown* pDevice, void* pDesc, IDXGISwapChain** ppSwapChain) PURE;
    STDMETHOD(CreateSoftwareAdapter)(HMODULE Module, IDXGIAdapter** ppAdapter) PURE;
};

class MIDL_INTERFACE("2411e7e1-12ac-4ccf-bd14-9798e8534dc0")
IDXGIFactory1 : public IDXGIFactory {
public:
    STDMETHOD(EnumAdapters1)(UINT Adapter, IDXGIAdapter** ppAdapter) PURE;
    STDMETHOD_(BOOL, IsCurrent)() PURE;
};

class MIDL_INTERFACE("790a45f7-0d42-4876-983a-0a55c2762d7f")
IDXGIFactory2 : public IDXGIFactory1 {
};

class MIDL_INTERFACE("3103a90d-34de-4ec3-988a-743f4c28c6d5")
IDXGIFactory3 : public IDXGIFactory2 {
};

class MIDL_INTERFACE("1bc6ea02-ef36-464f-bf0c-21ca39e5cd85")
IDXGIFactory4 : public IDXGIFactory3 {
};

typedef struct {
    DXGI_FORMAT Format;
    UINT ScanlineOrdering;
    UINT Scaling;
    UINT Width;
    UINT Height;
    UINT RefreshRateNumerator;
    UINT RefreshRateDenominator;
} DXGI_MODE_DESC;

class MIDL_INTERFACE("2411e7e1-12ac-4ccf-bd14-9798e8534dc0")
IDXGIAdapter : public IDXGIObject {
public:
    STDMETHOD(EnumOutputs)(UINT Output, IDXGIOutput** ppOutput) PURE;
    STDMETHOD(GetDesc)(void* pDesc) PURE;
    STDMETHOD(CheckInterfaceSupport)(const GUID& guid, UINT64* pUMDVersion) PURE;
};

class MIDL_INTERFACE("ae02eedb-c735-4690-8d52-5a8dc20213aa")
IDXGIOutput : public IDXGIObject {
public:
    STDMETHOD(GetDesc)(void* pDesc) PURE;
    STDMETHOD(GetDisplayModeList)(DXGI_FORMAT EnumFormat, UINT Flags, UINT* pNumModes, DXGI_MODE_DESC* pDesc) PURE;
    STDMETHOD(FindClosestMatchingMode)(const DXGI_MODE_DESC* pModeToMatch, DXGI_MODE_DESC* pClosestMatch, IUnknown* pConcernedDevice) PURE;
    STDMETHOD(WaitForVBlank)() PURE;
    STDMETHOD(TakeOwnership)(IUnknown* pDevice, BOOL Exclusive) PURE;
    STDMETHOD_(void, ReleaseOwnership)() PURE;
    STDMETHOD(GetGammaControlCapabilities)(void* pGammaCaps) PURE;
    STDMETHOD(SetGammaControl)(const void* pArray) PURE;
    STDMETHOD(GetGammaControl)(void* pArray) PURE;
    STDMETHOD(SetDisplaySurface)(IDXGIObject* pScanoutSurface) PURE;
    STDMETHOD(GetDisplaySurfaceData)(IDXGIObject* pDestination) PURE;
    STDMETHOD(GetFrameStatistics)(void* pStats) PURE;
};

class MIDL_INTERFACE("310d36a0-d2e7-4c0a-aa04-6a9d23b8886a")
IDXGISwapChain : public IDXGIObject {
public:
    STDMETHOD(Present)(UINT SyncInterval, UINT Flags) PURE;
    STDMETHOD(GetBuffer)(UINT Buffer, const GUID& riid, void** ppSurface) PURE;
    STDMETHOD(SetFullscreenState)(BOOL Fullscreen, IDXGIOutput* pTarget) PURE;
    STDMETHOD(GetFullscreenState)(BOOL* pFullscreen, IDXGIOutput** ppTarget) PURE;
    STDMETHOD(GetDesc)(void* pDesc) PURE;
    STDMETHOD(ResizeBuffers)(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) PURE;
    STDMETHOD(ResizeTarget)(const DXGI_MODE_DESC* pNewTargetParameters) PURE;
    STDMETHOD(GetContainingOutput)(IDXGIOutput** ppOutput) PURE;
    STDMETHOD(GetFrameStatistics)(void* pStats) PURE;
    STDMETHOD(GetLastPresentCount)(UINT* pLastPresentCount) PURE;
};

class MIDL_INTERFACE("a8ece026-1812-4191-a6f5-6d80c1fbb60c")
IDXGISwapChain1 : public IDXGISwapChain {
public:
    STDMETHOD(Present1)(UINT SyncInterval, UINT PresentFlags, const void* pPresentParameters) PURE;
};

extern "C" {
HRESULT CreateDXGIFactory(const GUID& riid, void** ppFactory);
HRESULT CreateDXGIFactory1(const GUID& riid, void** ppFactory);
HRESULT CreateDXGIFactory2(UINT Flags, const GUID& riid, void** ppFactory);
}
