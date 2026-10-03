# Only headers are used at link time. No ORT/GPU DLL is imported by the EXE.
set(RENDEPTH_CPU_RUNTIME_DIR "" CACHE PATH "Windows x64 CPU-only ONNX Runtime SDK")
find_path(RENDEPTH_ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
    HINTS "${RENDEPTH_ONNXRUNTIME_DIR}/include" "${RENDEPTH_CPU_RUNTIME_DIR}/include"
    PATH_SUFFIXES onnxruntime)
if(NOT RENDEPTH_ONNXRUNTIME_INCLUDE_DIR)
    message(FATAL_ERROR "Provide RENDEPTH_ONNXRUNTIME_DIR (ORT SDK headers shared by the packs)")
endif()
rendepth_require_ort_122_headers("${RENDEPTH_ONNXRUNTIME_INCLUDE_DIR}")
foreach(required lib/onnxruntime.dll LICENSE ThirdPartyNotices.txt)
    if(NOT EXISTS "${RENDEPTH_CPU_RUNTIME_DIR}/${required}")
        message(FATAL_ERROR "RENDEPTH_CPU_RUNTIME_DIR must name a CPU-only Windows x64 ORT SDK (missing ${required})")
    endif()
endforeach()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Windows inference packs require an x64 build")
endif()
if(EXISTS "${RENDEPTH_CPU_RUNTIME_DIR}/lib/onnxruntime_providers_cuda.dll")
    message(FATAL_ERROR "Use a CPU-only SDK for RENDEPTH_CPU_RUNTIME_DIR; CUDA belongs in an optional pack")
endif()
rendepth_require_ort_122_headers("${RENDEPTH_CPU_RUNTIME_DIR}/include")

add_library(RendepthInference INTERFACE)
target_compile_definitions(RendepthInference INTERFACE
    RENDEPTH_ENABLE_ONNX_RUNTIME RENDEPTH_DYNAMIC_ONNX_RUNTIME ORT_API_MANUAL_INIT)
target_include_directories(RendepthInference INTERFACE "${RENDEPTH_ONNXRUNTIME_INCLUDE_DIR}")
target_link_libraries(RendepthInference INTERFACE dxgi d3d12)

function(rendepth_windows_inference target)
    target_sources(${target} PRIVATE "${CMAKE_SOURCE_DIR}/Source/InferenceRuntimeWindows.cpp")
    target_link_libraries(${target} PRIVATE RendepthInference)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/Runtimes/cpu/bin"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${RENDEPTH_CPU_RUNTIME_DIR}/lib/onnxruntime.dll"
            "${CMAKE_SOURCE_DIR}/Runtimes/cpu/bin/onnxruntime.dll"
        VERBATIM)
endfunction()

rendepth_windows_inference(Rendepth)
if(RENDEPTH_BUILD_DEPTH_TEST)
    add_executable(DepthTest EXCLUDE_FROM_ALL Source/DepthTest.cpp Source/DepthEstimator.cpp)
    target_link_libraries(DepthTest PRIVATE SDL3::SDL3 SDL3_image::SDL3_image)
    rendepth_windows_inference(DepthTest)
endif()
if(RENDEPTH_BUILD_SUPER_TEST)
    add_executable(SuperTest EXCLUDE_FROM_ALL Source/SuperTest.cpp Source/SuperResolution.cpp)
    target_link_libraries(SuperTest PRIVATE SDL3::SDL3 SDL3_image::SDL3_image)
    rendepth_windows_inference(SuperTest)
endif()
add_executable(InferenceRuntimeTest EXCLUDE_FROM_ALL
    Source/InferenceRuntimeTest.cpp Source/DepthEstimator.cpp Source/SuperResolution.cpp)
target_link_libraries(InferenceRuntimeTest PRIVATE SDL3::SDL3 SDL3_image::SDL3_image)
rendepth_windows_inference(InferenceRuntimeTest)

install(FILES "${RENDEPTH_CPU_RUNTIME_DIR}/lib/onnxruntime.dll"
    DESTINATION libexec/rendepth/Runtimes/cpu/bin)
# Exact SDK notices are incorporated into THIRD_PARTY_LICENSING by Packaging/Legal.cmake.
