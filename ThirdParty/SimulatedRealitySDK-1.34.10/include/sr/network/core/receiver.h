/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include "packet.h"

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
 * \brief Interface to be used to enable receiving SR_packet from other SR applications
 *
 * \ingroup Core_Network API
 */
class DIMENCOSR_API Receiver {
public:
    /**
     * \brief Receives SR_packet sent over network
     *
     * \param packet is a C-compatible message which requires no further serialization
     *
     * Should be called from the applications network interfaces.
     */
    virtual void receive(SR_packet& packet) = 0;

    /**
     * \brief Prints text representation of SR_packet
     *
     * \param packet is a C-compatible message which requires no further serialization
     *
     * Used for debugging purposes.
     * Inherited via Receiver.
     */
    virtual void print(SR_packet& packet) = 0;

    /**
     * \brief Ensures that the destructor of extending classes is called when a pointer has been cast to Receiver*
     *
     * A destructor for the Receiver should ensure any networking related objects are cleaned up appropriately.
     */
    virtual ~Receiver() {};
};

}

#undef DIMENCOSR_API
