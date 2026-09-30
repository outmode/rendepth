/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include <stdint.h>
#include <iostream>
#include <iomanip>

#define SR_packet_sizeOffset        0
#define SR_packet_destinationOffset (sizeof(SR_packet::size))
#define SR_packet_payloadOffset     (sizeof(SR_packet::size) + sizeof(SR_packet::destination))
#define SR_packet_headerSize        SR_packet_payloadOffset

/**
 * \brief C-compatible struct for communication between SR applications
 *
 * \ingroup Core_Network API
 */
typedef struct {
    uint64_t size;
    uint64_t destination;
    uint64_t payload; //!< Start of binary payload data
} SR_packet;

/**
 * \brief Print raw contents of SR network packet
 *
 * \param packet for communication within the SR system
 */
static void SR_packet_print(SR_packet& packet) {
    uint64_t frameSize = packet.size - SR_packet_payloadOffset;
    std::cout << "SR_packet {\n"
        << "\tsize:        " << packet.size << "\n"
        << "\tdestination: " << packet.destination << "\n"
        << "\tpayload:     "
        << std::setfill('0') << std::hex;
    for (uint64_t i = frameSize; i > 0; --i) {
        std::cout << std::setw(2) << (int)(((uint8_t*)&packet.payload)[i - 1]) << " ";
    }
    std::cout << std::dec << std::endl;
}
