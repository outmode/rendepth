/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

/**
 * \brief C-compatible descriptor of SR camera component
 */
struct SR_cameraDescriptor {
    union {
        uint64_t serialNumber;
        uint64_t numeralIdentifier;
    };
    uint64_t cameraTypeLength;
    const char* cameraType;
};
