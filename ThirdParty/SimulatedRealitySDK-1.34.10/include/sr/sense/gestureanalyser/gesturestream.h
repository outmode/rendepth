/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

#include "sr/sense/core/buffer.h"
#include "gesture.h"

#include "gesturelistener.h"

#ifdef WIN32
#   ifdef COMPILING_DLL_SimulatedRealityHandTrackers
#     define DIMENCOSR_API __declspec(dllexport)
#   else
#     define DIMENCOSR_API __declspec(dllimport)
#   endif
#else
#   define DIMENCOSR_API
#endif

namespace SR {

/**
 * \brief Class of Buffer<SR_gesture> managing instances of SR_gesture in real-time
 *
 * \ingroup DownstreamInterface GestureAnalyser
 */
class GestureBuffer : public Buffer<SR_gesture> {
    SR_gestureType type;

public:
    GestureBuffer(SR_gestureType type);

    SR_gestureType getGestureType();
};

class GestureAnalyser; //forward declaration

 /**
  * \brief Stream of SR_gesture objects
  *
  * Connects a GestureAnalyser Sense to a GestureAnalyser object.
  *
  * \ingroup DownstreamInterface GestureAnalyser API
  */
class DIMENCOSR_API GestureStream {
    class Impl;
    Impl* pimpl;
public:
    using Frame = SR_gesture;
    GestureStream(GestureAnalyser* sense, GestureListener* listener);
    void stopListening();
    void close();
    void update(SR_gesture gesture);
};

}

#undef DIMENCOSR_API
