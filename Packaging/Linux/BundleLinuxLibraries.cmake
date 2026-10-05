# Copy Linux disc and codec libraries needed by this build into the application.
# Resolve symlinks before copying so the package never points back to /usr/lib.
set(_app_root "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/rendepth")
set(_binary "${_app_root}/Binary/Rendepth")
set(_library_dir "${_app_root}/Library")
if(NOT EXISTS "${_binary}")
    message(FATAL_ERROR "Missing Rendepth binary while bundling disc libraries: ${_binary}")
endif()
find_program(_patchelf patchelf REQUIRED)
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${_binary}"
    RESOLVED_DEPENDENCIES_VAR _dependencies
    UNRESOLVED_DEPENDENCIES_VAR _unresolved)
if(_unresolved)
    message(FATAL_ERROR "Unresolved Rendepth runtime dependencies: ${_unresolved}")
endif()

set(_bundled_bluray FALSE)
set(_bundled_dvdread FALSE)
foreach(_dependency IN LISTS _dependencies)
    get_filename_component(_name "${_dependency}" NAME)
    if(NOT _name MATCHES "^lib(bluray|dvdread|udfread|dav1d)[.]so[.][0-9].*$")
        continue()
    endif()
    file(REAL_PATH "${_dependency}" _source)
    execute_process(COMMAND "${_patchelf}" --print-soname "${_source}"
        OUTPUT_VARIABLE _soname OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _result ERROR_VARIABLE _error)
    if(NOT _result EQUAL 0 OR NOT _soname MATCHES "^lib(bluray|dvdread|udfread|dav1d)[.]so[.][0-9]+$")
        message(FATAL_ERROR "Cannot determine disc library SONAME for ${_source}: ${_error}")
    endif()
    set(_destination "${_library_dir}/${_soname}")
    if(NOT EXISTS "${_destination}")
        file(COPY_FILE "${_source}" "${_destination}")
        execute_process(COMMAND "${_patchelf}" --force-rpath --set-rpath "$ORIGIN" "${_destination}"
            RESULT_VARIABLE _result ERROR_VARIABLE _error)
        if(NOT _result EQUAL 0)
            message(FATAL_ERROR "Cannot set relative RPATH on ${_destination}: ${_error}")
        endif()
    endif()
    if(_soname MATCHES "^libbluray[.]")
        set(_bundled_bluray TRUE)
    elseif(_soname MATCHES "^libdvdread[.]")
        set(_bundled_dvdread TRUE)
    endif()
endforeach()
if(NOT _bundled_bluray OR NOT _bundled_dvdread)
    message(FATAL_ERROR "The Linux package must contain real libbluray and libdvdread files")
endif()
