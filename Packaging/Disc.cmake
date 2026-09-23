# Disc playback is part of every desktop build. Never ship a successful build
# whose readers are just the unavailable-backend stubs.
set(RENDEPTH_DISC_ROOT "${RENDEPTH_FFMPEG_ROOT}" CACHE PATH
    "Disc SDK prefix containing include, lib and (on Windows) bin directories")

find_package(PkgConfig QUIET)
foreach(disc_library IN ITEMS bluray dvdread)
    if(disc_library STREQUAL "bluray")
        set(disc_header libbluray/bluray.h)
        set(disc_package libbluray)
        set(disc_feature BLURAY)
    else()
        set(disc_header dvdread/dvd_reader.h)
        set(disc_package dvdread)
        set(disc_feature DVD)
    endif()

    if(WIN32)
        # pkg-config can select release libraries for a Debug MSVC build.
        # Resolve both configurations and use headers from the same SDK.
        find_path(RENDEPTH_${disc_library}_INCLUDE_DIR NAMES ${disc_header}
            HINTS "${RENDEPTH_DISC_ROOT}/include" NO_DEFAULT_PATH REQUIRED)
        find_library(RENDEPTH_${disc_library}_RELEASE
            NAMES ${disc_library} lib${disc_library}
            HINTS "${RENDEPTH_DISC_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
        find_library(RENDEPTH_${disc_library}_DEBUG
            NAMES ${disc_library} lib${disc_library}
            HINTS "${RENDEPTH_DISC_ROOT}/debug/lib" NO_DEFAULT_PATH REQUIRED)
        add_library(RendepthDisc_${disc_library} UNKNOWN IMPORTED GLOBAL)
        set_target_properties(RendepthDisc_${disc_library} PROPERTIES
            IMPORTED_LOCATION "${RENDEPTH_${disc_library}_RELEASE}"
            IMPORTED_LOCATION_DEBUG "${RENDEPTH_${disc_library}_DEBUG}"
            INTERFACE_INCLUDE_DIRECTORIES "${RENDEPTH_${disc_library}_INCLUDE_DIR}")
    else()
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(RENDEPTH_${disc_library} QUIET IMPORTED_TARGET GLOBAL ${disc_package})
        endif()
        if(TARGET PkgConfig::RENDEPTH_${disc_library})
            add_library(RendepthDisc_${disc_library} ALIAS PkgConfig::RENDEPTH_${disc_library})
        else()
            find_path(RENDEPTH_${disc_library}_INCLUDE_DIR NAMES ${disc_header}
                HINTS "${RENDEPTH_DISC_ROOT}/include" REQUIRED)
            find_library(RENDEPTH_${disc_library}_LIBRARY
                NAMES ${disc_library} lib${disc_library}
                HINTS "${RENDEPTH_DISC_ROOT}/lib" REQUIRED)
            add_library(RendepthDisc_${disc_library} UNKNOWN IMPORTED GLOBAL)
            set_target_properties(RendepthDisc_${disc_library} PROPERTIES
                IMPORTED_LOCATION "${RENDEPTH_${disc_library}_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${RENDEPTH_${disc_library}_INCLUDE_DIR}")
        endif()
    endif()
    target_link_libraries(Rendepth PUBLIC RendepthDisc_${disc_library})
    target_compile_definitions(Rendepth PUBLIC RENDEPTH_ENABLE_${disc_feature})
    set(RENDEPTH_HAVE_${disc_feature} TRUE)
endforeach()

if(WIN32)
    if(NOT IS_DIRECTORY "${RENDEPTH_DISC_ROOT}/bin" OR
       NOT IS_DIRECTORY "${RENDEPTH_DISC_ROOT}/debug/bin")
        message(FATAL_ERROR "Disc runtime DLLs are missing. Install libbluray and libdvdread in RENDEPTH_DISC_ROOT (for example with vcpkg install libbluray:x64-windows libdvdread:x64-windows).")
    endif()
    # Include transitive DLLs (UDF, XML, fonts and DVD CSS) supplied by the SDK.
    # The FFmpeg packaging already copies these when the SDK prefixes match.
    if(NOT RENDEPTH_DISC_ROOT STREQUAL RENDEPTH_FFMPEG_ROOT)
        add_custom_command(TARGET Rendepth POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "$<IF:$<CONFIG:Debug>,${RENDEPTH_DISC_ROOT}/debug/bin,${RENDEPTH_DISC_ROOT}/bin>"
                "$<TARGET_FILE_DIR:Rendepth>")
        install(DIRECTORY "${RENDEPTH_DISC_ROOT}/bin/"
            DESTINATION libexec/rendepth/Binary FILES_MATCHING PATTERN "*.dll")
    endif()
endif()
message(STATUS "Rendepth: Blu-ray, DVD and audio CD playback enabled")
