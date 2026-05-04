/// @file EntryPoint.cpp
/// @brief XAudio2 DLL entry point with COM class factory exports.
///
/// Exports DllGetClassObject and DllCanUnloadNow so the PE loader can instantiate XAudio2 COM objects. Acts as the xaudio2_9.dll entry point that games load.
#include <metalsharp/XAudio2Engine.h>

using namespace metalsharp;

extern "C" {

static XAudio2Engine* g_engine = nullptr;

HRESULT XAudio2Create(void** ppXAudio2, UINT Flags, UINT XAudio2Processor) {
    if (!ppXAudio2) return E_POINTER;
    auto* engine = new XAudio2Engine();
    HRESULT hr = engine->init();
    if (FAILED(hr)) { delete engine; return hr; }
    *ppXAudio2 = engine;
    g_engine = engine;
    return S_OK;
}

}
