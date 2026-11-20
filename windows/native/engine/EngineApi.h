#pragma once

#include <cstdint>

#if defined(_WIN32)
  #if defined(PHONECAM_ENGINE_EXPORTS)
    #define PC_API __declspec(dllexport)
  #else
    #define PC_API __declspec(dllimport)
  #endif
#else
  #define PC_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct PC_VideoParams {
    int32_t flipH;
    int32_t flipV;
    int32_t manualRotationDeg;   // 0, 90, 180, 270
    int32_t autoRotateFromPhone; // 1 = combine phone sensor rotation + manualRotationDeg
    int32_t framingMode;         // 0 = Fill/Crop, 1 = Fit+Blur, 2 = Fit+Black
    int32_t outputWidth;         // e.g. 1280 or 1920
    int32_t outputHeight;        // e.g. 720 or 1080
    int32_t outputFps;           // 30 or 60
    int32_t previewMirrorLocal;  // 1 = selfie preview only (never sent to virtual cam)
    int32_t mirrorCheckOverlay;  // 1 = overlay "123 ABC ->" on output & preview
    int32_t bypassEffects;       // 1 = bypass beauty & color grading
    int32_t testPatternMode;     // 1 = generate animated test pattern even without phone

    float zoom;                  // 1.0 .. 3.0
    float panX;                  // -0.5 .. 0.5
    float panY;                  // -0.5 .. 0.5
    float skinSmooth;            // 0.0 .. 1.0
    float denoise;               // 0.0 .. 1.0
    float studioLight;           // 0.0 .. 1.0
    float exposure;              // -2.0 .. +2.0
    float brightness;            // -0.5 .. +0.5
    float contrast;              // 0.5 .. 1.8
    float highlights;            // -1.0 .. +1.0
    float shadows;               // -1.0 .. +1.0
    float gammaVal;              // 0.6 .. 1.6
    float temperature;           // -1.0 .. +1.0
    float tint;                  // -1.0 .. +1.0
    float saturation;            // 0.0 .. 2.0
    float vibrance;              // -1.0 .. +1.0
    float sharpen;               // 0.0 .. 1.0
    float softGlow;              // 0.0 .. 1.0
    float vignette;              // 0.0 .. 1.0
    float autoWhiteBalance;      // 0.0 .. 1.0
    float splitComparePos;       // < 0 disabled, 0.0 .. 1.0 preview split X
};

typedef void(__stdcall* PC_PreviewFrameCallback)(
    const uint8_t* bgraData,
    int32_t width,
    int32_t height,
    int32_t strideBytes,
    void* userData);

PC_API void* __stdcall pc_create();
PC_API void __stdcall pc_destroy(void* handle);

PC_API int32_t __stdcall pc_start_discovery(void* handle);
PC_API int32_t __stdcall pc_get_discovered_devices_json(void* handle, char* outBuf, int32_t bufSize);

PC_API int32_t __stdcall pc_connect(void* handle, const char* ipUtf8, int32_t port, const char* pinUtf8);
PC_API int32_t __stdcall pc_connect_usb(void* handle, const char* pinUtf8);
PC_API void __stdcall pc_disconnect(void* handle);

PC_API int32_t __stdcall pc_get_status_json(void* handle, char* outBuf, int32_t bufSize);
PC_API int32_t __stdcall pc_send_control_json(void* handle, const char* jsonUtf8);
PC_API void __stdcall pc_set_video_params(void* handle, const PC_VideoParams* params);

PC_API int32_t __stdcall pc_start_virtual_cam(void* handle);
PC_API void __stdcall pc_stop_virtual_cam(void* handle);
PC_API int32_t __stdcall pc_get_vcam_diagnostics_json(void* handle, char* outBuf, int32_t bufSize);

PC_API void __stdcall pc_set_preview_callback(void* handle, PC_PreviewFrameCallback cb, void* userData);
PC_API int32_t __stdcall pc_push_test_frame(void* handle);

#ifdef __cplusplus
}
#endif
