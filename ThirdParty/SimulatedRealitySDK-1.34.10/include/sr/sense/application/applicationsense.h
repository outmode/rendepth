/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include <map>
#include <thread>
#include <mutex>
#include "sr/management/srcontext.h"

#ifdef WIN32
#   ifdef COMPILING_DLL_SimulatedRealityCore
#     define DIMENCOSR_API __declspec(dllexport)
#   else
#     define DIMENCOSR_API __declspec(dllimport)
#   endif
#else
#   define DIMENCOSR_API
#endif

namespace SR {

 /**
 * \defgroup Application
 * \brief Classes that enable applications to get access to data about other SR Applications
 */

 /**
  * \brief Sense class which shares information about SR Applications throughout the SR system and to applications
  *
  * \ingroup DownstreamInterface Application API
  */
class DIMENCOSR_API ApplicationSense : public Sense {

public:
    /**
     * \brief Creates a functional ApplicationSense instance
     *
     * \param context is the environment in which created senses are kept track of
     * \return ApplicationSense* which can provide information about other connected SR Application
     */
    static ApplicationSense* create(SRContext &context);

    /**
    * \brief Get the names of all SR Applications connected to the server
    *
    * \returns A list of the names of all SR Applications connected to the server
    */
    virtual std::vector<std::string> getApplicationNames() = 0;
};

}

#undef DIMENCOSR_API
