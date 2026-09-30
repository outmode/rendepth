# edge264 uses GNU C vector extensions. Build its Windows C ABI as a DLL with
# MinGW, then generate an MSVC import library; the application stays on MSVC.
# Older configurations selected CLion's bundled MinGW through the CMake path
# hint. Its static winpthread archive cannot link this DLL, so discard that
# cached auto-selection and search the developer's PATH again.
if(RENDEPTH_MVC_GCC MATCHES "[/\\\\]JetBrains[/\\\\].*[/\\\\]mingw[/\\\\]bin[/\\\\]gcc\\.exe$")
    unset(RENDEPTH_MVC_GCC CACHE)
    unset(RENDEPTH_MVC_RUNTIME_LICENSES_DIR CACHE)
endif()
find_program(RENDEPTH_MVC_GCC NAMES gcc x86_64-w64-mingw32-gcc
    HINTS "C:/msys64/ucrt64/bin" "C:/msys64/mingw64/bin"
    DOC "64-bit MinGW GCC used to build the bundled MVC decoder" REQUIRED)
execute_process(COMMAND "${RENDEPTH_MVC_GCC}" -dumpmachine
    OUTPUT_VARIABLE mvc_machine OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT mvc_machine MATCHES "^x86_64-.*mingw" OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The Windows MVC decoder requires an x64 MinGW compiler and x64 application")
endif()
get_filename_component(mvc_gcc_bin "${RENDEPTH_MVC_GCC}" DIRECTORY)
# GCC finds its tools relative to its executable. Only its own bin directory
# needs to be on PATH for the DLLs used by the compiler and linker. Passing the
# full Windows PATH here breaks Visual Studio custom commands at semicolons.
get_filename_component(mvc_gcc_root "${mvc_gcc_bin}" DIRECTORY)
if(RENDEPTH_MVC_RUNTIME_LICENSES_DIR)
    cmake_path(IS_PREFIX mvc_gcc_root "${RENDEPTH_MVC_RUNTIME_LICENSES_DIR}"
        NORMALIZE mvc_licenses_match)
    if(NOT mvc_licenses_match)
        unset(RENDEPTH_MVC_RUNTIME_LICENSES_DIR CACHE)
    endif()
endif()
find_path(RENDEPTH_MVC_RUNTIME_LICENSES_DIR NAMES
    crt/COPYING.MinGW-w64-runtime.txt mingw-w64/COPYING.MinGW-w64-runtime.txt
    HINTS "${mvc_gcc_bin}/../share/licenses" "${mvc_gcc_bin}/../licenses"
    DOC "MinGW runtime license directory for the selected MVC compiler" REQUIRED)
set(mvc_source "${CMAKE_SOURCE_DIR}/ThirdParty/SyLC/edge264")
set(mvc_output "${CMAKE_BINARY_DIR}/mvc")
file(MAKE_DIRECTORY "${mvc_output}")
file(GLOB mvc_sources CONFIGURE_DEPENDS "${mvc_source}/src/*.c" "${mvc_source}/src/*.h")
set(mvc_flags -std=gnu17 -O3 -fno-strict-aliasing -flax-vector-conversions
    "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.")
foreach(variant IN ITEMS base v2 v3)
    if(variant STREQUAL "base")
        set(variant_source "${mvc_source}/src/edge264.c")
        set(variant_flags -DHAS_X86_64_V2 -DHAS_X86_64_V3)
    else()
        set(variant_source "${mvc_source}/src/edge264_headers.c")
        set(variant_flags -march=x86-64-${variant} "-DADD_VARIANT(f)=f##_${variant}")
    endif()
    add_custom_command(OUTPUT "${mvc_output}/${variant}.o"
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${mvc_gcc_bin}"
            "${RENDEPTH_MVC_GCC}" ${mvc_flags} ${variant_flags}
            -c "${variant_source}" -o "${mvc_output}/${variant}.o"
        DEPENDS ${mvc_sources} "${mvc_source}/edge264.h" VERBATIM)
endforeach()
add_custom_command(OUTPUT "${mvc_output}/rendepth-mvc.dll"
    COMMAND ${CMAKE_COMMAND} -E env "PATH=${mvc_gcc_bin}"
        "${RENDEPTH_MVC_GCC}" -shared -static-libgcc
        "${CMAKE_CURRENT_LIST_DIR}/Mvc.def"
        "${mvc_output}/base.o" "${mvc_output}/v2.o" "${mvc_output}/v3.o"
        -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic -o "${mvc_output}/rendepth-mvc.dll"
    DEPENDS "${mvc_output}/base.o" "${mvc_output}/v2.o" "${mvc_output}/v3.o"
        "${CMAKE_CURRENT_LIST_DIR}/Mvc.def" VERBATIM)
add_custom_command(OUTPUT "${mvc_output}/rendepth-mvc.lib"
    COMMAND "${CMAKE_AR}" /nologo /machine:x64
        "/def:${CMAKE_CURRENT_LIST_DIR}/Mvc.def" "/out:${mvc_output}/rendepth-mvc.lib"
    DEPENDS "${mvc_output}/rendepth-mvc.dll" "${CMAKE_CURRENT_LIST_DIR}/Mvc.def" VERBATIM)
add_custom_target(RendepthMVCBuild DEPENDS "${mvc_output}/rendepth-mvc.lib")
add_library(RendepthMVC SHARED IMPORTED GLOBAL)
set_target_properties(RendepthMVC PROPERTIES
    IMPORTED_LOCATION "${mvc_output}/rendepth-mvc.dll"
    IMPORTED_IMPLIB "${mvc_output}/rendepth-mvc.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${mvc_source}")
add_dependencies(RendepthMVC RendepthMVCBuild)
target_link_libraries(Rendepth PUBLIC RendepthMVC)
target_compile_definitions(Rendepth PUBLIC RENDEPTH_ENABLE_MVC)
add_custom_command(TARGET Rendepth POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:RendepthMVC>" "$<TARGET_FILE_DIR:Rendepth>")
install(FILES "$<TARGET_FILE:RendepthMVC>" DESTINATION libexec/rendepth/Binary)
