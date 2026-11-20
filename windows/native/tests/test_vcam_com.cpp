#include "TestCheck.h"

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

// {FB6C4281-0353-11D1-905F-0000C0CC16BA} == PINNAME_VIDEO_CAPTURE
constexpr GUID kPinNameVideoCapture = {
    0xFB6C4281u, 0x0353u, 0x11D1u, { 0x90u, 0x5Fu, 0x00u, 0x00u, 0xC0u, 0xCCu, 0x16u, 0xBAu }
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
    PC_CHECK(mod != nullptr);

    PC_CHECK(GetProcAddress(mod, "DllRegisterServer") != nullptr);
    PC_CHECK(GetProcAddress(mod, "DllUnregisterServer") != nullptr);
    auto getObj = reinterpret_cast<FnDllGetClassObject>(GetProcAddress(mod, "DllGetClassObject"));
    auto canUnload = reinterpret_cast<FnDllCanUnloadNow>(GetProcAddress(mod, "DllCanUnloadNow"));
    PC_CHECK(getObj && canUnload);

    ComPtr<IClassFactory> factory;
    PC_CHECK(SUCCEEDED(getObj(kClsidMfActivator, IID_PPV_ARGS(&factory))));

    ComPtr<IMFActivate> activator;
    PC_CHECK(SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&activator))));

    ComPtr<IMFMediaSourceEx> srcEx;
    PC_CHECK(SUCCEEDED(activator->ActivateObject(IID_PPV_ARGS(&srcEx))));

    DWORD chars = 0;
    PC_CHECK(SUCCEEDED(srcEx->GetCharacteristics(&chars)));
    PC_CHECK((chars & MFMEDIASOURCE_IS_LIVE) != 0);

    ComPtr<IMFAttributes> srcAttrs;
    PC_CHECK(SUCCEEDED(srcEx->GetSourceAttributes(&srcAttrs)));

    ComPtr<IMFSampleAllocatorControl> allocCtrl;
    PC_CHECK(SUCCEEDED(srcEx.As(&allocCtrl)));
    PC_CHECK(allocCtrl != nullptr);

    if (HMODULE hSensor = LoadLibraryW(L"mfsensorgroup.dll")) {
        ComPtr<IUnknown> profCol;
        PC_CHECK(SUCCEEDED(srcAttrs->GetUnknown(MF_DEVICEMFT_SENSORPROFILE_COLLECTION, IID_PPV_ARGS(&profCol))));
        PC_CHECK(profCol != nullptr);
        profCol.Reset();
        FreeLibrary(hSensor);
    }

    ComPtr<IMFAttributes> streamAttrs;
    PC_CHECK(SUCCEEDED(srcEx->GetStreamAttributes(0, &streamAttrs)));
    GUID cat{};
    PC_CHECK(SUCCEEDED(streamAttrs->GetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, &cat)));
    PC_CHECK(cat == kPinNameVideoCapture);

    ComPtr<IMFPresentationDescriptor> pd;
    PC_CHECK(SUCCEEDED(srcEx->CreatePresentationDescriptor(&pd)));
    DWORD streamCount = 0;
    PC_CHECK(SUCCEEDED(pd->GetStreamDescriptorCount(&streamCount)));
    PC_CHECK(streamCount == 1);

    BOOL selected = FALSE;
    ComPtr<IMFStreamDescriptor> sd;
    PC_CHECK(SUCCEEDED(pd->GetStreamDescriptorByIndex(0, &selected, &sd)));
    PC_CHECK(selected == TRUE);

    ComPtr<IMFMediaTypeHandler> handler;
    PC_CHECK(SUCCEEDED(sd->GetMediaTypeHandler(&handler)));
    DWORD mtCount = 0;
    PC_CHECK(SUCCEEDED(handler->GetMediaTypeCount(&mtCount)));
    PC_CHECK(mtCount >= 2);

    bool sawNv12 = false, sawRgb32 = false;
    for (DWORD i = 0; i < mtCount; ++i) {
        ComPtr<IMFMediaType> mt;
        PC_CHECK(SUCCEEDED(handler->GetMediaTypeByIndex(i, &mt)));
        GUID sub{};
        PC_CHECK(SUCCEEDED(mt->GetGUID(MF_MT_SUBTYPE, &sub)));
        if (sub == MFVideoFormat_NV12) sawNv12 = true;
        if (sub == MFVideoFormat_RGB32) sawRgb32 = true;
    }
    PC_CHECK(sawNv12 && sawRgb32);

    PC_CHECK(SUCCEEDED(srcEx->Start(pd.Get(), nullptr, nullptr)));
    PC_CHECK(SUCCEEDED(srcEx->Stop()));
    PC_CHECK(SUCCEEDED(activator->ShutdownObject()));
    pd.Reset();
    sd.Reset();
    handler.Reset();
    allocCtrl.Reset();
    srcAttrs.Reset();
    streamAttrs.Reset();
    srcEx.Reset();
    activator.Reset();
    factory.Reset();
    PC_CHECK(canUnload() == S_OK);
    FreeLibrary(mod);
}

void VerifyDirectShowFilterDll(const std::wstring& dir) {
    std::wstring dllPath = dir + L"\\PhoneCamDSFilter.dll";
    HMODULE mod = LoadLibraryW(dllPath.c_str());
    PC_CHECK(mod != nullptr);

    PC_CHECK(GetProcAddress(mod, "DllRegisterServer") != nullptr);
    PC_CHECK(GetProcAddress(mod, "DllUnregisterServer") != nullptr);
    auto getObj = reinterpret_cast<FnDllGetClassObject>(GetProcAddress(mod, "DllGetClassObject"));
    PC_CHECK(getObj != nullptr);

    ComPtr<IClassFactory> factory;
    PC_CHECK(SUCCEEDED(getObj(kClsidDsFilter, IID_PPV_ARGS(&factory))));

    ComPtr<IBaseFilter> filter;
    PC_CHECK(SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&filter))));

    CLSID clsid{};
    PC_CHECK(SUCCEEDED(filter->GetClassID(&clsid)));
    PC_CHECK(clsid == kClsidDsFilter);

    ComPtr<IEnumPins> enumPins;
    PC_CHECK(SUCCEEDED(filter->EnumPins(&enumPins)));
    ComPtr<IPin> pin;
    ULONG fetched = 0;
    PC_CHECK(enumPins->Next(1, &pin, &fetched) == S_OK && fetched == 1);

    PIN_DIRECTION dirPin{};
    PC_CHECK(SUCCEEDED(pin->QueryDirection(&dirPin)));
    PC_CHECK(dirPin == PINDIR_OUTPUT);

    ComPtr<IKsPropertySet> ksProp;
    PC_CHECK(SUCCEEDED(pin.As(&ksProp)));
    GUID cat{};
    DWORD retBytes = 0;
    PC_CHECK(SUCCEEDED(ksProp->Get(AMPROPSETID_Pin, AMPROPERTY_PIN_CATEGORY, nullptr, 0, &cat, sizeof(cat), &retBytes)));
    PC_CHECK(cat == PIN_CATEGORY_CAPTURE);

    ComPtr<IAMStreamConfig> cfg;
    PC_CHECK(SUCCEEDED(pin.As(&cfg)));
    int count = 0, size = 0;
    PC_CHECK(SUCCEEDED(cfg->GetNumberOfCapabilities(&count, &size)));
    PC_CHECK(count >= 3 && size == sizeof(VIDEO_STREAM_CONFIG_CAPS));

    cfg.Reset();
    ksProp.Reset();
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
