cmake_minimum_required(VERSION 3.22)
project(Rendepth VERSION 3.0.0 LANGUAGES C CXX)
set(EXECUTABLE_NAME Rendepth)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# Build features are intentionally owned by this file rather than the CMake
# cache.  A developer should only need to provide machine-specific SDK paths
# (such as ONNX Runtime and cuDNN), not remember a separate set of IDE flags.
# Clear legacy cache entries so an old CLion profile cannot silently override
# these defaults.
foreach(RENDEPTH_BUILD_FEATURE IN ITEMS
        RENDEPTH_ENABLE_ONNX_RUNTIME
        RENDEPTH_ENABLE_FFMPEG
        RENDEPTH_ENABLE_ROCM
        RENDEPTH_ENABLE_CUDA
        RENDEPTH_BUILD_DEPTH_TEST
        RENDEPTH_BUILD_SUPER_TEST
        RENDEPTH_COMPILE_SHADERS)
    unset(${RENDEPTH_BUILD_FEATURE} CACHE)
endforeach()

# Windows and Linux ship a CPU base and discover optional GPU runtime packs.
set(RENDEPTH_ENABLE_ONNX_RUNTIME ON)
set(RENDEPTH_ENABLE_BLURAY ON)
set(RENDEPTH_ENABLE_DVD ON)
set(RENDEPTH_ENABLE_FFMPEG ON)
set(RENDEPTH_ENABLE_ROCM OFF)
set(RENDEPTH_ENABLE_CUDA OFF)
set(RENDEPTH_BUILD_DEPTH_TEST ON)
set(RENDEPTH_BUILD_SUPER_TEST ON)
set(RENDEPTH_COMPILE_SHADERS OFF)
if(APPLE)
    option(RENDEPTH_MAC_EXPORT_BUNDLE "Build the macOS app bundle for export" OFF)
endif()

# ONNX Runtime is not bundled, so these remain cache entries for each local
# machine or CI environment to supply.
set(RENDEPTH_FFMPEG_ROOT "" CACHE PATH
        "MSVC-compatible FFmpeg SDK root (for example, vcpkg_installed/x64-windows)")
set(RENDEPTH_ONNXRUNTIME_DIR "" CACHE PATH "ONNX Runtime installation directory")
set(RENDEPTH_CUDNN_BIN_DIR "" CACHE PATH
        "Directory containing the cuDNN runtime DLLs required by the CUDA ONNX Runtime provider")
set(RENDEPTH_ROCM_DIR "/usr/lib64/rocm" CACHE PATH "ROCm installation directory")
if(NOT WIN32)
    find_package(CURL REQUIRED)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Keep developer paths out of release diagnostics and embedded debug metadata,
# including the vendored libraries built below. Debug builds retain full paths.
foreach(RENDEPTH_PATH_ROOT IN ITEMS "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
    if(MSVC)
        add_compile_options("$<$<AND:$<NOT:$<CONFIG:Debug>>,$<COMPILE_LANGUAGE:C,CXX>>:/pathmap:${RENDEPTH_PATH_ROOT}=.>")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options("$<$<AND:$<NOT:$<CONFIG:Debug>>,$<COMPILE_LANGUAGE:C,CXX>>:-ffile-prefix-map=${RENDEPTH_PATH_ROOT}=.>")
    endif()
endforeach()
if(MSVC)
    add_link_options("$<$<NOT:$<CONFIG:Debug>>:/PDBALTPATH:%_PDB%>")
endif()

set(CMAKE_POLICY_DEFAULT_CMP0077 OLD)
set(CMAKE_POLICY_DEFAULT_CMP0095 OLD)
set(CMAKE_POLICY_DEFAULT_CMP0068 OLD)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/Binary")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/Debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/Binary")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/Binary")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}/Binary")
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
    set(CMAKE_INSTALL_RPATH "\$ORIGIN/../Library")
endif()

set(SDL_AUDIO ON)
set(SDL_CAMERA OFF)
set(SDL_VIDEO ON)
set(SDL_HIDAPI OFF)
set(SDL_JOYSTICK OFF)
set(SDL_POWER OFF)
set(SDL_HAPTIC OFF)
set(SDL_SENSOR OFF)
set(SDL_TEST_LIBRARY OFF)

set(SDLIMAGE_VENDORED ON)
# Modern image codecs are grouped behind one switch so they can be disabled
# for a lightweight test build when needed.
option(RENDEPTH_ENABLE_MODERN_IMAGE_FORMATS
        "Build AVIF, JPEG XL, and WebP image support" ON)
set(SDLIMAGE_AVIF ${RENDEPTH_ENABLE_MODERN_IMAGE_FORMATS})
set(SDLIMAGE_BMP ON)
# GIFs are treated as animated video and decoded by the FFmpeg pipeline.
set(SDLIMAGE_GIF OFF)
set(SDLIMAGE_JPG ON)
set(SDLIMAGE_JXL ${RENDEPTH_ENABLE_MODERN_IMAGE_FORMATS})
set(SDLIMAGE_LBM OFF)
set(SDLIMAGE_PCX OFF)
set(SDLIMAGE_PNG ON)
set(SDLIMAGE_PNM OFF)
set(SDLIMAGE_QOI ON)
set(SDLIMAGE_SVG OFF)
set(SDLIMAGE_TGA ON)
set(SDLIMAGE_TIF ON)
set(SDLIMAGE_WEBP ${RENDEPTH_ENABLE_MODERN_IMAGE_FORMATS})
set(SDLIMAGE_XCF OFF)
set(SDLIMAGE_XPM OFF)
set(SDLIMAGE_XV OFF)
set(SDLIMAGE_AOM OFF)

set(SDLTTF_VENDORED ON)
set(SDLTTF_FREETYPE ON)
set(SDLTTF_HARFBUZZ OFF)
set(SDLTTF_PLUTOSVG OFF)

# GLM is header-only in Rendepth. Building its optional library conflicts with
# the shared-library setting on MSVC and produces an unusable import library.
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "Build GLM's optional library" FORCE)
set(WITH_PERF_TOOL OFF)
set(USE_OWN_FFTW OFF)
set(BUILD_BENCHMARK OFF)
set(ENABLE_TESTS OFF)
set(BUILD_TESTING OFF)
set(BUILD_SHARED_LIBS ON)

