#ifndef RETICULUM_PACKET_H
#define RETICULUM_PACKET_H

#include "Config.h" // Includes size constants, contexts etc.
#include <vector>
#include <cstdint>
#include <cstring> // For memcpy

// --- Reticulum Header Defines ---
#define RNS_HEADER_TYPE_MASK  0b00000111
#define RNS_HEADER_TYPE_DATA  0x01
#define RNS_HEADER_TYPE_ACK   0x02 // Used for Link ACKs too
#define RNS_HEADER_TYPE_ANN   0x03 // Announce

#define RNS_HEADER_FLAG_REQUEST_ACK_MASK 0b10000000 // Request ACK (e.g., for Link Data)

// Destination Address Types
#define RNS_DST_TYPE_SINGLE   0x01
#define RNS_DST_TYPE_GROUP    0x02
#define RNS_DST_TYPE_PLAIN    0x03 // Not fully handled
#define RNS_DST_TYPE_LINK     0x04 // Not fully handled

// Basic Header Size (Type 1/2 addresses)
const size_t RNS_MIN_HEADER_SIZE = 24;
const size_t MAX_PACKET_SIZE = RNS_MIN_HEADER_SIZE + RNS_MAX_PAYLOAD;


// --- Decoded Packet Info Structure ---
struct RnsPacketInfo {
    uint8_t header_type = 0;
    uint8_t context = RNS_CONTEXT_NONE;
    uint16_t packet_id = 0;
    uint8_t hops = 0;
    uint8_t destination_type = 0;
    uint8_t destination[RNS_ADDRESS_SIZE] = {0};
    uint8_t source_type = 0;
    uint8_t source[RNS_ADDRESS_SIZE] = {0};

    std::vector<uint8_t> payload; // Raw payload including sequence number if applicable
    size_t packet_len = 0;
    bool valid = false;

    // --- Link Layer Specific Extracted Info ---
    uint16_t sequence_number = 0; // Extracted sequence number (if applicable)
    std::vector<uint8_t> data;    // Actual data payload (excluding sequence number)

    RnsPacketInfo() : valid(false), packet_len(0), sequence_number(0) {}

    // Helper to extract sequence number and data
    void processPayloadForLink() {
        if (context == RNS_CONTEXT_LINK_DATA || (header_type == RNS_HEADER_TYPE_ACK && context == RNS_CONTEXT_ACK)) {
            if (payload.size() >= RNS_SEQ_SIZE) {
                sequence_number = (payload[0] << 8) | payload[1]; // Network Byte Order (Big Endian)
                data.assign(payload.begin() + RNS_SEQ_SIZE, payload.end());
            } else {
                 // Invalid packet format for link context
                 valid = false; // Mark packet as invalid if seq num missing
                 data.clear();
                 sequence_number = 0;
            }
        } else {
            // Not a packet type that contains sequence number in payload like this
            data = payload; // Data is the whole payload
            sequence_number = 0;
        }
    }
};

// --- Serialization/Deserialization Functions ---
namespace ReticulumPacket {
    bool deserialize(const uint8_t *buffer, size_t len, RnsPacketInfo &info);
    // Modified serialize takes data, adds sequence num if needed
    bool serialize(uint8_t *buffer, size_t &len,
                   const uint8_t* dest, const uint8_t* src,
                   uint8_t dest_type, uint8_t header_type, uint8_t context,
                   uint16_t packet_id, uint8_t hops,
                   const std::vector<uint8_t>& data_payload, // Actual data
                   uint16_t sequence_num = 0); // Sequence number to embed

     // Helper to create simple packets (like ACKs, REQs) easier
     bool serialize_control(uint8_t* buffer, size_t& len,
                            const uint8_t* dest, const uint8_t* src,
                            uint8_t header_type, uint8_t context,
                            uint16_t packet_id,
                            uint16_t sequence_num_for_ack = 0); // Embed seq num in payload for ACK
}

#endif // RETICULUM_PACKET_H
