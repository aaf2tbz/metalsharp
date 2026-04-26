#pragma once

#include <stdint.h>
#include <stddef.h>
#include <cstring>

typedef uint32_t DWORD;
typedef int32_t LONG;
typedef int32_t HRESULT;
typedef void* HWND;
typedef void* HMODULE;
typedef void* HANDLE;
typedef unsigned long ULONG;
typedef unsigned int UINT;
typedef int INT;
typedef float FLOAT;
typedef unsigned char UINT8;
typedef uint64_t UINT64;
typedef size_t SIZE_T;

#ifndef __OBJC__
typedef int BOOL;
#endif

#define MS_TRUE 1
#define MS_FALSE 0
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef __OBJC__
typedef int BOOL;
#endif
#ifndef FALSE
#define FALSE 0
#endif

typedef struct GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
    bool operator==(const GUID& other) const {
        return memcmp(this, &other, sizeof(GUID)) == 0;
    }
    bool operator!=(const GUID& other) const { return !(*this == other); }
} GUID, REFIID, IID;

#define S_OK ((HRESULT)0L)
#define S_FALSE ((HRESULT)1L)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#define E_POINTER ((HRESULT)0x80004003L)
#define E_FAIL ((HRESULT)0x80004005L)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define DXGI_ERROR_INVALID_CALL ((HRESULT)0x887A0001L)
#define DXGI_ERROR_NOT_FOUND ((HRESULT)0x887A0002L)

#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)

#define STDMETHOD(method) virtual HRESULT method
#define STDMETHOD_(type, method) virtual type method
#define PURE = 0
#define THIS_
#define THIS
#define MIDL_INTERFACE(str)

#define __uuidof(x) IID_##x

class IUnknown {
public:
    virtual HRESULT QueryInterface(REFIID riid, void** ppvObject) = 0;
    virtual ULONG AddRef() = 0;
    virtual ULONG Release() = 0;
};

typedef struct tagRECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT;

typedef struct tagSIZE {
    LONG cx;
    LONG cy;
} SIZE;

constexpr HRESULT MAKE_HRESULT(int sev, int fac, int code) {
    return (HRESULT)(((unsigned long)(sev) << 31) | ((unsigned long)(fac) << 16) | ((unsigned long)(code)));
}

static const GUID IID_IUnknown = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