if(WIN32)
    if(MSVC)
        # SDL and its vendored dependencies compile sources in parallel. /FS
        # serializes PDB writes so MSVC builds remain reliable in CI as well.
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /O2 /FS")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /O2 /FS")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -mavx -w -O2")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static-libstdc++ -mavx -w -O2")
    endif()
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
    # The Windows DXC source build requires ATL, which is not present in the
    # standard Visual Studio Build Tools installation. Use the standalone DXC
    # executable for the final HLSL -> DXIL conversion instead.
    if(WIN32)
        set(SDLSHADERCROSS_DXC OFF CACHE BOOL "" FORCE)
        find_program(RENDEPTH_DXC_EXECUTABLE NAMES dxc dxc.exe REQUIRED)
    else()
        set(SDLSHADERCROSS_DXC ON CACHE BOOL "" FORCE)
    endif()
    set(SDLSHADERCROSS_SHARED OFF CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_STATIC ON CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_VENDORED ON CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_CLI ON CACHE BOOL "" FORCE)
    set(SDLSHADERCROSS_INSTALL OFF CACHE BOOL "" FORCE)
    # DXC's bundled LLVM/Clang targets contain dependency cycles that can only
    # be represented by static libraries. Scope shared libraries off while
    # configuring shadercross when the bundled DXC is used; the application's
    # SDL libraries remain shared.
    if(NOT WIN32)
        set(RENDEPTH_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
        set(BUILD_SHARED_LIBS OFF)
    endif()
    add_subdirectory(ThirdParty/SDL_shadercross EXCLUDE_FROM_ALL)
    if(NOT WIN32)
        set(BUILD_SHARED_LIBS ${RENDEPTH_BUILD_SHARED_LIBS})
    endif()

    find_program(RENDEPTH_GLSLANG_VALIDATOR glslangValidator REQUIRED)
    set(RENDEPTH_SHADER_TEMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/Shaders/Temp")
    set(RENDEPTH_SHADER_OUTPUTS)
    set(QUILTTEST_SHADER_OUTPUTS)
    foreach(shader IN ITEMS
            Image.vert Image.frag DepthRefine.frag VideoYUV.vert VideoYUV.frag
            Lanczos.frag
            Icon.vert Icon.frag Sprite.vert Sprite.frag Interlacer.frag
            QuiltTest.vert QuiltTest.frag)
        set(shader_spv "${CMAKE_SOURCE_DIR}/Shaders/Compiled/${shader}.spv")
        set(shader_dxil "${CMAKE_SOURCE_DIR}/Shaders/Compiled/${shader}.dxil")
        set(shader_msl "${CMAKE_SOURCE_DIR}/Shaders/Compiled/${shader}.msl")
        set(shader_hlsl "${RENDEPTH_SHADER_TEMP_DIR}/${shader}.hlsl")
        if(WIN32)
            if(shader MATCHES "\\.vert$")
                set(shader_dxil_profile vs_6_0)
            else()
                set(shader_dxil_profile ps_6_0)
            endif()
            set(shader_dxil_command
                    COMMAND "${RENDEPTH_DXC_EXECUTABLE}" -T ${shader_dxil_profile} -E main
                            -Fo "${shader_dxil}" "${shader_hlsl}")
        else()
            set(shader_dxil_command
                    COMMAND $<TARGET_FILE:shadercross> "${shader_hlsl}"
                            -s HLSL -d DXIL -o "${shader_dxil}")
        endif()
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
                ${shader_dxil_command}
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
    if(WIN32)
        add_custom_target(RendepthShadercross
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_FILE:shadercross>"
                        "$<TARGET_FILE_DIR:Rendepth>/$<TARGET_FILE_NAME:shadercross>"
                DEPENDS shadercross)
        add_dependencies(Shaders RendepthShadercross)
    endif()
endif()

if(WIN32)
    if(NOT MSVC)
        add_compile_options(-Wl,-Bstatic)
    endif()
    add_executable(Rendepth WIN32 Assets/Rendepth.rc Source/Main.cpp)
elseif(APPLE)
    # Bundle layout and library fixup are only needed for the export build.
    # Use a dedicated flag so a cached packaging setting cannot change the
    # executable built by an existing CLion profile.
    if(NOT RENDEPTH_MAC_EXPORT_BUNDLE)
        add_executable(Rendepth Source/Main.cpp)
    else()
        set(MACOSX_BUNDLE_ICON_FILE Rendepth.icns)
        file(GLOB RENDEPTH_MAC_SHADERS CONFIGURE_DEPENDS
                "${CMAKE_SOURCE_DIR}/Shaders/Compiled/*.msl")
        add_executable(Rendepth MACOSX_BUNDLE ${CMAKE_CURRENT_SOURCE_DIR}/Assets/Rendepth.icns
                ${CMAKE_SOURCE_DIR}/Assets/AppIcons.png ${CMAKE_SOURCE_DIR}/Assets/CircleIcon.png
                ${CMAKE_SOURCE_DIR}/Assets/Lato.ttf ${CMAKE_SOURCE_DIR}/Assets/NotoSans.ttf
                ${CMAKE_SOURCE_DIR}/Assets/Rendepth.png
                ${RENDEPTH_MAC_SHADERS}
                ${CMAKE_SOURCE_DIR}/Legal/RENDEPTH_APP_LICENSE
                ${CMAKE_BINARY_DIR}/generated/Legal/THIRD_PARTY_LICENSING)
        set_target_properties(Rendepth PROPERTIES
                MACOSX_BUNDLE_GUI_IDENTIFIER com.outmode.rendepth
                MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
                MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
                MACOSX_BUNDLE_BUNDLE_NAME Rendepth)
        if(NOT RENDEPTH_MAC_BUNDLE_OUTPUT_DIRECTORY)
            set(RENDEPTH_MAC_BUNDLE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/output" CACHE PATH
                    "Output directory for the macOS export app bundle" FORCE)
        endif()
        set_target_properties(Rendepth PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${RENDEPTH_MAC_BUNDLE_OUTPUT_DIRECTORY}"
                RUNTIME_OUTPUT_DIRECTORY_RELEASE "${RENDEPTH_MAC_BUNDLE_OUTPUT_DIRECTORY}"
                RUNTIME_OUTPUT_DIRECTORY_DEBUG "${RENDEPTH_MAC_BUNDLE_OUTPUT_DIRECTORY}"
                RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${RENDEPTH_MAC_BUNDLE_OUTPUT_DIRECTORY}"
                RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${RENDEPTH_MAC_BUNDLE_OUTPUT_DIRECTORY}")
        set_source_files_properties(
                ${CMAKE_SOURCE_DIR}/Assets/Rendepth.icns
                PROPERTIES
                MACOSX_PACKAGE_LOCATION "Resources")
        set_source_files_properties(
                ${CMAKE_SOURCE_DIR}/Assets/AppIcons.png ${CMAKE_SOURCE_DIR}/Assets/CircleIcon.png
                ${CMAKE_SOURCE_DIR}/Assets/Lato.ttf ${CMAKE_SOURCE_DIR}/Assets/NotoSans.ttf
                ${CMAKE_SOURCE_DIR}/Assets/Rendepth.png
                PROPERTIES
                MACOSX_PACKAGE_LOCATION Assets)
        set_source_files_properties(
                ${RENDEPTH_MAC_SHADERS}
                PROPERTIES
                MACOSX_PACKAGE_LOCATION Shaders/Compiled)
        set_source_files_properties(
                ${CMAKE_SOURCE_DIR}/Legal/RENDEPTH_APP_LICENSE
                ${CMAKE_BINARY_DIR}/generated/Legal/THIRD_PARTY_LICENSING
                PROPERTIES
                MACOSX_PACKAGE_LOCATION Legal)
    endif()
elseif(UNIX)
    add_executable(Rendepth Source/Main.cpp)
endif()

if(RENDEPTH_COMPILE_SHADERS)
    add_dependencies(Rendepth Shaders)
endif()

target_compile_definitions(Rendepth PUBLIC
        SDL_MAIN_USE_CALLBACKS
        RENDEPTH_ENABLE_MODERN_IMAGE_FORMATS=$<BOOL:${RENDEPTH_ENABLE_MODERN_IMAGE_FORMATS}>)

target_sources(Rendepth PUBLIC Source/Main.cpp Source/Core.cpp Source/Image.cpp Source/VideoPlayer.cpp Source/AudioCdReader.cpp Source/DiscSource.cpp Source/MediaOpenDialog.cpp Source/BlurayReader.cpp Source/DvdReader.cpp Source/DiscTitleMenu.cpp
		Source/VideoDepthProcessor.cpp
		Source/GettingStarted.cpp
		Source/ScreenCapture.cpp Source/BrowserBridge.cpp
        Source/Style.cpp Source/Utils.cpp Source/DepthEstimator.cpp
        Source/ModelDownloader.cpp Source/SuperResolution.cpp Source/CubeViCalibration.cpp)

target_include_directories(Rendepth PUBLIC
        ThirdParty/glm ThirdParty/SDL/include ThirdParty/SDL_image/include
        ThirdParty/SDL_ttf/include ThirdParty/SDL_shadercross/include
        ThirdParty/rapidjson/include)

if(UNIX AND NOT APPLE)
    target_link_directories(Rendepth PUBLIC "\${ORIGIN}/../Library")
endif()

target_link_libraries(Rendepth PUBLIC
        SDL3::SDL3 SDL3_ttf::SDL3_ttf SDL3_image::SDL3_image
        glm)
if(RENDEPTH_ENABLE_MODERN_IMAGE_FORMATS)
    target_link_libraries(Rendepth PRIVATE libjxl::libjxl)
endif()

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
    target_link_libraries(Rendepth PRIVATE PkgConfig::GTK3)
    target_compile_definitions(Rendepth PRIVATE RENDEPTH_GTK_MEDIA_DIALOG)
endif()

if(WIN32)
    target_link_libraries(Rendepth PUBLIC winhttp ole32 shell32 uuid bcrypt crypt32)
    add_executable(FirefoxNativeHost EXCLUDE_FROM_ALL Source/FirefoxNativeHost.cpp)
    add_executable(ChromeNativeHost EXCLUDE_FROM_ALL Source/FirefoxNativeHost.cpp)
    target_compile_definitions(ChromeNativeHost PRIVATE RENDEPTH_CHROME_NATIVE_HOST=1)
    add_executable(BrowserBridgeTest EXCLUDE_FROM_ALL
        Source/BrowserBridgeWindowsTest.cpp Source/BrowserBridge.cpp)
    target_include_directories(BrowserBridgeTest PRIVATE ThirdParty/rapidjson/include)
    if(TARGET zlibstatic)
        set(RENDEPTH_PACK_ZLIB zlibstatic)
    else()
        find_package(ZLIB REQUIRED)
        set(RENDEPTH_PACK_ZLIB ZLIB::ZLIB)
    endif()
    target_sources(Rendepth PRIVATE Source/RuntimePackDownloader.cpp
        ThirdParty/SDL_image/external/zlib/contrib/minizip/unzip.c
        ThirdParty/SDL_image/external/zlib/contrib/minizip/ioapi.c
        ThirdParty/SDL_image/external/zlib/contrib/minizip/iowin32.c)
    target_include_directories(Rendepth PRIVATE
        ThirdParty/SDL_image/external/zlib/contrib/minizip)
    target_link_libraries(Rendepth PRIVATE ${RENDEPTH_PACK_ZLIB})
    add_executable(RuntimePackDownloaderSmoke EXCLUDE_FROM_ALL
        Source/RuntimePackDownloaderSmoke.cpp Source/RuntimePackDownloader.cpp
        ThirdParty/SDL_image/external/zlib/contrib/minizip/unzip.c
        ThirdParty/SDL_image/external/zlib/contrib/minizip/ioapi.c
        ThirdParty/SDL_image/external/zlib/contrib/minizip/iowin32.c)
    set_target_properties(RuntimePackDownloaderSmoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/tests"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/tests")
    target_include_directories(RuntimePackDownloaderSmoke PRIVATE
        ThirdParty/SDL_image/external/zlib/contrib/minizip)
    target_link_libraries(RuntimePackDownloaderSmoke PRIVATE ${RENDEPTH_PACK_ZLIB} winhttp bcrypt)
else()
    target_link_libraries(Rendepth PUBLIC CURL::libcurl)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND RENDEPTH_ENABLE_ONNX_RUNTIME)
        find_package(OpenSSL REQUIRED COMPONENTS Crypto)
        target_sources(Rendepth PRIVATE Source/RuntimePackDownloaderLinux.cpp)
        target_link_libraries(Rendepth PRIVATE OpenSSL::Crypto)
    endif()
endif()

# Public Lemon Squeezy identifiers only; never put seller API credentials here.
# Owner-supplied IDs; confirm against a test activation before release.
set(RENDEPTH_LICENSE_STORE_ID "474981" CACHE STRING "Lemon Squeezy store ID (0 disables activation)")
set(RENDEPTH_LICENSE_PRODUCT_ID "1390849" CACHE STRING "Rendepth Pro product ID")
# CMake keeps old cache entries when defaults change. Migrate the previous Pro ID.
if(RENDEPTH_LICENSE_PRODUCT_ID STREQUAL "1373415")
    set(RENDEPTH_LICENSE_PRODUCT_ID "1390849" CACHE STRING "Rendepth Pro product ID" FORCE)
endif()
set(RENDEPTH_LICENSE_VARIANT_ID "0" CACHE STRING "Optional Pro variant ID (0 accepts all product variants)")
# Current checkout is owner-supplied for Test Mode; verify live settings before release.
set(RENDEPTH_LICENSE_PURCHASE_URL "https://shop.rendepth.com/" CACHE STRING "Rendepth Pro HTTPS checkout URL")
# Community support is an application-owned link. Drop the old cached email URL
# so existing build profiles receive the current invite on reconfiguration.
unset(RENDEPTH_LICENSE_SUPPORT_URL CACHE)
set(RENDEPTH_LICENSE_SUPPORT_URL "https://discord.gg/wWGNtd8KS")
foreach(field STORE_ID PRODUCT_ID VARIANT_ID)
    if(NOT "${RENDEPTH_LICENSE_${field}}" MATCHES "^(0|[1-9][0-9]*)$")
        message(FATAL_ERROR "RENDEPTH_LICENSE_${field} must be a non-negative integer")
    endif()
    string(LENGTH "${RENDEPTH_LICENSE_${field}}" id_length)
    if(id_length GREATER 15)
        message(FATAL_ERROR "RENDEPTH_LICENSE_${field} is too long")
    endif()
endforeach()
foreach(field PURCHASE_URL SUPPORT_URL)
    set(license_url_scheme "^https://")
    if(NOT "${RENDEPTH_LICENSE_${field}}" STREQUAL "" AND
       (NOT "${RENDEPTH_LICENSE_${field}}" MATCHES "${license_url_scheme}" OR
        "${RENDEPTH_LICENSE_${field}}" MATCHES "[\"\\\\\n\r;]"))
        message(FATAL_ERROR "RENDEPTH_LICENSE_${field} must be a plain HTTPS URL")
    endif()
endforeach()
configure_file(Source/Licensing/LicenseConfig.h.in generated/LicenseConfig.h @ONLY)
find_package(Threads REQUIRED)
add_library(RendepthLicensing STATIC
    Source/Licensing/LemonSqueezyClient.cpp Source/Licensing/LicenseStorage.cpp
    Source/Licensing/LicenseManager.cpp Source/SettingsFile.cpp)
target_include_directories(RendepthLicensing PUBLIC Source PRIVATE ThirdParty/rapidjson/include)
target_link_libraries(RendepthLicensing PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(RendepthLicensing PRIVATE winhttp)
else()
    target_link_libraries(RendepthLicensing PRIVATE CURL::libcurl)
endif()
target_link_libraries(Rendepth PRIVATE RendepthLicensing)
target_include_directories(Rendepth PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_sources(Rendepth PRIVATE Source/Licensing/LicenseService.cpp)
if(WIN32)
    target_sources(Rendepth PRIVATE Source/Licensing/LicenseDialogWin.cpp)
elseif(APPLE)
    enable_language(OBJCXX)
    set_property(TARGET Rendepth PROPERTY OBJCXX_STANDARD 20)
    target_sources(Rendepth PRIVATE Source/Licensing/LicenseDialogMac.mm)
    target_sources(Rendepth PRIVATE Source/MediaOpenDialogMac.mm)
    set_source_files_properties(Source/MediaOpenDialogMac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    set_source_files_properties(Source/Licensing/LicenseDialogMac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(Rendepth PRIVATE "-framework Cocoa")
else()
    target_sources(Rendepth PRIVATE Source/Licensing/LicenseDialogGtk.cpp)
endif()
add_executable(LicensingTest EXCLUDE_FROM_ALL Source/Licensing/LicensingTest.cpp)
target_link_libraries(LicensingTest PRIVATE RendepthLicensing)
if(UNIX AND NOT APPLE)
    add_executable(LicenseDialogTest EXCLUDE_FROM_ALL
        Source/Licensing/LicenseDialogTest.cpp Source/Licensing/LicenseDialogGtk.cpp)
    target_link_libraries(LicenseDialogTest PRIVATE SDL3::SDL3 PkgConfig::GTK3)
endif()

if(RENDEPTH_ENABLE_FFMPEG)
    if(WIN32)
        if(NOT MSVC)
            message(FATAL_ERROR
                    "Windows FFmpeg playback requires an MSVC build and an MSVC-compatible FFmpeg SDK.")
        endif()
        if(NOT RENDEPTH_FFMPEG_ROOT)
            message(FATAL_ERROR
                    "RENDEPTH_FFMPEG_ROOT must point to an MSVC-compatible FFmpeg SDK. "
                    "For vcpkg, use its installed/x64-windows directory.")
        endif()

        find_path(RENDEPTH_FFMPEG_INCLUDE_DIR
                NAMES libavcodec/avcodec.h
                PATHS "${RENDEPTH_FFMPEG_ROOT}/include"
                NO_DEFAULT_PATH)
        if(NOT RENDEPTH_FFMPEG_INCLUDE_DIR)
            message(FATAL_ERROR
                    "Could not find FFmpeg headers under ${RENDEPTH_FFMPEG_ROOT}/include.")
        endif()

        foreach(ffmpeg_library IN ITEMS avformat avcodec swresample swscale avutil)
            string(TOUPPER "${ffmpeg_library}" ffmpeg_library_upper)
            find_library(RENDEPTH_FFMPEG_${ffmpeg_library_upper}_RELEASE
                    NAMES ${ffmpeg_library} lib${ffmpeg_library}
                    PATHS "${RENDEPTH_FFMPEG_ROOT}/lib"
                    NO_DEFAULT_PATH)
            find_library(RENDEPTH_FFMPEG_${ffmpeg_library_upper}_DEBUG
                    NAMES ${ffmpeg_library} lib${ffmpeg_library}
                    PATHS "${RENDEPTH_FFMPEG_ROOT}/debug/lib"
                    NO_DEFAULT_PATH)
            if(NOT RENDEPTH_FFMPEG_${ffmpeg_library_upper}_RELEASE OR
                    NOT RENDEPTH_FFMPEG_${ffmpeg_library_upper}_DEBUG)
                message(FATAL_ERROR
                        "Could not find release and debug ${ffmpeg_library} import libraries under "
                        "${RENDEPTH_FFMPEG_ROOT}.")
            endif()
            add_library(RendepthFFmpeg_${ffmpeg_library} UNKNOWN IMPORTED GLOBAL)
            set_target_properties(RendepthFFmpeg_${ffmpeg_library} PROPERTIES
                    IMPORTED_LOCATION_DEBUG
                        "${RENDEPTH_FFMPEG_${ffmpeg_library_upper}_DEBUG}"
                    IMPORTED_LOCATION_RELEASE
                        "${RENDEPTH_FFMPEG_${ffmpeg_library_upper}_RELEASE}"
                    IMPORTED_LOCATION_RELWITHDEBINFO
                        "${RENDEPTH_FFMPEG_${ffmpeg_library_upper}_RELEASE}"
                    IMPORTED_LOCATION_MINSIZEREL
                        "${RENDEPTH_FFMPEG_${ffmpeg_library_upper}_RELEASE}"
                    INTERFACE_INCLUDE_DIRECTORIES "${RENDEPTH_FFMPEG_INCLUDE_DIR}")
        endforeach()

        if(NOT EXISTS "${RENDEPTH_FFMPEG_ROOT}/bin" OR
                NOT EXISTS "${RENDEPTH_FFMPEG_ROOT}/debug/bin")
            message(FATAL_ERROR
                    "FFmpeg runtime DLL directories were not found under ${RENDEPTH_FFMPEG_ROOT}.")
        endif()
        add_custom_command(TARGET Rendepth POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "$<IF:$<CONFIG:Debug>,${RENDEPTH_FFMPEG_ROOT}/debug/bin,${RENDEPTH_FFMPEG_ROOT}/bin>"
                        "$<TARGET_FILE_DIR:Rendepth>")
        install(DIRECTORY "${RENDEPTH_FFMPEG_ROOT}/bin/"
                DESTINATION libexec/rendepth/Binary
                FILES_MATCHING PATTERN "*.dll")
    else()
        if(NOT EXISTS "${CMAKE_SOURCE_DIR}/ThirdParty/FFmpeg/configure")
            message(FATAL_ERROR
                    "FFmpeg is missing. Run: git submodule update --init ThirdParty/FFmpeg")
        endif()

        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(DAV1D QUIET IMPORTED_TARGET "dav1d >= 0.5.0")
        endif()
        set(RENDEPTH_FFMPEG_COMMON_DECODERS
                cinepak,dnxhd,ffv1,gif,h264,hevc,huffyuv,mjpeg,mpeg1video,mpeg2video,mpeg4,msmpeg4v1,msmpeg4v2,msmpeg4v3,prores,rawvideo,theora,vc1,vp8,vp9,wmv1,wmv2,wmv3,aac,aac_latm,ac3,alac,dca,eac3,flac,mp1,mp2,mp3,opus,pcm_alaw,pcm_bluray,pcm_dvd,pcm_f32be,pcm_f32le,pcm_f64be,pcm_f64le,pcm_mulaw,pcm_s16be,pcm_s16le,pcm_s24be,pcm_s24le,pcm_s32be,pcm_s32le,pcm_u8,truehd,vorbis,wavpack,wmalossless,wmapro,wmav1,wmav2,wmavoice,subrip,ass,mov_text,webvtt,pgssub,dvdsub)
        set(RENDEPTH_FFMPEG_COMMON_PARSERS
                aac,aac_latm,ac3,dca,flac,gif,h264,hevc,mlp,mpegaudio,mpegvideo,opus,vc1,vorbis)
        if(DAV1D_FOUND)
            set(RENDEPTH_FFMPEG_DAV1D_OPTIONS
                    --enable-libdav1d
                    --enable-decoder=av1,libdav1d,${RENDEPTH_FFMPEG_COMMON_DECODERS})
            set(RENDEPTH_FFMPEG_DAV1D_LINK PkgConfig::DAV1D)
            message(STATUS "Rendepth: using system dav1d for FFmpeg AV1 decoding")
        else()
            set(RENDEPTH_FFMPEG_DAV1D_OPTIONS
                    --enable-decoder=av1,${RENDEPTH_FFMPEG_COMMON_DECODERS})
            message(STATUS "Rendepth: system dav1d not found; using FFmpeg's native AV1 decoder")
        endif()
        include(ExternalProject)
        find_program(RENDEPTH_FFMPEG_MAKE NAMES gmake make REQUIRED)
        set(RENDEPTH_FFMPEG_VAAPI_OPTIONS)
        if(PkgConfig_FOUND)
            pkg_check_modules(RENDEPTH_VAAPI QUIET libva)
            if(RENDEPTH_VAAPI_FOUND)
                pkg_check_modules(RENDEPTH_VAAPI_DRM QUIET libva-drm)
                list(APPEND RENDEPTH_FFMPEG_VAAPI_OPTIONS
                        --enable-vaapi
                        --enable-hwaccel=h264_vaapi
                        --enable-hwaccel=hevc_vaapi
                        --enable-hwaccel=vp8_vaapi
                        --enable-hwaccel=vp9_vaapi)
                message(STATUS "Rendepth: enabling optional FFmpeg VA-API decoding")
            endif()
        endif()
        set(RENDEPTH_FFMPEG_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg")
        set(RENDEPTH_FFMPEG_BUILD "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-build")
        set(RENDEPTH_FFMPEG_LIBRARY_DIR "${RENDEPTH_FFMPEG_PREFIX}/lib")
        set(RENDEPTH_FFMPEG_INCLUDE_DIR "${RENDEPTH_FFMPEG_PREFIX}/include")
        file(MAKE_DIRECTORY "${RENDEPTH_FFMPEG_INCLUDE_DIR}")

        ExternalProject_Add(RendepthFFmpeg
            SOURCE_DIR "${CMAKE_SOURCE_DIR}/ThirdParty/FFmpeg"
            BINARY_DIR "${RENDEPTH_FFMPEG_BUILD}"
            CONFIGURE_COMMAND
                "${CMAKE_SOURCE_DIR}/ThirdParty/FFmpeg/configure"
                "--cc=${CMAKE_C_COMPILER}"
                "--host-cc=${CMAKE_C_COMPILER}"
                "--pkg-config=${PKG_CONFIG_EXECUTABLE}"
                "--prefix=${RENDEPTH_FFMPEG_PREFIX}"
                --disable-programs
                --disable-doc
                --disable-debug
                --disable-network
                --disable-autodetect
                --disable-avdevice
                --disable-avfilter
                --disable-iconv
                --disable-shared
                --enable-static
                --enable-pic
                --enable-pthreads
                --disable-everything
                --enable-avcodec
                --enable-avformat
                --enable-avutil
                --enable-swresample
                --enable-swscale
                --enable-protocol=file
                --enable-demuxer=aac,avi,asf,flac,gif,matroska,mp3,mov,mpegps,mpegts,mpegvideo,ogg,wav
                --enable-parser=${RENDEPTH_FFMPEG_COMMON_PARSERS}
                ${RENDEPTH_FFMPEG_DAV1D_OPTIONS}
                ${RENDEPTH_FFMPEG_VAAPI_OPTIONS}
            BUILD_COMMAND "${RENDEPTH_FFMPEG_MAKE}"
            INSTALL_COMMAND "${RENDEPTH_FFMPEG_MAKE}" install
            BUILD_BYPRODUCTS
                "${RENDEPTH_FFMPEG_LIBRARY_DIR}/libavformat.a"
                "${RENDEPTH_FFMPEG_LIBRARY_DIR}/libavcodec.a"
                "${RENDEPTH_FFMPEG_LIBRARY_DIR}/libswresample.a"
                "${RENDEPTH_FFMPEG_LIBRARY_DIR}/libswscale.a"
                "${RENDEPTH_FFMPEG_LIBRARY_DIR}/libavutil.a"
            UPDATE_COMMAND ""
            TEST_COMMAND "")

        foreach(ffmpeg_library IN ITEMS avformat avcodec swresample swscale avutil)
            add_library(RendepthFFmpeg_${ffmpeg_library} STATIC IMPORTED GLOBAL)
            set_target_properties(RendepthFFmpeg_${ffmpeg_library} PROPERTIES
                IMPORTED_LOCATION
                    "${RENDEPTH_FFMPEG_LIBRARY_DIR}/lib${ffmpeg_library}.a"
                INTERFACE_INCLUDE_DIRECTORIES "${RENDEPTH_FFMPEG_INCLUDE_DIR}")
            add_dependencies(RendepthFFmpeg_${ffmpeg_library} RendepthFFmpeg)
        endforeach()
        add_dependencies(Rendepth RendepthFFmpeg)
    endif()
    target_compile_definitions(Rendepth PUBLIC RENDEPTH_ENABLE_FFMPEG)
    include(Packaging/Disc.cmake)
    target_link_libraries(Rendepth PUBLIC
        RendepthFFmpeg_avformat RendepthFFmpeg_avcodec
        RendepthFFmpeg_swresample
        RendepthFFmpeg_swscale RendepthFFmpeg_avutil
        ${RENDEPTH_FFMPEG_DAV1D_LINK} ${CMAKE_DL_LIBS})
    if(UNIX)
        target_link_libraries(Rendepth PUBLIC m pthread)
    endif()
    if(RENDEPTH_VAAPI_FOUND)
        target_link_directories(Rendepth PUBLIC ${RENDEPTH_VAAPI_LIBRARY_DIRS})
        target_link_libraries(Rendepth PUBLIC ${RENDEPTH_VAAPI_LIBRARIES})
        if(RENDEPTH_VAAPI_DRM_FOUND)
            target_link_directories(Rendepth PUBLIC ${RENDEPTH_VAAPI_DRM_LIBRARY_DIRS})
            target_link_libraries(Rendepth PUBLIC ${RENDEPTH_VAAPI_DRM_LIBRARIES})
        endif()
    endif()
endif()

if(RENDEPTH_ENABLE_FFMPEG)
    target_sources(Rendepth PRIVATE Source/MvcDecoder.cpp)
    if(WIN32 AND MSVC)
        include(Packaging/Windows/Mvc.cmake)
    elseif(RENDEPTH_HAVE_BLURAY AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        if(NOT EXISTS "${CMAKE_SOURCE_DIR}/ThirdParty/SyLC/edge264/src/edge264.c")
            message(FATAL_ERROR "MVC decoder missing. Run: git submodule update --init ThirdParty/SyLC")
        endif()
        find_package(Threads REQUIRED)
        add_library(RendepthMVC STATIC ThirdParty/SyLC/edge264/src/edge264.c)
        target_include_directories(RendepthMVC PUBLIC ThirdParty/SyLC/edge264)
        set_target_properties(RendepthMVC PROPERTIES C_STANDARD 17 C_EXTENSIONS ON)
        target_compile_options(RendepthMVC PRIVATE -std=gnu17 -O3 -fno-strict-aliasing -flax-vector-conversions)
        # Build upstream's SIMD variants separately. edge264 selects one at
        # runtime, so the executable still works on baseline x86-64 CPUs.
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" AND NOT APPLE)
            foreach(variant IN ITEMS v2 v3)
                add_library(RendepthMVC_${variant} OBJECT ThirdParty/SyLC/edge264/src/edge264_headers.c)
                set_target_properties(RendepthMVC_${variant} PROPERTIES C_STANDARD 17 C_EXTENSIONS ON)
                target_compile_options(RendepthMVC_${variant} PRIVATE
                    -std=gnu17 -O3 -fno-strict-aliasing -flax-vector-conversions
                    -march=x86-64-${variant} "-DADD_VARIANT(f)=f##_${variant}")
                target_sources(RendepthMVC PRIVATE $<TARGET_OBJECTS:RendepthMVC_${variant}>)
            endforeach()
            target_compile_definitions(RendepthMVC PRIVATE HAS_X86_64_V2 HAS_X86_64_V3)
        endif()
        target_link_libraries(RendepthMVC PUBLIC Threads::Threads)
        target_link_libraries(Rendepth PUBLIC RendepthMVC)
        target_compile_definitions(Rendepth PUBLIC RENDEPTH_ENABLE_MVC)
    endif()
endif()

if(RENDEPTH_ENABLE_ONNX_RUNTIME AND WIN32)
    include(Packaging/Windows/Inference.cmake)
elseif(RENDEPTH_ENABLE_ONNX_RUNTIME AND UNIX AND NOT APPLE)
    include(Packaging/Linux/Inference.cmake)
elseif(RENDEPTH_ENABLE_ONNX_RUNTIME)
    if(NOT RENDEPTH_ENABLE_ROCM AND NOT RENDEPTH_ONNXRUNTIME_DIR)
        message(FATAL_ERROR "RENDEPTH_ONNXRUNTIME_DIR must point to an ONNX Runtime installation")
    endif()
    if(RENDEPTH_ENABLE_ROCM)
        find_path(RENDEPTH_ONNXRUNTIME_INCLUDE_DIR
                NAMES onnxruntime_cxx_api.h
                PATHS "${RENDEPTH_ROCM_DIR}/include/onnxruntime"
                      "/usr/include/onnxruntime"
                NO_DEFAULT_PATH)
        if(NOT RENDEPTH_ONNXRUNTIME_INCLUDE_DIR)
            message(FATAL_ERROR
                    "Could not find ONNX Runtime headers matching the ROCm installation")
        endif()
    else()
        set(RENDEPTH_ONNXRUNTIME_INCLUDE_DIR "${RENDEPTH_ONNXRUNTIME_DIR}/include")
        if(EXISTS "${RENDEPTH_ONNXRUNTIME_DIR}/include/onnxruntime/onnxruntime_cxx_api.h")
            set(RENDEPTH_ONNXRUNTIME_INCLUDE_DIR
                    "${RENDEPTH_ONNXRUNTIME_DIR}/include/onnxruntime")
        endif()
    endif()
    if(APPLE AND NOT EXISTS "${RENDEPTH_ONNXRUNTIME_INCLUDE_DIR}/coreml_provider_factory.h")
        message(FATAL_ERROR "The macOS ONNX Runtime SDK must include coreml_provider_factory.h")
    endif()
    target_compile_definitions(Rendepth PUBLIC RENDEPTH_ENABLE_ONNX_RUNTIME)
    if(APPLE)
        target_compile_definitions(Rendepth PUBLIC RENDEPTH_ENABLE_COREML)
    endif()
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
        file(GLOB RENDEPTH_ONNXRUNTIME_RUNTIME_DLLS
                "${RENDEPTH_ONNXRUNTIME_DIR}/lib/*.dll"
                "${RENDEPTH_ONNXRUNTIME_DIR}/bin/*.dll")
        if(NOT RENDEPTH_ONNXRUNTIME_RUNTIME_DLLS)
            message(FATAL_ERROR "Could not find ONNX Runtime DLLs under ${RENDEPTH_ONNXRUNTIME_DIR}")
        endif()
        set(RENDEPTH_CUDA_RUNTIME_DLLS)
        if(RENDEPTH_ENABLE_CUDA)
            if(NOT EXISTS "${RENDEPTH_CUDNN_BIN_DIR}/cudnn64_9.dll")
                message(FATAL_ERROR
                        "RENDEPTH_CUDNN_BIN_DIR must contain cudnn64_9.dll when RENDEPTH_ENABLE_CUDA=ON")
            endif()
            file(GLOB RENDEPTH_CUDA_RUNTIME_DLLS "${RENDEPTH_CUDNN_BIN_DIR}/*.dll")
            find_package(CUDAToolkit QUIET)
            set(RENDEPTH_CUDA_BIN_DIRS)
            if(CUDAToolkit_BIN_DIR)
                list(APPEND RENDEPTH_CUDA_BIN_DIRS "${CUDAToolkit_BIN_DIR}")
            endif()
            if(DEFINED ENV{CUDA_PATH})
                list(APPEND RENDEPTH_CUDA_BIN_DIRS "$ENV{CUDA_PATH}/bin")
            endif()
            if(RENDEPTH_CUDA_BIN_DIRS)
                list(REMOVE_DUPLICATES RENDEPTH_CUDA_BIN_DIRS)
            endif()
            set(RENDEPTH_CUDA_TOOLKIT_DLLS)
            foreach(cuda_bin_dir IN LISTS RENDEPTH_CUDA_BIN_DIRS)
                if(EXISTS "${cuda_bin_dir}")
                    file(GLOB _cuda_dlls
                            "${cuda_bin_dir}/cublas64_*.dll"
                            "${cuda_bin_dir}/cublasLt64_*.dll"
                            "${cuda_bin_dir}/cudart64_*.dll"
                            "${cuda_bin_dir}/cufft64_*.dll"
                            "${cuda_bin_dir}/cufftw64_*.dll"
                            "${cuda_bin_dir}/curand64_*.dll"
                            "${cuda_bin_dir}/cusolver64_*.dll"
                            "${cuda_bin_dir}/cusparse64_*.dll")
                    list(APPEND RENDEPTH_CUDA_TOOLKIT_DLLS ${_cuda_dlls})
                endif()
            endforeach()
            if(RENDEPTH_CUDA_TOOLKIT_DLLS)
                list(REMOVE_DUPLICATES RENDEPTH_CUDA_TOOLKIT_DLLS)
            endif()
            list(APPEND RENDEPTH_CUDA_RUNTIME_DLLS ${RENDEPTH_CUDA_TOOLKIT_DLLS})
        endif()
        set(RENDEPTH_RUNTIME_DLLS
                ${RENDEPTH_ONNXRUNTIME_RUNTIME_DLLS}
                ${RENDEPTH_CUDA_RUNTIME_DLLS})
        add_custom_command(TARGET Rendepth POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${RENDEPTH_RUNTIME_DLLS}
                        "$<TARGET_FILE_DIR:Rendepth>")
    elseif(APPLE)
        target_link_libraries(Rendepth PUBLIC "${RENDEPTH_ONNXRUNTIME_DIR}/lib/libonnxruntime.dylib")
    else()
        target_link_libraries(Rendepth PUBLIC "${RENDEPTH_ONNXRUNTIME_DIR}/lib/libonnxruntime.so")
    endif()
    if(RENDEPTH_ENABLE_CUDA)
        target_compile_definitions(Rendepth PUBLIC RENDEPTH_ENABLE_CUDA)
    endif()
    set_target_properties(Rendepth PROPERTIES
            SKIP_BUILD_RPATH FALSE
            BUILD_RPATH "${CMAKE_SOURCE_DIR}/Library;${RENDEPTH_ONNXRUNTIME_DIR}/lib"
            INSTALL_RPATH "${CMAKE_INSTALL_RPATH}")
    if(APPLE)
        set_target_properties(Rendepth PROPERTIES BUILD_WITH_INSTALL_RPATH FALSE)
    endif()
    if(RENDEPTH_ENABLE_ROCM)
        set_target_properties(Rendepth PROPERTIES
                BUILD_RPATH "${CMAKE_SOURCE_DIR}/Library;${RENDEPTH_ROCM_DIR}/lib"
                INSTALL_RPATH "\$ORIGIN/../Library;${RENDEPTH_ROCM_DIR}/lib")
        target_link_options(Rendepth PRIVATE "-Wl,--disable-new-dtags")
        add_custom_command(TARGET Rendepth POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E rm -f
                "${CMAKE_SOURCE_DIR}/Library/libonnxruntime.so.1"
                COMMAND ${CMAKE_COMMAND} -E create_symlink
                "${RENDEPTH_ROCM_ONNXRUNTIME_LIBRARY}"
                "${CMAKE_SOURCE_DIR}/Library/libonnxruntime.so.1")
    endif()

    if(RENDEPTH_BUILD_DEPTH_TEST)
        # Development-only test executable; keep it out of the default and
        # packaging builds. It can still be built explicitly when requested.
        add_executable(DepthTest EXCLUDE_FROM_ALL Source/DepthTest.cpp Source/DepthEstimator.cpp)
        target_compile_definitions(DepthTest PUBLIC RENDEPTH_ENABLE_ONNX_RUNTIME)
        if(APPLE)
            target_compile_definitions(DepthTest PUBLIC RENDEPTH_ENABLE_COREML)
        endif()
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
            add_custom_command(TARGET DepthTest POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            ${RENDEPTH_RUNTIME_DLLS}
                            "$<TARGET_FILE_DIR:DepthTest>")
        elseif(APPLE)
            target_link_libraries(DepthTest PUBLIC "${RENDEPTH_ONNXRUNTIME_DIR}/lib/libonnxruntime.dylib")
        else()
            target_link_libraries(DepthTest PUBLIC "${RENDEPTH_ONNXRUNTIME_DIR}/lib/libonnxruntime.so")
        endif()
        if(RENDEPTH_ENABLE_CUDA)
            target_compile_definitions(DepthTest PUBLIC RENDEPTH_ENABLE_CUDA)
        endif()
        set_target_properties(DepthTest PROPERTIES
                SKIP_BUILD_RPATH FALSE
                BUILD_RPATH "${CMAKE_SOURCE_DIR}/Library;${RENDEPTH_ONNXRUNTIME_DIR}/lib"
                INSTALL_RPATH "${CMAKE_INSTALL_RPATH}")
        if(APPLE)
            set_target_properties(DepthTest PROPERTIES BUILD_WITH_INSTALL_RPATH FALSE)
        endif()
        if(RENDEPTH_ENABLE_ROCM)
            set_target_properties(DepthTest PROPERTIES
                    BUILD_RPATH "${CMAKE_SOURCE_DIR}/Library;${RENDEPTH_ROCM_DIR}/lib"
                    INSTALL_RPATH "\$ORIGIN/../Library;${RENDEPTH_ROCM_DIR}/lib")
            target_link_options(DepthTest PRIVATE "-Wl,--disable-new-dtags")
        endif()
    endif()

    if(RENDEPTH_BUILD_SUPER_TEST)
        # Development-only test executable; keep it out of the default and
        # packaging builds. It can still be built explicitly when requested.
        add_executable(SuperTest EXCLUDE_FROM_ALL Source/SuperTest.cpp Source/SuperResolution.cpp)
        target_compile_definitions(SuperTest PUBLIC RENDEPTH_ENABLE_ONNX_RUNTIME)
        if(APPLE)
            target_compile_definitions(SuperTest PUBLIC RENDEPTH_ENABLE_COREML)
        endif()
        target_include_directories(SuperTest PUBLIC
                Source ThirdParty/SDL/include ThirdParty/SDL_image/include
                "${RENDEPTH_ONNXRUNTIME_INCLUDE_DIR}")
        target_link_libraries(SuperTest PUBLIC SDL3::SDL3 SDL3_image::SDL3_image)
        if(RENDEPTH_ENABLE_ROCM)
            target_compile_definitions(SuperTest PUBLIC RENDEPTH_ENABLE_ROCM)
            target_link_libraries(SuperTest PUBLIC "${RENDEPTH_ROCM_ONNXRUNTIME_LIBRARY}")
            set_property(TARGET SuperTest APPEND PROPERTY BUILD_RPATH "${RENDEPTH_ROCM_DIR}/lib")
        elseif(WIN32)
            target_link_libraries(SuperTest PUBLIC "${RENDEPTH_ONNXRUNTIME_DIR}/lib/onnxruntime.lib")
            add_custom_command(TARGET SuperTest POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            ${RENDEPTH_RUNTIME_DLLS}
                            "$<TARGET_FILE_DIR:SuperTest>")
        elseif(APPLE)
            target_link_libraries(SuperTest PUBLIC "${RENDEPTH_ONNXRUNTIME_DIR}/lib/libonnxruntime.dylib")
        else()
            target_link_libraries(SuperTest PUBLIC "${RENDEPTH_ONNXRUNTIME_DIR}/lib/libonnxruntime.so")
        endif()
        if(RENDEPTH_ENABLE_CUDA)
            target_compile_definitions(SuperTest PUBLIC RENDEPTH_ENABLE_CUDA)
        endif()
        set_target_properties(SuperTest PROPERTIES
                SKIP_BUILD_RPATH FALSE
                BUILD_RPATH "${CMAKE_SOURCE_DIR}/Library;${RENDEPTH_ONNXRUNTIME_DIR}/lib"
                INSTALL_RPATH "${CMAKE_INSTALL_RPATH}")
        if(APPLE)
            set_target_properties(SuperTest PROPERTIES BUILD_WITH_INSTALL_RPATH FALSE)
        endif()
    endif()

endif()

include(Packaging/Legal.cmake)
if(APPLE AND RENDEPTH_MAC_EXPORT_BUNDLE)
    add_custom_command(TARGET Rendepth POST_BUILD
        COMMAND ${CMAKE_COMMAND} "-DRENDEPTH_BUNDLE=$<TARGET_BUNDLE_DIR:Rendepth>"
            -P "${CMAKE_SOURCE_DIR}/Packaging/Mac/FixupBundle.cmake")
endif()

if(WIN32)
    set(RENDEPTH_GSTREAMER_ROOT "${CMAKE_SOURCE_DIR}/Runtimes/GStreamer" CACHE PATH
        "GStreamer MSVC development installation for Firefox video capture")
    if(EXISTS "${RENDEPTH_GSTREAMER_ROOT}/bin/pkg-config.exe")
        set(PKG_CONFIG_EXECUTABLE "${RENDEPTH_GSTREAMER_ROOT}/bin/pkg-config.exe")
        set(ENV{PKG_CONFIG_PATH} "${RENDEPTH_GSTREAMER_ROOT}/lib/pkgconfig")
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-app-1.0
            gstreamer-video-1.0 gstreamer-webrtc-1.0>=1.22 gstreamer-sdp-1.0)
        target_sources(Rendepth PRIVATE Source/BrowserStream.cpp)
        target_compile_definitions(Rendepth PRIVATE RENDEPTH_HAVE_GSTREAMER=1
            RENDEPTH_GSTREAMER_PLUGIN_DIR="${RENDEPTH_GSTREAMER_ROOT}/lib/gstreamer-1.0")
        target_link_libraries(Rendepth PRIVATE PkgConfig::GSTREAMER)
        add_executable(BrowserCaptureTest EXCLUDE_FROM_ALL
            Source/BrowserCaptureTest.cpp Source/ScreenCapture.cpp Source/BrowserStream.cpp)
        target_compile_definitions(BrowserCaptureTest PRIVATE RENDEPTH_HAVE_GSTREAMER=1
            RENDEPTH_GSTREAMER_PLUGIN_DIR="${RENDEPTH_GSTREAMER_ROOT}/lib/gstreamer-1.0")
        target_link_libraries(BrowserCaptureTest PRIVATE SDL3::SDL3 PkgConfig::GSTREAMER)
        foreach(RENDEPTH_BROWSER_TARGET IN ITEMS Rendepth BrowserCaptureTest)
            add_custom_command(TARGET ${RENDEPTH_BROWSER_TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${RENDEPTH_GSTREAMER_ROOT}/bin" "$<TARGET_FILE_DIR:${RENDEPTH_BROWSER_TARGET}>")
        endforeach()
    else()
        message(STATUS "GStreamer development files not found; Firefox video capture is disabled")
    endif()
endif()
if(APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GSTREAMER QUIET IMPORTED_TARGET gstreamer-1.0 gstreamer-app-1.0
            gstreamer-video-1.0 gstreamer-webrtc-1.0>=1.22 gstreamer-sdp-1.0)
    endif()
    if(GSTREAMER_FOUND)
        message(STATUS "GStreamer WebRTC found; browser video capture is enabled")
        target_sources(Rendepth PRIVATE Source/BrowserStream.cpp)
        target_compile_definitions(Rendepth PRIVATE RENDEPTH_HAVE_GSTREAMER=1)
        target_link_libraries(Rendepth PRIVATE PkgConfig::GSTREAMER)
        add_executable(BrowserCaptureTest EXCLUDE_FROM_ALL
            Source/BrowserCaptureTest.cpp Source/ScreenCapture.cpp Source/BrowserStream.cpp)
        target_compile_definitions(BrowserCaptureTest PRIVATE RENDEPTH_HAVE_GSTREAMER=1)
        target_link_libraries(BrowserCaptureTest PRIVATE SDL3::SDL3 PkgConfig::GSTREAMER)
    else()
        message(STATUS "GStreamer WebRTC development files not found; browser video capture is disabled")
    endif()
    add_executable(BrowserBridgeTest EXCLUDE_FROM_ALL
        Source/BrowserBridgeTest.cpp Source/BrowserBridge.cpp)
    target_include_directories(BrowserBridgeTest PRIVATE ThirdParty/rapidjson/include)
endif()
include(GNUInstallDirs)
if(WIN32)
    set(RENDEPTH_LIBRARY_RUNTIME_DESTINATION libexec/rendepth/Binary)
    if(MSVC)
        # The installer should run on Windows machines without a separately
        # installed Visual C++ Redistributable.
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION libexec/rendepth/Binary)
        include(InstallRequiredSystemLibraries)
    endif()
else()
    set(RENDEPTH_LIBRARY_RUNTIME_DESTINATION libexec/rendepth/Library)
endif()
install(TARGETS Rendepth
        RUNTIME DESTINATION libexec/rendepth/Binary
        BUNDLE DESTINATION .)
install(TARGETS SDL3-shared SDL3_image-shared SDL3_ttf-shared glm
        RUNTIME DESTINATION ${RENDEPTH_LIBRARY_RUNTIME_DESTINATION}
        LIBRARY DESTINATION libexec/rendepth/Library
        ARCHIVE DESTINATION libexec/rendepth/Library)
if(RENDEPTH_ENABLE_MODERN_IMAGE_FORMATS)
    # SDL_image is included as a subdirectory, so its optional dependency
    # targets are not installed by SDLIMAGE_INSTALL. Install the shared codec
    # libraries with the application package as well.
    set(RENDEPTH_MODERN_IMAGE_TARGETS)
    foreach(RENDEPTH_IMAGE_TARGET IN ITEMS
            avif dav1d aom jxl brotlicommon brotlidec brotlienc
            webp webpdemux libwebpmux)
        if(TARGET ${RENDEPTH_IMAGE_TARGET})
            list(APPEND RENDEPTH_MODERN_IMAGE_TARGETS ${RENDEPTH_IMAGE_TARGET})
        endif()
    endforeach()
    set_property(TARGET ${RENDEPTH_MODERN_IMAGE_TARGETS} PROPERTY PUBLIC_HEADER "")
    install(TARGETS ${RENDEPTH_MODERN_IMAGE_TARGETS}
            RUNTIME DESTINATION ${RENDEPTH_LIBRARY_RUNTIME_DESTINATION}
            LIBRARY DESTINATION libexec/rendepth/Library
            ARCHIVE DESTINATION libexec/rendepth/Library)

endif()
if(TARGET tiff)
    # SDL_image's vendored TIFF target is not installed transitively.
    set_property(TARGET tiff PROPERTY PUBLIC_HEADER "")
    install(TARGETS tiff
            RUNTIME DESTINATION ${RENDEPTH_LIBRARY_RUNTIME_DESTINATION}
            LIBRARY DESTINATION libexec/rendepth/Library
            ARCHIVE DESTINATION libexec/rendepth/Library)
endif()
# List runtime assets explicitly so local settings, sample media, and other
# developer files in Assets cannot silently enter a release.
install(FILES Assets/AppIcons.png Assets/CircleIcon.png Assets/Lato.ttf
        Assets/NotoSans.ttf Assets/Rendepth.ico Assets/Rendepth.icns Assets/Rendepth.png
        DESTINATION libexec/rendepth/Assets)
if(WIN32)
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/Shaders/Compiled/"
            DESTINATION libexec/rendepth/Shaders/Compiled
            FILES_MATCHING PATTERN "*.dxil")
elseif(APPLE)
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/Shaders/Compiled/"
            DESTINATION libexec/rendepth/Shaders/Compiled
            FILES_MATCHING PATTERN "*.msl")
elseif(UNIX)
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/Shaders/Compiled/"
            DESTINATION libexec/rendepth/Shaders/Compiled
            FILES_MATCHING PATTERN "*.spv"
            PATTERN "QuiltTest.*" EXCLUDE)
endif()
install(FILES "${RENDEPTH_LEGAL_OUTPUT_DIR}/RENDEPTH_APP_LICENSE"
        "${RENDEPTH_LEGAL_OUTPUT_DIR}/THIRD_PARTY_LICENSING"
        DESTINATION share/licenses/rendepth)
install(FILES "${RENDEPTH_LEGAL_OUTPUT_DIR}/RENDEPTH_APP_LICENSE"
        "${RENDEPTH_LEGAL_OUTPUT_DIR}/THIRD_PARTY_LICENSING"
        DESTINATION libexec/rendepth/Legal)
if(UNIX AND NOT APPLE)
    install(PROGRAMS "${CMAKE_SOURCE_DIR}/Packaging/Linux/rendepth-launcher"
            DESTINATION bin
            RENAME rendepth)
    install(FILES "${CMAKE_SOURCE_DIR}/Packaging/Linux/rendepth.desktop"
            DESTINATION share/applications)
    install(FILES "${CMAKE_SOURCE_DIR}/Assets/Rendepth.png"
            DESTINATION share/icons/hicolor/512x512/apps
            RENAME rendepth.png)
endif()

if(WIN32)
    set(RENDEPTH_DISTRIBUTION_PLATFORM "Windows")
elseif(APPLE)
    set(RENDEPTH_DISTRIBUTION_PLATFORM "macOS")
elseif(UNIX)
    set(RENDEPTH_DISTRIBUTION_PLATFORM "Linux")
else()
    set(RENDEPTH_DISTRIBUTION_PLATFORM "Unknown")
endif()

set(CPACK_PACKAGE_DIRECTORY
        "${CMAKE_SOURCE_DIR}/Distribution/${PROJECT_VERSION}/${RENDEPTH_DISTRIBUTION_PLATFORM}")
set(CPACK_PACKAGE_NAME "rendepth")
set(CPACK_PACKAGE_VENDOR "Outmode")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Convert any image to 3D")
set(CPACK_PACKAGE_DESCRIPTION "Make any normal photo into UHD 3D. Rendepth uses machine learning algorithms to generate depth from regular color images, allowing you to adjust stereo and parallax settings for your display. Exports to open standard stereoscopic formats.")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "Outmode")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/Legal/RENDEPTH_APP_LICENSE")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Outmode")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
# SDL and ONNX Runtime are bundled or supplied by the platform-specific
# runtime setup. These are the Debian-family libraries used by the Linux
# executable itself; alternatives cover the current t64 transitions.
set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libc6, libstdc++6, libgcc-s1, libbluray2 | libbluray3, libdvdread8, libcurl4 | libcurl4t64, libssl3 | libssl3t64, tar, libva2, libva-drm2, libdrm2, libdav1d6 | libdav1d5, libgstreamer1.0-0, libgstreamer-plugins-base1.0-0, gstreamer1.0-plugins-good, gstreamer1.0-plugins-bad, gstreamer1.0-nice")
# Application-level license; component terms are retained in THIRD_PARTY_LICENSING.
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-only")
set(CPACK_RPM_PACKAGE_ARCHITECTURE "x86_64")
# Increment the RPM release for rebuilds of the same application version so
# package managers do not treat a freshly built artifact as the old package.
set(CPACK_RPM_PACKAGE_RELEASE 25)
set(RENDEPTH_RPM_REQUIRES gstreamer1-plugins-good gstreamer1-plugins-bad-free libnice-gstreamer1 tar)
if(RENDEPTH_ENABLE_FFMPEG AND DAV1D_FOUND)
    list(APPEND RENDEPTH_RPM_REQUIRES "libdav1d >= 0.5.0")
endif()
if(RENDEPTH_RPM_REQUIRES)
    list(JOIN RENDEPTH_RPM_REQUIRES ", " CPACK_RPM_PACKAGE_REQUIRES)
endif()
# Strip non-runtime symbol tables, including assembler source filenames.
set(CPACK_STRIP_FILES TRUE)
include(CPack)

if(WIN32)
    if(NOT MSVC)
        target_link_libraries(Rendepth PUBLIC pthread)
        if(RENDEPTH_DLL_DIR)
            add_custom_command(TARGET Rendepth POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${RENDEPTH_DLL_DIR}/libgcc_s_seh-1.dll
                    ${RENDEPTH_DLL_DIR}/libwinpthread-1.dll
                    $<TARGET_FILE_DIR:Rendepth>)
        endif()
    endif()
elseif(UNIX)
    target_link_libraries(Rendepth PUBLIC stdc++)
endif()

# Development-only test executable; keep it out of the default and packaging
# builds. It can still be built explicitly when requested.
add_executable(QuiltTest EXCLUDE_FROM_ALL Source/QuiltTest.cpp)
add_executable(AudioWaveformTest EXCLUDE_FROM_ALL Source/AudioWaveformTest.cpp)
if(MSVC)
    target_compile_options(AudioWaveformTest PRIVATE /UNDEBUG)
else()
    target_compile_options(AudioWaveformTest PRIVATE -UNDEBUG)
endif()
add_executable(NativeDisplaySelectionTest EXCLUDE_FROM_ALL Source/NativeDisplaySelectionTest.cpp)
add_executable(CalibrationVolumesTest EXCLUDE_FROM_ALL Source/CalibrationVolumesTest.cpp)
if(MSVC)
    target_compile_options(CalibrationVolumesTest PRIVATE /UNDEBUG)
else()
    target_compile_options(CalibrationVolumesTest PRIVATE -UNDEBUG)
endif()
add_executable(AIEngineSettingsTest EXCLUDE_FROM_ALL Source/AIEngineSettingsTest.cpp)
target_include_directories(AIEngineSettingsTest PRIVATE ThirdParty/rapidjson/include)
add_executable(CubeViCalibrationTest EXCLUDE_FROM_ALL
        Source/CubeViCalibrationTest.cpp Source/CubeViCalibration.cpp)
add_executable(LiveVideoBufferTest EXCLUDE_FROM_ALL Source/LiveVideoBufferTest.cpp)
add_executable(VideoDepthMotionTest EXCLUDE_FROM_ALL Source/VideoDepthMotionTest.cpp)
target_link_libraries(VideoDepthMotionTest PRIVATE SDL3::SDL3)
target_include_directories(CubeViCalibrationTest PRIVATE ThirdParty/rapidjson/include)
target_link_libraries(CubeViCalibrationTest PRIVATE SDL3::SDL3)
if(WIN32)
    target_link_libraries(CubeViCalibrationTest PRIVATE bcrypt crypt32 user32)
endif()
target_compile_definitions(QuiltTest PRIVATE SDL_MAIN_USE_CALLBACKS)
target_include_directories(QuiltTest PRIVATE
        ThirdParty/SDL/include ThirdParty/rapidjson/include)
target_link_libraries(QuiltTest PRIVATE SDL3::SDL3)
if(RENDEPTH_COMPILE_SHADERS)
	add_dependencies(QuiltTest QuiltTestShaders)
endif()

if(UNIX AND NOT APPLE)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
	pkg_check_modules(GIO_UNIX REQUIRED IMPORTED_TARGET gio-unix-2.0)
	pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-app-1.0
		gstreamer-video-1.0 gstreamer-webrtc-1.0>=1.22 gstreamer-sdp-1.0)
	target_sources(Rendepth PRIVATE Source/BrowserStream.cpp)
	target_link_libraries(Rendepth PUBLIC PkgConfig::GIO PkgConfig::GIO_UNIX PkgConfig::GSTREAMER)
	add_executable(ScreenCaptureTest EXCLUDE_FROM_ALL Source/ScreenCaptureTest.cpp)
	target_include_directories(ScreenCaptureTest PRIVATE ThirdParty/SDL/include)
	target_link_libraries(ScreenCaptureTest PRIVATE
		SDL3::SDL3 PkgConfig::GIO PkgConfig::GIO_UNIX PkgConfig::GSTREAMER)
	add_executable(BrowserCaptureTest EXCLUDE_FROM_ALL
		Source/BrowserCaptureTest.cpp Source/ScreenCapture.cpp Source/BrowserStream.cpp)
	target_link_libraries(BrowserCaptureTest PRIVATE SDL3::SDL3
		PkgConfig::GIO PkgConfig::GIO_UNIX PkgConfig::GSTREAMER)
	add_executable(BrowserBridgeTest EXCLUDE_FROM_ALL Source/BrowserBridgeTest.cpp Source/BrowserBridge.cpp)
	target_include_directories(BrowserBridgeTest PRIVATE ThirdParty/rapidjson/include)
endif()

# Headless welcome-page layout and interaction checks.
add_executable(GettingStartedTest EXCLUDE_FROM_ALL Source/GettingStartedTest.cpp Source/GettingStarted.cpp)
target_include_directories(GettingStartedTest PRIVATE $<TARGET_PROPERTY:Rendepth,INCLUDE_DIRECTORIES>)
target_link_libraries(GettingStartedTest PRIVATE SDL3::SDL3 SDL3_image::SDL3_image SDL3_ttf::SDL3_ttf)

# Disc integration test (requires a readable Blu-ray with at least two titles).
if(TARGET RendepthMVC)
    add_executable(MvcRuntimeTest EXCLUDE_FROM_ALL Source/MvcRuntimeTest.cpp)
    target_link_libraries(MvcRuntimeTest PRIVATE RendepthMVC)
    if(WIN32)
        add_custom_command(TARGET MvcRuntimeTest POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:RendepthMVC>" "$<TARGET_FILE_DIR:MvcRuntimeTest>")
    endif()
endif()
add_executable(BlurayPlaylistTest EXCLUDE_FROM_ALL Source/BlurayPlaylistTest.cpp)
add_executable(BlurayMainFeatureTest EXCLUDE_FROM_ALL Source/BlurayMainFeatureTest.cpp)
add_executable(BlurayTitleNamesTest EXCLUDE_FROM_ALL Source/BlurayTitleNamesTest.cpp)
if(MSVC)
    target_compile_options(BlurayTitleNamesTest PRIVATE /UNDEBUG)
else()
    target_compile_options(BlurayTitleNamesTest PRIVATE -UNDEBUG)
endif()
add_executable(MvcPacketAssemblerTest EXCLUDE_FROM_ALL Source/MvcPacketAssemblerTest.cpp)
if(RENDEPTH_ENABLE_FFMPEG AND RENDEPTH_HAVE_BLURAY)
    add_executable(DiscTitleMenuTest EXCLUDE_FROM_ALL
        Source/DiscTitleMenuTest.cpp Source/DiscTitleMenu.cpp Source/BlurayReader.cpp
        Source/DvdReader.cpp Source/AudioCdReader.cpp Source/DiscSource.cpp Source/VideoPlayer.cpp Source/MvcDecoder.cpp Source/Style.cpp)
    target_include_directories(DiscTitleMenuTest PRIVATE $<TARGET_PROPERTY:Rendepth,INCLUDE_DIRECTORIES>)
    target_compile_definitions(DiscTitleMenuTest PRIVATE $<TARGET_PROPERTY:Rendepth,COMPILE_DEFINITIONS>)
    target_link_libraries(DiscTitleMenuTest PRIVATE $<TARGET_PROPERTY:Rendepth,LINK_LIBRARIES>)
    add_executable(MvcPlaybackTest EXCLUDE_FROM_ALL Source/MvcPlaybackTest.cpp
        Source/BlurayReader.cpp Source/DvdReader.cpp Source/AudioCdReader.cpp
        Source/DiscSource.cpp Source/VideoPlayer.cpp Source/MvcDecoder.cpp)
    target_include_directories(MvcPlaybackTest PRIVATE $<TARGET_PROPERTY:Rendepth,INCLUDE_DIRECTORIES>)
    target_compile_definitions(MvcPlaybackTest PRIVATE $<TARGET_PROPERTY:Rendepth,COMPILE_DEFINITIONS>)
    target_link_libraries(MvcPlaybackTest PRIVATE $<TARGET_PROPERTY:Rendepth,LINK_LIBRARIES>)

    find_package(Threads REQUIRED)
    add_executable(DiscReadAheadTest EXCLUDE_FROM_ALL Source/DiscReadAheadTest.cpp)
    target_include_directories(DiscReadAheadTest PRIVATE $<TARGET_PROPERTY:Rendepth,INCLUDE_DIRECTORIES>)
    target_link_libraries(DiscReadAheadTest PRIVATE Threads::Threads)
endif()

if(RENDEPTH_ENABLE_FFMPEG AND RENDEPTH_HAVE_DVD)
    add_executable(DvdPlaybackTest EXCLUDE_FROM_ALL
        Source/DvdPlaybackTest.cpp Source/DvdReader.cpp Source/DiscSource.cpp
        Source/VideoPlayer.cpp Source/AudioCdReader.cpp Source/BlurayReader.cpp Source/MvcDecoder.cpp)
    target_include_directories(DvdPlaybackTest PRIVATE $<TARGET_PROPERTY:Rendepth,INCLUDE_DIRECTORIES>)
    target_compile_definitions(DvdPlaybackTest PRIVATE $<TARGET_PROPERTY:Rendepth,COMPILE_DEFINITIONS>)
    target_link_libraries(DvdPlaybackTest PRIVATE $<TARGET_PROPERTY:Rendepth,LINK_LIBRARIES>)
endif()

if(RENDEPTH_ENABLE_FFMPEG)
    add_executable(VideoAudioTrackTest EXCLUDE_FROM_ALL
        Source/VideoAudioTrackTest.cpp Source/VideoPlayer.cpp Source/AudioCdReader.cpp
        Source/DiscSource.cpp Source/BlurayReader.cpp Source/DvdReader.cpp Source/MvcDecoder.cpp)
    target_include_directories(VideoAudioTrackTest PRIVATE $<TARGET_PROPERTY:Rendepth,INCLUDE_DIRECTORIES>)
    target_compile_definitions(VideoAudioTrackTest PRIVATE $<TARGET_PROPERTY:Rendepth,COMPILE_DEFINITIONS>)
    target_link_libraries(VideoAudioTrackTest PRIVATE $<TARGET_PROPERTY:Rendepth,LINK_LIBRARIES>)
    add_executable(AudioCdReaderTest EXCLUDE_FROM_ALL
        Source/AudioCdReaderTest.cpp Source/AudioCdReader.cpp Source/DiscSource.cpp)
    target_include_directories(AudioCdReaderTest PRIVATE $<TARGET_PROPERTY:Rendepth,INCLUDE_DIRECTORIES>)
    target_compile_definitions(AudioCdReaderTest PRIVATE $<TARGET_PROPERTY:Rendepth,COMPILE_DEFINITIONS>)
    target_link_libraries(AudioCdReaderTest PRIVATE $<TARGET_PROPERTY:Rendepth,LINK_LIBRARIES>)
    if(MSVC)
        target_compile_options(AudioCdReaderTest PRIVATE /UNDEBUG)
    else()
        target_compile_options(AudioCdReaderTest PRIVATE -UNDEBUG)
    endif()
endif()
if(RENDEPTH_ENABLE_FFMPEG AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(AudioCdPlaybackTest EXCLUDE_FROM_ALL
        Source/AudioCdPlaybackTest.cpp Source/AudioCdReader.cpp Source/DiscSource.cpp
        Source/VideoPlayer.cpp Source/MvcDecoder.cpp Source/BlurayReader.cpp Source/DvdReader.cpp)
    target_include_directories(AudioCdPlaybackTest PRIVATE $<TARGET_PROPERTY:Rendepth,INCLUDE_DIRECTORIES>)
    target_compile_definitions(AudioCdPlaybackTest PRIVATE $<TARGET_PROPERTY:Rendepth,COMPILE_DEFINITIONS>)
    target_link_libraries(AudioCdPlaybackTest PRIVATE $<TARGET_PROPERTY:Rendepth,LINK_LIBRARIES>)
    target_compile_options(AudioCdPlaybackTest PRIVATE -UNDEBUG)
    target_link_options(AudioCdPlaybackTest PRIVATE -Wl,--wrap=open,--wrap=close,--wrap=ioctl)
endif()
if(UNIX AND NOT APPLE)
    add_executable(MediaOpenDialogTest EXCLUDE_FROM_ALL
        Source/MediaOpenDialogTest.cpp Source/MediaOpenDialog.cpp Source/DiscSource.cpp Source/AudioCdReader.cpp)
    target_include_directories(MediaOpenDialogTest PRIVATE ThirdParty/SDL/include)
    target_compile_definitions(MediaOpenDialogTest PRIVATE RENDEPTH_GTK_MEDIA_DIALOG)
    target_compile_options(MediaOpenDialogTest PRIVATE -UNDEBUG)
    target_link_libraries(MediaOpenDialogTest PRIVATE SDL3::SDL3 PkgConfig::GTK3)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(SettingsFileTest EXCLUDE_FROM_ALL Source/SettingsFileTest.cpp Source/SettingsFile.cpp)
    target_include_directories(SettingsFileTest PRIVATE ThirdParty/rapidjson/include)
    target_compile_options(SettingsFileTest PRIVATE -UNDEBUG)
    target_link_options(SettingsFileTest PRIVATE -Wl,--wrap=fsync)
endif()
