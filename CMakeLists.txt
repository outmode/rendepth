cmake_minimum_required(VERSION 3.22)
project(Rendepth LANGUAGES CXX)
set(EXECUTABLE_NAME Rendepth)
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
set(ZMQ_BUILD_TESTS OFF)
set(ZMQ_HAVE_IPC OFF)
set(USE_OWN_FFTW OFF)
set(BUILD_BENCHMARK OFF)
set(ENABLE_TESTS OFF)
set(BUILD_TESTING OFF)
set(BUILD_SHARED_LIBS ON)

if(WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -fopenmp -mavx -w -O3")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static-libstdc++ -fopenmp -mavx -w -O3")
elseif(APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -Xclang -fopenmp -w -O3")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static-libstdc++ -Xclang -fopenmp -w -O3")
elseif(UNIX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++ -fopenmp -mavx -w -O3")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static-libgcc -static-libstdc++ -fopenmp -mavx -w -O3")
endif()

find_package(OpenMP)
if (OPENMP_FOUND)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
endif()

add_subdirectory(ThirdParty/SDL EXCLUDE_FROM_ALL)
add_subdirectory(ThirdParty/SDL_ttf EXCLUDE_FROM_ALL)
add_subdirectory(ThirdParty/SDL_image EXCLUDE_FROM_ALL)
add_subdirectory(ThirdParty/libzmq EXCLUDE_FROM_ALL)
add_subdirectory(ThirdParty/glm EXCLUDE_FROM_ALL)
add_subdirectory(ThirdParty/fCWT EXCLUDE_FROM_ALL)


if(WIN32)
    add_compile_options(-Wl,-Bstatic)
    add_executable(Rendepth WIN32 Assets/Rendepth.rc)
    target_sources(Rendepth PUBLIC ThirdParty/fCWT/src/fcwt/fcwt.cpp)
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
                ${CMAKE_SOURCE_DIR}/Library/libfCWT.2.0.dylib ${CMAKE_SOURCE_DIR}/Library/libfCWT.dylib
                ${CMAKE_SOURCE_DIR}/Library/libglm.dylib ${CMAKE_SOURCE_DIR}/Library/libomp.dylib
                ${CMAKE_SOURCE_DIR}/Legal/LICENSE)
        set_source_files_properties(
                ${CMAKE_SOURCE_DIR}/Library/libSDL3.0.dylib ${CMAKE_SOURCE_DIR}/Library/libSDL3.dylib
                ${CMAKE_SOURCE_DIR}/Library/libSDL3_image.0.2.4.dylib ${CMAKE_SOURCE_DIR}/Library/libSDL3_image.0.dylib
                ${CMAKE_SOURCE_DIR}/Library/libSDL3_image.dylib
                ${CMAKE_SOURCE_DIR}/Library/libSDL3_ttf.0.2.2.dylib ${CMAKE_SOURCE_DIR}/Library/libSDL3_ttf.0.dylib
                ${CMAKE_SOURCE_DIR}/Library/libSDL3_ttf.dylib
                ${CMAKE_SOURCE_DIR}/Library/libfCWT.2.0.dylib ${CMAKE_SOURCE_DIR}/Library/libfCWT.dylib
                ${CMAKE_SOURCE_DIR}/Library/libglm.dylib ${CMAKE_SOURCE_DIR}/Library/libomp.dylib
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

if(WIN32)
    add_custom_target(Shaders
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/Shaders
            COMMAND compile_windows.bat
            VERBATIM)
elseif(APPLE)
    add_custom_target(Shaders
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/Shaders
            COMMAND ${CMAKE_SOURCE_DIR}/Shaders/compile_macos.sh
            VERBATIM)
elseif(UNIX)
    add_custom_target(Shaders
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/Shaders
            COMMAND ${CMAKE_SOURCE_DIR}/Shaders/compile_linux.sh
            VERBATIM)
endif()

add_dependencies(Rendepth Shaders)

target_compile_definitions(Rendepth PUBLIC SDL_MAIN_USE_CALLBACKS)

target_sources(Rendepth PUBLIC Source/Main.cpp Source/Core.cpp Source/Image.cpp
        Source/Style.cpp Source/Utils.cpp)

target_include_directories(Rendepth PUBLIC
        ThirdParty/glm ThirdParty/SDL/include ThirdParty/SDL_image/include
        ThirdParty/SDL_ttf/include ThirdParty/SDL_shadercross/include
        ThirdParty/fCWT/libs ThirdParty/fCWT/src ThirdParty/rapidjson/include
        ThirdParty/libzmq/include ThirdParty/cppzmq)

target_link_directories(Rendepth PUBLIC "\${ORIGIN}/../Library"
        ThirdParty/fCWT/libs)

target_link_libraries(Rendepth PUBLIC
        SDL3::SDL3 SDL3_image::SDL3_image SDL3_ttf::SDL3_ttf
        libzmq-static fCWT glm omp)

if(WIN32)
    target_link_libraries(Rendepth PUBLIC fftw3f gomp pthread)
    add_custom_command(TARGET Rendepth POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/ThirdParty/fCWT/libs/fftw3f.dll
        $<TARGET_FILE_DIR:Rendepth>)
    if(RENDEPTH_DLL_DIR)
        add_custom_command(TARGET Rendepth POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${RENDEPTH_DLL_DIR}/libgcc_s_seh-1.dll
                ${RENDEPTH_DLL_DIR}/libgomp-1.dll
                ${RENDEPTH_DLL_DIR}/libwinpthread-1.dll
                $<TARGET_FILE_DIR:Rendepth>)
    endif()
elseif(APPLE)
    if(RENDEPTH_OMP_DYLIB)
        add_custom_command(TARGET Rendepth POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${RENDEPTH_OMP_DYLIB}/libomp.dylib
                ${CMAKE_SOURCE_DIR}/Library)
    endif()
elseif(UNIX)
    target_link_libraries(Rendepth PUBLIC stdc++)
endif()
