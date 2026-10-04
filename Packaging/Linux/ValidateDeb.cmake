find_program(RENDEPTH_PYTHON3 python3 REQUIRED)
find_program(RENDEPTH_READELF readelf REQUIRED)
if(NOT CPACK_TEMPORARY_DIRECTORY)
    message(FATAL_ERROR "CPack did not provide its staged package directory")
endif()
execute_process(
    COMMAND "${RENDEPTH_PYTHON3}"
        "${CMAKE_CURRENT_LIST_DIR}/validate_deb_elf.py"
        "${CPACK_TEMPORARY_DIRECTORY}/usr/libexec/rendepth"
    RESULT_VARIABLE validation_result
    OUTPUT_VARIABLE validation_output
    ERROR_VARIABLE validation_error)
if(NOT validation_result EQUAL 0)
    message(FATAL_ERROR "DEB ELF validation failed:\n${validation_output}${validation_error}")
endif()
message(STATUS "${validation_output}")
