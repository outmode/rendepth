# Linux executables use only the ORT headers at link time. The CPU runtime is
# an explicit release input; optional GPU packs are never part of the base.
set(RENDEPTH_CPU_RUNTIME_LIBRARY "" CACHE FILEPATH
    "Self-contained CPU ONNX Runtime shared library to bundle")
set(RENDEPTH_CPU_RUNTIME_NOTICES_DIR "" CACHE PATH
    "CPU ONNX Runtime SDK directory containing LICENSE and ThirdPartyNotices.txt")
find_path(RENDEPTH_ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
    HINTS "${RENDEPTH_ONNXRUNTIME_DIR}/include"
    PATH_SUFFIXES onnxruntime)
if(NOT RENDEPTH_ONNXRUNTIME_INCLUDE_DIR)
    message(FATAL_ERROR "Provide RENDEPTH_ONNXRUNTIME_INCLUDE_DIR (ORT headers shared by all packs)")
endif()
rendepth_require_ort_headers("${RENDEPTH_ONNXRUNTIME_INCLUDE_DIR}")
if(NOT EXISTS "${RENDEPTH_CPU_RUNTIME_LIBRARY}")
    message(FATAL_ERROR "Set RENDEPTH_CPU_RUNTIME_LIBRARY to a CPU-only ONNX Runtime .so")
endif()
foreach(notice LICENSE ThirdPartyNotices.txt)
    if(NOT EXISTS "${RENDEPTH_CPU_RUNTIME_NOTICES_DIR}/${notice}")
        message(FATAL_ERROR "RENDEPTH_CPU_RUNTIME_NOTICES_DIR must contain ${notice}")
    endif()
endforeach()

add_library(RendepthInference INTERFACE)
target_compile_definitions(RendepthInference INTERFACE
    RENDEPTH_ENABLE_ONNX_RUNTIME RENDEPTH_DYNAMIC_ONNX_RUNTIME ORT_API_MANUAL_INIT)
target_include_directories(RendepthInference INTERFACE "${RENDEPTH_ONNXRUNTIME_INCLUDE_DIR}")
target_link_libraries(RendepthInference INTERFACE ${CMAKE_DL_LIBS})

function(rendepth_linux_inference target)
    target_sources(${target} PRIVATE "${CMAKE_SOURCE_DIR}/Source/InferenceRuntime.cpp")
    target_link_libraries(${target} PRIVATE RendepthInference)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/Runtimes/cpu/lib"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${RENDEPTH_CPU_RUNTIME_LIBRARY}"
            "${CMAKE_SOURCE_DIR}/Runtimes/cpu/lib/libonnxruntime.so.1")
endfunction()

rendepth_linux_inference(Rendepth)
if(RENDEPTH_BUILD_DEPTH_TEST)
    add_executable(DepthTest EXCLUDE_FROM_ALL Source/DepthTest.cpp Source/DepthEstimator.cpp)
    target_include_directories(DepthTest PRIVATE Source)
    target_link_libraries(DepthTest PRIVATE SDL3::SDL3 SDL3_image::SDL3_image)
    rendepth_linux_inference(DepthTest)
endif()
if(RENDEPTH_BUILD_SUPER_TEST)
    add_executable(SuperTest EXCLUDE_FROM_ALL Source/SuperTest.cpp Source/SuperResolution.cpp)
    target_include_directories(SuperTest PRIVATE Source)
    target_link_libraries(SuperTest PRIVATE SDL3::SDL3 SDL3_image::SDL3_image)
    rendepth_linux_inference(SuperTest)
endif()

install(FILES "${RENDEPTH_CPU_RUNTIME_LIBRARY}"
    DESTINATION libexec/rendepth/Runtimes/cpu/lib RENAME libonnxruntime.so.1)
# Exact SDK notices are incorporated into THIRD_PARTY_LICENSING by Packaging/Legal.cmake.

add_executable(InferenceRuntimeTest EXCLUDE_FROM_ALL
    Source/InferenceRuntimeTest.cpp Source/DepthEstimator.cpp Source/SuperResolution.cpp
    Source/InferenceRuntime.cpp)
target_include_directories(InferenceRuntimeTest PRIVATE Source)
target_link_libraries(InferenceRuntimeTest PRIVATE
    RendepthInference SDL3::SDL3 SDL3_image::SDL3_image)
