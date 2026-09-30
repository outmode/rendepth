/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#ifndef SYSTEMEVENT_H
#define SYSTEMEVENT_H

#include "sr/types.h"
#include <stdint.h>

#ifdef __cplusplus
enum SR_eventType : uint64_t {
    Info = 0, //!< The application is being informed of some changes to or within the system that do not affect it directly
    ContextInvalid = 1, //!< Context needs to be re-initialized for the application to recover
    SRUnavailable = 2, //!< Application should only display 2D content because SR content will not be experienced as intended (The lens could be forcefully deactivated)
    SRRestored = 3, //!< Application can display SR content again after an occurrence of SR_eventType::SRUnavailable
    USBNotConnected = 4, //!< SR Device's USB cable is not connected
    USBNotConnectedResolved = 5, //!< SR Device's USB cable has been successfully connected after an occurance of SR_eventType::USBNotConnected
    DisplayNotConnected = 6, //!< SR Device's display cable is not connected
    DisplayNotConnectedResolved = 7, //!< SR Device's display cable has been successfully connected after an occurrence of SR_eventType::DisplayNotConnected
    Duplicated = 8, //!< SR Display is in duplicated mode and can not be used by SR applications to show SR content
    DuplicatedResolved = 9, //!< SR Display is in extended mode and can now be used by SR applications to show SR content after an occurrence of SR_eventType::Duplicated
    NonNativeResolution = 10, //!< SR Display is configured to use a non-native resolution and can not be used by SR applications to show SR content
    NonNativeResolutionResolved = 11, //!< SR Display is configured to use the correct resolution and can now be used by SR applications to show SR content after an occurrence of SR_eventType::NonNativeResolution
    DeviceConnectedAndReady = 12, //!< SR Device is connected and ready to use
    DeviceDisconnected = 13, //!< SR Device is disconnected and can not be used
    LensOn = 14, //!< The lens of the SR device has been enabled
    LensOff = 15, //!< The lens of the SR device has been disabled
    UserFound = 16, //!< The user is being tracked by the Eyetracker
    UserLost = 17 //!< The user has been lost by the Eyetracker
};
#else
typedef uint64_t SR_eventType;
#endif //__cplusplus

/**
 * \brief C-compatible struct containing a system-wide event
 *
 * \ingroup System API
 */
typedef struct {
    uint64_t time; //!< Time since epoch in microseconds
    SR_eventType eventType; //!< Type of the event
    uint64_t messageLength; //!< Full message string length
    const char* message; //!< Start of full string message
} SR_systemEvent;

#ifdef __cplusplus
#include <string>

namespace SR {

/**
 * \brief Class containing dynamic-length event messages
 *
 * The class can be converted to a C-compatible struct when required, the original C++ object should be kept in scope to guarantee the message data being available.
 *
 * \ingroup System API
 */
class SystemEvent {
public:
    uint64_t time; //!< Time since epoch in microseconds
    SR_eventType eventType; //!< Type of the event
    std::string message; //!< Full description of the event

    operator SR_systemEvent() {
        return {
            time,
            eventType,
            static_cast<uint64_t>(message.length()),
            message.c_str(),
        };
    }
};

}
#endif //__cplusplus

#endif //SYSTEMEVENT_H
