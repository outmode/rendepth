cmake_minimum_required(VERSION 3.22)
project(Rendepth LANGUAGES CXX)
set(EXECUTABLE_NAME Rendepth)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

option(RENDEPTH_ENABLE_ONNX_RUNTIME "Enable native ONNX Runtime depth inference" OFF)
option(RENDEPTH_ENABLE_ROCM "Enable the ONNX Runtime ROCm execution provider" OFF)
option(RENDEPTH_BUILD_DEPTH_TEST "Build the standalone ONNX depth test harness" OFF)
option(RENDEPTH_COMPILE_SHADERS "Compile shader sources before building Rendepth" OFF)
set(RENDEPTH_ONNXRUNTIME_DIR "" CACHE PATH "ONNX Runtime installation directory")
set(RENDEPTH_ROCM_DIR "/usr/lib64/rocm" CACHE PATH "ROCm installation directory")
set(EXECUTABLE_OUTPUT_PATH ../Binary)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_POLICY_DEFAULT_CMP0077 OLD)
set(CMAKE_POLICY_DEFAULT_CMP0095 OLD)
set(CMAKE_POLICY_DEFAULT_CMP0068 OLD)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Binary)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/Binary)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/Binary)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Library)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/Library)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/Library)

set(CMAKE_SKIP_BUILD_RPATH TRUE)
set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)

if(APPLE)
    set(CMAKE_INSTALL_RPATH "@loader_path/../Library")
    set(CMAKE_MACOSX_RPATH TRUE)
elseif(UNIX)
    set(CMAKE_INSTALL_RPATH "\${ORIGIN\}/../Library")
endif()

set(SDL_AUDIO OFF)
set(SDL_CAMERA OFF)
set(SDL_VIDEO ON)
set(SDL_HIDAPI OFF)
set(SDL_JOYSTICK OFF)
set(SDL_POWER OFF)
set(SDL_HAPTIC OFF)
set(SDL_SENSOR OFF)
set(SDL_TEST_LIBRARY OFF)

set(SDLIMAGE_VENDORED ON)
set(SDLIMAGE_AVIF OFF)
set(SDLIMAGE_BMP ON)
set(SDLIMAGE_GIF OFF)
set(SDLIMAGE_JPG ON)
set(SDLIMAGE_JXL OFF)
set(SDLIMAGE_LBM OFF)
set(SDLIMAGE_PCX OFF)
set(SDLIMAGE_PNG ON)
set(SDLIMAGE_PNM OFF)
set(SDLIMAGE_QOI OFF)
set(SDLIMAGE_SVG OFF)
set(SDLIMAGE_TGA ON)
set(SDLIMAGE_TIF OFF)
set(SDLIMAGE_WEBP OFF)
set(SDLIMAGE_XCF OFF)
set(SDLIMAGE_XPM OFF)
set(SDLIMAGE_XV OFF)
set(SDLIMAGE_AOM OFF)

set(SDLTTF_VENDORED ON)
set(SDLTTF_FREETYPE ON)
set(SDLTTF_HARFBUZZ OFF)
set(SDLTTF_PLUTOSVG OFF)

set(GLM_STATIC_LIBRARY ON)
set(WITH_PERF_TOOL OFF)
set(USE_OWN_FFTW OFF)
set(BUILD_BENCHMARK OFF)
set(ENABLE_TESTS OFF)
set(BUILD_TESTING OFF)
set(BUILD_SHARED_LIBS ON)

if(WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -mavx -w -O2")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static-libstdc++ -mavx -w -O2")
elseif(APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -Xclang -w -O2")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static-libstdc++ -Xclang -w -O2")
elseif(UNIX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -mavx -w -fpermissive -O2")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static-libgcc -mavx -w -fpermissive -O2")
endif()

add_subdirectory(ThirdParty/SDL EXCLUDE_FROM_ALL)
add_subdirectory(ThirdParty/SDL_ttf EXCLUDE_FROM_ALL)
add_subdirectory(ThirdParty/SDL_image EXCLUDE_FROM_ALL)
add_subdirectory(ThirdParty/glm EXCLUDE_FROM_ALL)

