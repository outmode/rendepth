/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#ifndef ADMIN_C_H
#define ADMIN_C_H

//TODO include application information struct type

typedef void* SRContext;

#ifdef WIN32
#ifdef __cplusplus
#   ifdef COMPILING_DLL_SimulatedRealityCore
#     define SRAPI extern "C" __declspec(dllexport)
#   else
#     define SRAPI extern "C" __declspec(dllimport)
#   endif
#else
#   ifdef COMPILING_DLL_SimulatedRealityCore
#     error Trying to compile SimulatedRealityCore.dll using a non-C++ compiler! Use a C++ compiler instead!
#   else
#     define SRAPI __declspec(dllimport)
#   endif
#endif
#else
#   define SRAPI
#endif

/**
 * \brief Application Programming Interface to be used by (Admin) C SR applications
 */

typedef void* SR_applicationSense;

/**
 * \brief Create an application sense instance to gain information about running SR applications
 *
 * \param context is the environment in which created senses are kept track of
 * \returns SR_applicationSense which is a pointer to an instance of the class with the underlying implementation 
 */
SRAPI SR_applicationSense createApplicationSense(SRContext context);

/**
 * \brief C-compatible struct to represent the contents of the std::vector<std::string> returned in the C++ interface
 */
typedef struct {
    unsigned int size;
    char** applicationNames;
} SR_applicationList;

/**
 * \brief Returns a list of applications that are currently connected to the SR Service
 *
 * \param a SR_applicationSense pointer to the underlying implementation as returned by createApplicationSense
 * \returns SR_applicationList with an array of null-terminated strings
 */
SRAPI SR_applicationList getApplicationNames(SR_applicationSense applicationSense);

/**
 * \brief Clears the memory allocated for the list of applications returned by getApplicationNames
 *
 * \param an unmodified instance of SR_applicationList as returned by getApplicationNames
 */
SRAPI void cleanupApplicationList(SR_applicationList applicationList);

#undef SRAPI

#endif // ADMIN_C_H
