/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include "systemevent.h"

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
 * \brief Interface for listening to SR_systemEvent updates
 *
 * \ingroup System API
 */
class DIMENCOSR_API SystemEventListener {
public:
    /**
     * \brief Accept an SR_systemEvent frame
     *
     * \param frame represents a new SR_systemEvent update
     *
     * This function is called from an SystemEventStream.
     * SystemEvents will never be dropped.
     */
    virtual void accept(const SystemEvent& frame) = 0;
};

}

#undef DIMENCOSR_API
