cmake_minimum_required(VERSION 3.22)

if(WIN32 AND NOT CMAKE_SYSTEM_VERSION)
    set(CMAKE_SYSTEM_VERSION 10.0.22000.0 CACHE STRING "Target Windows 11 SDK" FORCE)
endif()

project(PhoneCamNative LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /EHsc /permissive- /utf-8)
    add_compile_definitions(
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00
        NTDDI_VERSION=0x0A00000B
    )
endif()

# Optional ONNX Runtime + DirectML paths (populated in CI or local builds)
set(ORT_ROOT "" CACHE PATH "Path to extracted Microsoft.ML.OnnxRuntime.DirectML NuGet root")
set(DML_ROOT "" CACHE PATH "Path to extracted Microsoft.AI.DirectML NuGet root")

set(PHONECAM_HAS_ONNX FALSE)
set(ORT_INCLUDE_DIRS "")
set(ORT_LIB_PATH "")
set(ORT_RUNTIME_DLL "")
set(DML_RUNTIME_DLL "")

if(ORT_ROOT AND EXISTS "${ORT_ROOT}/runtimes/win-x64/native/onnxruntime.lib")
    set(PHONECAM_HAS_ONNX TRUE)
    list(APPEND ORT_INCLUDE_DIRS "${ORT_ROOT}/build/native/include")
    set(ORT_LIB_PATH "${ORT_ROOT}/runtimes/win-x64/native/onnxruntime.lib")
    if(EXISTS "${ORT_ROOT}/runtimes/win-x64/native/onnxruntime.dll")
        set(ORT_RUNTIME_DLL "${ORT_ROOT}/runtimes/win-x64/native/onnxruntime.dll")
    endif()
    if(DML_ROOT)
        if(EXISTS "${DML_ROOT}/include/DirectML.h")
            list(APPEND ORT_INCLUDE_DIRS "${DML_ROOT}/include")
        endif()
        if(EXISTS "${DML_ROOT}/bin/x64-win/DirectML.dll")
            set(DML_RUNTIME_DLL "${DML_ROOT}/bin/x64-win/DirectML.dll")
        endif()
    endif()
    message(STATUS "ONNX Runtime + DirectML enabled: ${ORT_LIB_PATH}")
else()
    message(STATUS "ORT_ROOT not configured; building without ONNX Runtime AI inference (PHONECAM_HAS_ONNX=0)")
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateShaders.cmake")
set(GENERATED_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_SHADER_DIR}")
generate_shader_header("${GENERATED_SHADER_DIR}/GeneratedShaders.h" "${CMAKE_CURRENT_SOURCE_DIR}/engine/shaders")

