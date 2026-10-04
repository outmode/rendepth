if(CPACK_GENERATOR STREQUAL "DEB")
    # Resolve ELF package dependencies from the actual staged binary, including
    # SONAMEs and minimum symbol versions, instead of listing ABI alternatives.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_PRE_BUILD_SCRIPTS
        "${CMAKE_CURRENT_LIST_DIR}/ValidateDeb.cmake")
endif()
