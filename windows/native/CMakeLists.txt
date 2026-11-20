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
# PhoneCamEngine.dll (Networking, H.264/MJPEG Decoder, D3D11 Shaders, C-ABI)
# -----------------------------------------------------------------------------
add_library(PhoneCamEngine SHARED
    engine/EngineApi.h
    engine/EngineApi.cpp
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
    bcrypt
    ole32
    oleaut32
    advapi32
)

# -----------------------------------------------------------------------------
# PhoneCamMFSource.dll (Windows 11 Media Foundation Virtual Camera COM Source)
# -----------------------------------------------------------------------------
add_library(PhoneCamMFSource SHARED
    vcam_mf/MFSource.h
    vcam_mf/MFSource.cpp
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
)
target_include_directories(test_gpu_pipeline PRIVATE "${GENERATED_SHADER_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_gpu_pipeline PRIVATE phonecam_common d3d11 d3dcompiler)
add_test(NAME GpuPipelineShaderTests COMMAND test_gpu_pipeline)

add_executable(test_vcam_com tests/test_vcam_com.cpp)
target_link_libraries(test_vcam_com PRIVATE phonecam_common mfplat mfuuid strmiids ksuser ole32)
add_dependencies(test_vcam_com PhoneCamMFSource PhoneCamDSFilter)
add_test(NAME VCamComTests COMMAND test_vcam_com)