if(RENDEPTH_COMPILE_SHADERS)
    set(SDLSHADERCROSS_DXC ON CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_SHARED OFF CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_STATIC ON CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_VENDORED ON CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_CLI ON CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(ThirdParty/SDL_shadercross EXCLUDE_FROM_ALL)

    find_program(RENDEPTH_GLSLANG_VALIDATOR glslangValidator REQUIRED)
    set(RENDEPTH_SHADER_TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/Shaders/Temp")
    set(RENDEPTH_SHADER_OUTPUTS)
    set(QUILTTEST_SHADER_OUTPUTS)
    foreach(shader IN ITEMS
            Image.vert Image.frag Icon.vert Icon.frag Sprite.vert Sprite.frag Interlacer.frag
            QuiltTest.vert QuiltTest.frag)
        set(shader_spv "${CMAKE_SOURCE_DIR}/Shaders/Compiled/${shader}.spv")
        set(shader_dxil "${CMAKE_SOURCE_DIR}/Shaders/Compiled/${shader}.dxil")
        set(shader_msl "${CMAKE_SOURCE_DIR}/Shaders/Compiled/${shader}.msl")
        set(shader_hlsl "${RENDEPTH_SHADER_TEMP_DIR}/${shader}.hlsl")
        add_custom_command(
                OUTPUT "${shader_dxil}" "${shader_msl}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${RENDEPTH_SHADER_TEMP_DIR}"
                COMMAND ${RENDEPTH_GLSLANG_VALIDATOR} -V
                        "${CMAKE_SOURCE_DIR}/Shaders/${shader}"
                        -o "${shader_spv}"
                COMMAND $<TARGET_FILE:shadercross> "${shader_spv}"
                        -s SPIRV -d MSL -o "${shader_msl}"
                COMMAND $<TARGET_FILE:shadercross> "${shader_spv}"
                        -s SPIRV -d HLSL -o "${shader_hlsl}"
                COMMAND $<TARGET_FILE:shadercross> "${shader_hlsl}"
                        -s HLSL -d DXIL -o "${shader_dxil}"
                DEPENDS
                        "${CMAKE_SOURCE_DIR}/Shaders/${shader}"
                        $<TARGET_FILE:shadercross>
                VERBATIM)
        if(shader MATCHES "^QuiltTest\\.")
            list(APPEND QUILTTEST_SHADER_OUTPUTS "${shader_dxil}" "${shader_msl}")
        else()
            list(APPEND RENDEPTH_SHADER_OUTPUTS "${shader_dxil}" "${shader_msl}")
        endif()
    endforeach()
    add_custom_target(Shaders DEPENDS ${RENDEPTH_SHADER_OUTPUTS})
    add_custom_target(QuiltTestShaders DEPENDS ${QUILTTEST_SHADER_OUTPUTS})
endif()

if(WIN32)
    add_compile_options(-Wl,-Bstatic)
    add_executable(Rendepth WIN32 Assets/Rendepth.rc Source/Main.cpp)
elseif(APPLE)
    if(NOT RENDEPTH_MAC_BUNDLE)
        add_executable(Rendepth Source/Main.cpp)
    else()
        set(MACOSX_BUNDLE_ICON_FILE Rendepth.icns)
        add_executable(Rendepth MACOSX_BUNDLE ${CMAKE_CURRENT_SOURCE_DIR}/Assets/Rendepth.icns
                ${CMAKE_SOURCE_DIR}/Assets/AppIcons.png ${CMAKE_SOURCE_DIR}/Assets/CircleIcon.png
                ${CMAKE_SOURCE_DIR}/Assets/Lato.ttf
                ${CMAKE_SOURCE_DIR}/Shaders/Compiled/Image.vert.msl ${CMAKE_SOURCE_DIR}/Shaders/Compiled/Image.frag.msl
                ${CMAKE_SOURCE_DIR}/Shaders/Compiled/Icon.vert.msl ${CMAKE_SOURCE_DIR}/Shaders/Compiled/Icon.frag.msl
                ${CMAKE_SOURCE_DIR}/Shaders/Compiled/Sprite.vert.msl ${CMAKE_SOURCE_DIR}/Shaders/Compiled/Sprite.frag.msl
                ${CMAKE_SOURCE_DIR}/Library/libSDL3.0.dylib ${CMAKE_SOURCE_DIR}/Library/libSDL3.dylib
                ${CMAKE_SOURCE_DIR}/Library/libSDL3_image.0.2.4.dylib ${CMAKE_SOURCE_DIR}/Library/libSDL3_image.0.dylib
                ${CMAKE_SOURCE_DIR}/Library/libSDL3_image.dylib
                ${CMAKE_SOURCE_DIR}/Library/libSDL3_ttf.0.2.2.dylib ${CMAKE_SOURCE_DIR}/Library/libSDL3_ttf.0.dylib
                ${CMAKE_SOURCE_DIR}/Library/libSDL3_ttf.dylib
                ${CMAKE_SOURCE_DIR}/Library/libglm.dylib
                ${CMAKE_SOURCE_DIR}/Legal/LICENSE)
        set_source_files_properties(
                ${CMAKE_SOURCE_DIR}/Library/libSDL3.0.dylib ${CMAKE_SOURCE_DIR}/Library/libSDL3.dylib
                ${CMAKE_SOURCE_DIR}/Library/libSDL3_image.0.2.4.dylib ${CMAKE_SOURCE_DIR}/Library/libSDL3_image.0.dylib
                ${CMAKE_SOURCE_DIR}/Library/libSDL3_image.dylib
                ${CMAKE_SOURCE_DIR}/Library/libSDL3_ttf.0.2.2.dylib ${CMAKE_SOURCE_DIR}/Library/libSDL3_ttf.0.dylib
                ${CMAKE_SOURCE_DIR}/Library/libSDL3_ttf.dylib
                ${CMAKE_SOURCE_DIR}/Library/libglm.dylib
                PROPERTIES
                MACOSX_PACKAGE_LOCATION Library)
        set_source_files_properties(
                ${CMAKE_SOURCE_DIR}/Assets/Rendepth.icns
                PROPERTIES
                MACOSX_PACKAGE_LOCATION "Resources")
        set_source_files_properties(
                ${CMAKE_SOURCE_DIR}/Assets/AppIcons.png ${CMAKE_SOURCE_DIR}/Assets/CircleIcon.png
                ${CMAKE_SOURCE_DIR}/Assets/Lato.ttf
                PROPERTIES
                MACOSX_PACKAGE_LOCATION Assets)
        set_source_files_properties(
                ${CMAKE_SOURCE_DIR}/Shaders/Compiled/Image.vert.msl ${CMAKE_SOURCE_DIR}/Shaders/Compiled/Image.frag.msl
                ${CMAKE_SOURCE_DIR}/Shaders/Compiled/Icon.vert.msl ${CMAKE_SOURCE_DIR}/Shaders/Compiled/Icon.frag.msl
                ${CMAKE_SOURCE_DIR}/Shaders/Compiled/Sprite.vert.msl ${CMAKE_SOURCE_DIR}/Shaders/Compiled/Sprite.frag.msl
                PROPERTIES
                MACOSX_PACKAGE_LOCATION Shaders/Compiled)
        set_source_files_properties(
                ${CMAKE_SOURCE_DIR}/Legal/LICENSE
                PROPERTIES
                MACOSX_PACKAGE_LOCATION Legal)
    endif()
elseif(UNIX)
    add_executable(Rendepth Source/Main.cpp)
endif()

if(RENDEPTH_COMPILE_SHADERS)
    add_dependencies(Rendepth Shaders)
endif()

target_compile_definitions(Rendepth PUBLIC SDL_MAIN_USE_CALLBACKS)

target_sources(Rendepth PUBLIC Source/Main.cpp Source/Core.cpp Source/Image.cpp
        Source/Style.cpp Source/Utils.cpp Source/DepthEstimator.cpp)

target_include_directories(Rendepth PUBLIC
        ThirdParty/glm ThirdParty/SDL/include ThirdParty/SDL_image/include
        ThirdParty/SDL_ttf/include ThirdParty/SDL_shadercross/include
        ThirdParty/rapidjson/include)

target_link_directories(Rendepth PUBLIC "\${ORIGIN}/../Library")

target_link_libraries(Rendepth PUBLIC
        SDL3::SDL3 SDL3_ttf::SDL3_ttf SDL3_image::SDL3_image
        glm)

if(RENDEPTH_ENABLE_ONNX_RUNTIME)
    if(NOT RENDEPTH_ONNXRUNTIME_DIR)
        message(FATAL_ERROR "RENDEPTH_ONNXRUNTIME_DIR must point to an ONNX Runtime installation")
    endif()
    set(RENDEPTH_ONNXRUNTIME_INCLUDE_DIR "${RENDEPTH_ONNXRUNTIME_DIR}/include")
    if(EXISTS "${RENDEPTH_ONNXRUNTIME_DIR}/include/onnxruntime/onnxruntime_cxx_api.h")
        set(RENDEPTH_ONNXRUNTIME_INCLUDE_DIR
                "${RENDEPTH_ONNXRUNTIME_DIR}/include/onnxruntime")
    endif()
    target_compile_definitions(Rendepth PUBLIC RENDEPTH_ENABLE_ONNX_RUNTIME)
    target_include_directories(Rendepth PUBLIC
            "${RENDEPTH_ONNXRUNTIME_INCLUDE_DIR}")
    if(RENDEPTH_ENABLE_ROCM)
        if(NOT UNIX OR APPLE)
            message(FATAL_ERROR "RENDEPTH_ENABLE_ROCM currently requires Linux ROCm libraries")
        endif()
        find_library(RENDEPTH_ROCM_ONNXRUNTIME_LIBRARY
                NAMES onnxruntime libonnxruntime.so.1.22.2
                PATHS "${RENDEPTH_ROCM_DIR}/lib" NO_DEFAULT_PATH)
        if(NOT RENDEPTH_ROCM_ONNXRUNTIME_LIBRARY)
            message(FATAL_ERROR "Could not find ROCm-enabled ONNX Runtime under ${RENDEPTH_ROCM_DIR}/lib")
        endif()
        target_compile_definitions(Rendepth PUBLIC RENDEPTH_ENABLE_ROCM)
        target_link_libraries(Rendepth PUBLIC "${RENDEPTH_ROCM_ONNXRUNTIME_LIBRARY}")
        set_property(TARGET Rendepth APPEND PROPERTY BUILD_RPATH
                "${RENDEPTH_ROCM_DIR}/lib")
    elseif(WIN32)
        target_link_libraries(Rendepth PUBLIC "${RENDEPTH_ONNXRUNTIME_DIR}/lib/onnxruntime.lib")
    elseif(APPLE)
        target_link_libraries(Rendepth PUBLIC "${RENDEPTH_ONNXRUNTIME_DIR}/lib/libonnxruntime.dylib")
    else()
        target_link_libraries(Rendepth PUBLIC "${RENDEPTH_ONNXRUNTIME_DIR}/lib/libonnxruntime.so")
    endif()
    set_target_properties(Rendepth PROPERTIES
            SKIP_BUILD_RPATH FALSE
            BUILD_RPATH "${CMAKE_SOURCE_DIR}/Library;${RENDEPTH_ONNXRUNTIME_DIR}/lib;${RENDEPTH_ROCM_DIR}/lib"
            INSTALL_RPATH "${CMAKE_SOURCE_DIR}/Library;${RENDEPTH_ONNXRUNTIME_DIR}/lib;${RENDEPTH_ROCM_DIR}/lib")
    if(RENDEPTH_ENABLE_ROCM)
        set_target_properties(Rendepth PROPERTIES
                BUILD_RPATH "${CMAKE_SOURCE_DIR}/Library;${RENDEPTH_ROCM_DIR}/lib"
                INSTALL_RPATH "${CMAKE_SOURCE_DIR}/Library;${RENDEPTH_ROCM_DIR}/lib")
        target_link_options(Rendepth PRIVATE "-Wl,--disable-new-dtags")
        add_custom_command(TARGET Rendepth POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E rm -f
                "${CMAKE_SOURCE_DIR}/Library/libonnxruntime.so.1"
                COMMAND ${CMAKE_COMMAND} -E create_symlink
                "${RENDEPTH_ROCM_ONNXRUNTIME_LIBRARY}"
                "${CMAKE_SOURCE_DIR}/Library/libonnxruntime.so.1")
    endif()

    if(RENDEPTH_BUILD_DEPTH_TEST)
        add_executable(DepthTest Source/DepthTest.cpp Source/DepthEstimator.cpp)
        target_compile_definitions(DepthTest PUBLIC RENDEPTH_ENABLE_ONNX_RUNTIME)
        target_include_directories(DepthTest PUBLIC
                Source ThirdParty/SDL/include ThirdParty/SDL_image/include
                "${RENDEPTH_ONNXRUNTIME_INCLUDE_DIR}")
        target_link_libraries(DepthTest PUBLIC
                SDL3::SDL3 SDL3_image::SDL3_image)
        if(RENDEPTH_ENABLE_ROCM)
            target_compile_definitions(DepthTest PUBLIC RENDEPTH_ENABLE_ROCM)
            target_link_libraries(DepthTest PUBLIC "${RENDEPTH_ROCM_ONNXRUNTIME_LIBRARY}")
            set_property(TARGET DepthTest APPEND PROPERTY BUILD_RPATH
                    "${RENDEPTH_ROCM_DIR}/lib")
        elseif(WIN32)
            target_link_libraries(DepthTest PUBLIC "${RENDEPTH_ONNXRUNTIME_DIR}/lib/onnxruntime.lib")
        elseif(APPLE)
            target_link_libraries(DepthTest PUBLIC "${RENDEPTH_ONNXRUNTIME_DIR}/lib/libonnxruntime.dylib")
        else()
            target_link_libraries(DepthTest PUBLIC "${RENDEPTH_ONNXRUNTIME_DIR}/lib/libonnxruntime.so")
        endif()
        set_target_properties(DepthTest PROPERTIES
                SKIP_BUILD_RPATH FALSE
                BUILD_RPATH "${CMAKE_SOURCE_DIR}/Library;${RENDEPTH_ONNXRUNTIME_DIR}/lib;${RENDEPTH_ROCM_DIR}/lib"
                INSTALL_RPATH "${CMAKE_SOURCE_DIR}/Library;${RENDEPTH_ONNXRUNTIME_DIR}/lib;${RENDEPTH_ROCM_DIR}/lib")
        if(RENDEPTH_ENABLE_ROCM)
            set_target_properties(DepthTest PROPERTIES
                    BUILD_RPATH "${CMAKE_SOURCE_DIR}/Library;${RENDEPTH_ROCM_DIR}/lib"
                    INSTALL_RPATH "${CMAKE_SOURCE_DIR}/Library;${RENDEPTH_ROCM_DIR}/lib")
            target_link_options(DepthTest PRIVATE "-Wl,--disable-new-dtags")
        endif()
    endif()

endif()

if(WIN32)
    target_link_libraries(Rendepth PUBLIC pthread)
    if(RENDEPTH_DLL_DIR)
        add_custom_command(TARGET Rendepth POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${RENDEPTH_DLL_DIR}/libgcc_s_seh-1.dll
                ${RENDEPTH_DLL_DIR}/libwinpthread-1.dll
                $<TARGET_FILE_DIR:Rendepth>)
    endif()
elseif(UNIX)
    target_link_libraries(Rendepth PUBLIC stdc++)
endif()

add_executable(QuiltTest Source/QuiltTest.cpp)
target_compile_definitions(QuiltTest PRIVATE SDL_MAIN_USE_CALLBACKS)
target_include_directories(QuiltTest PRIVATE
        ThirdParty/SDL/include ThirdParty/rapidjson/include)
target_link_libraries(QuiltTest PRIVATE SDL3::SDL3)
if(RENDEPTH_COMPILE_SHADERS)
    add_dependencies(QuiltTest QuiltTestShaders)
endif()
