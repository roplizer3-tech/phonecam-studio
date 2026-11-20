#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <dshow.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/client.h>

#include <cassert>
#include <iostream>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {

// {7A9E4B21-6C3D-4F18-9E2A-51B8C4D730F1}
constexpr CLSID kClsidMfActivator = {
    0x7a9e4b21, 0x6c3d, 0x4f18, { 0x9e, 0x2a, 0x51, 0xb8, 0xc4, 0xd7, 0x30, 0xf1 }
};

// {8B1F5C32-7D4E-4A29-8F3B-62C9D5E841A2}
constexpr CLSID kClsidDsFilter = {
    0x8b1f5c32, 0x7d4e, 0x4a29, { 0x8f, 0x3b, 0x62, 0xc9, 0xd5, 0xe8, 0x41, 0xa2 }
};

using FnDllGetClassObject = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, LPVOID*);
using FnDllCanUnloadNow = HRESULT(STDAPICALLTYPE*)();

std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring s(buf);
    size_t pos = s.find_last_of(L"\\/");
    return pos != std::wstring::npos ? s.substr(0, pos) : L".";
}

void VerifyMfSourceDll(const std::wstring& dir) {
    std::wstring dllPath = dir + L"\\PhoneCamMFSource.dll";
    HMODULE mod = LoadLibraryW(dllPath.c_str());
    assert(mod != nullptr);

    assert(GetProcAddress(mod, "DllRegisterServer") != nullptr);
    assert(GetProcAddress(mod, "DllUnregisterServer") != nullptr);
    auto getObj = reinterpret_cast<FnDllGetClassObject>(GetProcAddress(mod, "DllGetClassObject"));
    auto canUnload = reinterpret_cast<FnDllCanUnloadNow>(GetProcAddress(mod, "DllCanUnloadNow"));
    assert(getObj && canUnload);

    ComPtr<IClassFactory> factory;
    assert(SUCCEEDED(getObj(kClsidMfActivator, IID_PPV_ARGS(&factory))));

    ComPtr<IMFActivate> activator;
    assert(SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&activator))));

    ComPtr<IMFMediaSourceEx> srcEx;
    assert(SUCCEEDED(activator->ActivateObject(IID_PPV_ARGS(&srcEx))));

    DWORD chars = 0;
    assert(SUCCEEDED(srcEx->GetCharacteristics(&chars)));
    assert((chars & MFMEDIASOURCE_IS_LIVE) != 0);

    ComPtr<IMFAttributes> srcAttrs;
    assert(SUCCEEDED(srcEx->GetSourceAttributes(&srcAttrs)));

    ComPtr<IMFAttributes> streamAttrs;
    assert(SUCCEEDED(srcEx->GetStreamAttributes(0, &streamAttrs)));
    GUID cat{};
    assert(SUCCEEDED(streamAttrs->GetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, &cat)));
    assert(cat == PINNAME_VIDEO_CAPTURE);

    ComPtr<IMFPresentationDescriptor> pd;
    assert(SUCCEEDED(srcEx->CreatePresentationDescriptor(&pd)));
    DWORD streamCount = 0;
    assert(SUCCEEDED(pd->GetStreamDescriptorCount(&streamCount)));
    assert(streamCount == 1);

    BOOL selected = FALSE;
    ComPtr<IMFStreamDescriptor> sd;
    assert(SUCCEEDED(pd->GetStreamDescriptorByIndex(0, &selected, &sd)));
    assert(selected == TRUE);

    ComPtr<IMFMediaTypeHandler> handler;
    assert(SUCCEEDED(sd->GetMediaTypeHandler(&handler)));
    DWORD mtCount = 0;
    assert(SUCCEEDED(handler->GetMediaTypeCount(&mtCount)));
    assert(mtCount >= 2);

    bool sawNv12 = false, sawRgb32 = false;
    for (DWORD i = 0; i < mtCount; ++i) {
        ComPtr<IMFMediaType> mt;
        assert(SUCCEEDED(handler->GetMediaTypeByIndex(i, &mt)));
        GUID sub{};
        assert(SUCCEEDED(mt->GetGUID(MF_MT_SUBTYPE, &sub)));
        if (sub == MFVideoFormat_NV12) sawNv12 = true;
        if (sub == MFVideoFormat_RGB32) sawRgb32 = true;
    }
    assert(sawNv12 && sawRgb32);

    assert(SUCCEEDED(srcEx->Start(pd.Get(), nullptr, nullptr)));
    assert(SUCCEEDED(srcEx->Stop()));
    assert(SUCCEEDED(activator->ShutdownObject()));
    srcEx.Reset();
    activator.Reset();
    factory.Reset();
    FreeLibrary(mod);
}

void VerifyDirectShowFilterDll(const std::wstring& dir) {
    std::wstring dllPath = dir + L"\\PhoneCamDSFilter.dll";
    HMODULE mod = LoadLibraryW(dllPath.c_str());
    assert(mod != nullptr);

    assert(GetProcAddress(mod, "DllRegisterServer") != nullptr);
    assert(GetProcAddress(mod, "DllUnregisterServer") != nullptr);
    auto getObj = reinterpret_cast<FnDllGetClassObject>(GetProcAddress(mod, "DllGetClassObject"));
    assert(getObj != nullptr);

    ComPtr<IClassFactory> factory;
    assert(SUCCEEDED(getObj(kClsidDsFilter, IID_PPV_ARGS(&factory))));

    ComPtr<IBaseFilter> filter;
    assert(SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&filter))));

    CLSID clsid{};
    assert(SUCCEEDED(filter->GetClassID(&clsid)));
    assert(clsid == kClsidDsFilter);

    ComPtr<IEnumPins> enumPins;
    assert(SUCCEEDED(filter->EnumPins(&enumPins)));
    ComPtr<IPin> pin;
    ULONG fetched = 0;
    assert(enumPins->Next(1, &pin, &fetched) == S_OK && fetched == 1);

    PIN_DIRECTION dirPin{};
    assert(SUCCEEDED(pin->QueryDirection(&dirPin)));
    assert(dirPin == PINDIR_OUTPUT);

    ComPtr<IKsPropertySet> ksProp;
    assert(SUCCEEDED(pin.As(&ksProp)));
    GUID cat{};
    DWORD retBytes = 0;
    assert(SUCCEEDED(ksProp->Get(AMPROPSETID_Pin, AMPROPERTY_PIN_CATEGORY, nullptr, 0, &cat, sizeof(cat), &retBytes)));
    assert(cat == PIN_CATEGORY_CAPTURE);

    ComPtr<IAMStreamConfig> cfg;
    assert(SUCCEEDED(pin.As(&cfg)));
    int count = 0, size = 0;
    assert(SUCCEEDED(cfg->GetNumberOfCapabilities(&count, &size)));
    assert(count >= 3 && size == sizeof(VIDEO_STREAM_CONFIG_CAPS));

    pin.Reset();
    enumPins.Reset();
    filter.Reset();
    factory.Reset();
    FreeLibrary(mod);
}

} // namespace

int main() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_LITE);

    const std::wstring dir = GetExeDir();
    VerifyMfSourceDll(dir);
    VerifyDirectShowFilterDll(dir);

    MFShutdown();
    CoUninitialize();
    std::cout << "[PASS] VCamComTests (MFSource + DirectShow Filter verified)\n";
    return 0;
}