# -----------------------------------------------------------------------------
# Common static library
# -----------------------------------------------------------------------------
add_library(phonecam_common STATIC
    common/Protocol.h
    common/SharedFrameBuffer.h
    common/SharedFrameBuffer.cpp
    common/ColorConvert.h
    common/ColorConvert.cpp
)
target_include_directories(phonecam_common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(phonecam_common PUBLIC shell32 ole32)

# -----------------------------------------------------------------------------
# PhoneCamEngine.dll (Networking, Bluetooth, H.264/DXVA Decoder, AI, D3D11 Shaders)
# -----------------------------------------------------------------------------
add_library(PhoneCamEngine SHARED
    engine/EngineApi.h
    engine/EngineApi.cpp
    engine/AiEngine.h
    engine/AiEngine.cpp
    engine/BluetoothClient.h
    engine/BluetoothClient.cpp
    engine/GpuProcessor.h
    engine/GpuProcessor.cpp
    engine/VideoDecoder.h
    engine/VideoDecoder.cpp
    engine/NetworkClient.h
    engine/NetworkClient.cpp
    engine/AdbHelper.h
    engine/AdbHelper.cpp
    engine/VirtualCamManager.h
    engine/VirtualCamManager.cpp
)
target_compile_definitions(PhoneCamEngine PRIVATE PHONECAM_ENGINE_EXPORTS)
target_include_directories(PhoneCamEngine PRIVATE "${GENERATED_SHADER_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(PhoneCamEngine PRIVATE
    phonecam_common
    d3d11
    d3dcompiler
    mfplat
    mfuuid
    mf
    wmcodecdspuuid
    strmiids
    windowscodecs
    ws2_32
    bthprops
    bcrypt
    ole32
    oleaut32
    advapi32
)
if(PHONECAM_HAS_ONNX)
    target_compile_definitions(PhoneCamEngine PRIVATE PHONECAM_HAS_ONNX=1)
    target_include_directories(PhoneCamEngine SYSTEM PRIVATE ${ORT_INCLUDE_DIRS})
    target_link_libraries(PhoneCamEngine PRIVATE "${ORT_LIB_PATH}")
    if(ORT_RUNTIME_DLL)
        add_custom_command(TARGET PhoneCamEngine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_RUNTIME_DLL}" "$<TARGET_FILE_DIR:PhoneCamEngine>")
    endif()
    if(DML_RUNTIME_DLL)
        add_custom_command(TARGET PhoneCamEngine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DML_RUNTIME_DLL}" "$<TARGET_FILE_DIR:PhoneCamEngine>")
    endif()
endif()

# -----------------------------------------------------------------------------
# PhoneCamMFSource.dll (Windows 11 Media Foundation Virtual Camera COM Source)
# -----------------------------------------------------------------------------
add_library(PhoneCamMFSource SHARED
    vcam_mf/MFSource.h
    vcam_mf/MFSource.cpp
    vcam_mf/MfGuids.cpp
    vcam_mf/DllMain.cpp
    vcam_mf/exports.def
)
target_link_libraries(PhoneCamMFSource PRIVATE
    phonecam_common
    mfplat
    mfuuid
    mf
    ksuser
    strmiids
    ole32
    oleaut32
    advapi32
)

# -----------------------------------------------------------------------------
# PhoneCamVcamSetup.exe (Persistent System Virtual Camera Registration Tool)
# -----------------------------------------------------------------------------
add_executable(PhoneCamVcamSetup
    vcam_setup/PhoneCamVcamSetup.cpp
)
target_link_libraries(PhoneCamVcamSetup PRIVATE
    mfplat
    mfuuid
    mf
    ksuser
    ole32
    advapi32
)

# -----------------------------------------------------------------------------
# PhoneCamDSFilter.dll (Windows 10/11 DirectShow Virtual Camera Filter)
# -----------------------------------------------------------------------------
add_library(PhoneCamDSFilter SHARED
    vcam_dshow/DSFilter.h
    vcam_dshow/DSFilter.cpp
    vcam_dshow/DllMain.cpp
    vcam_dshow/exports.def
)
target_link_libraries(PhoneCamDSFilter PRIVATE
    phonecam_common
    strmiids
    ole32
    oleaut32
    advapi32
    winmm
)

# -----------------------------------------------------------------------------
# Automated CTest Suite
# -----------------------------------------------------------------------------
enable_testing()

add_executable(test_protocol tests/test_protocol.cpp)
target_link_libraries(test_protocol PRIVATE phonecam_common)
add_test(NAME ProtocolTests COMMAND test_protocol)

add_executable(test_shared_buffer tests/test_shared_buffer.cpp)
target_link_libraries(test_shared_buffer PRIVATE phonecam_common)
add_test(NAME SharedFrameBufferTests COMMAND test_shared_buffer)

add_executable(test_gpu_pipeline
    tests/test_gpu_pipeline.cpp
    engine/GpuProcessor.cpp
    engine/GpuProcessor.h
    engine/AiEngine.cpp
    engine/AiEngine.h
)
target_include_directories(test_gpu_pipeline PRIVATE "${GENERATED_SHADER_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_gpu_pipeline PRIVATE phonecam_common d3d11 d3dcompiler windowscodecs ole32)
if(PHONECAM_HAS_ONNX)
    target_compile_definitions(test_gpu_pipeline PRIVATE PHONECAM_HAS_ONNX=1)
    target_include_directories(test_gpu_pipeline SYSTEM PRIVATE ${ORT_INCLUDE_DIRS})
    target_link_libraries(test_gpu_pipeline PRIVATE "${ORT_LIB_PATH}")
endif()
add_test(NAME GpuPipelineShaderTests COMMAND test_gpu_pipeline)

add_executable(test_ai_engine
    tests/test_ai_engine.cpp
    engine/AiEngine.cpp
    engine/AiEngine.h
)
target_include_directories(test_ai_engine PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_ai_engine PRIVATE phonecam_common ole32)
if(PHONECAM_HAS_ONNX)
    target_compile_definitions(test_ai_engine PRIVATE PHONECAM_HAS_ONNX=1)
    target_include_directories(test_ai_engine SYSTEM PRIVATE ${ORT_INCLUDE_DIRS})
    target_link_libraries(test_ai_engine PRIVATE "${ORT_LIB_PATH}")
    if(ORT_RUNTIME_DLL)
        add_custom_command(TARGET test_ai_engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_RUNTIME_DLL}" "$<TARGET_FILE_DIR:test_ai_engine>")
    endif()
    if(DML_RUNTIME_DLL)
        add_custom_command(TARGET test_ai_engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DML_RUNTIME_DLL}" "$<TARGET_FILE_DIR:test_ai_engine>")
    endif()
endif()
add_test(NAME AiEngineTests COMMAND test_ai_engine)

add_executable(test_vcam_com tests/test_vcam_com.cpp)
target_link_libraries(test_vcam_com PRIVATE phonecam_common mfplat mfuuid strmiids ksuser ole32)
add_dependencies(test_vcam_com PhoneCamMFSource PhoneCamDSFilter PhoneCamVcamSetup)
add_test(NAME VCamComTests COMMAND test_vcam_com)
