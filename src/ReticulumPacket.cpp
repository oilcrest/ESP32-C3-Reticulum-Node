#include "ReticulumPacket.h"
#include "Config.h"
#include <Arduino.h> // For Serial debug prints
#include <cstring>   // For memcpy

namespace ReticulumPacket {

bool deserialize(const uint8_t *buffer, size_t len, RnsPacketInfo &info) {
    info.valid = false; // Assume invalid until parsed
    if (!buffer || len < RNS_MIN_HEADER_SIZE) {
        Serial.println("! Deserialize Error: Null buffer or packet too short.");
        return false;
    }

    info.header_type = buffer[0];
    info.context = buffer[1];
    info.packet_id = (buffer[2] << 8) | buffer[3]; // Network byte order (Big Endian)
    info.hops = buffer[4];
    info.destination_type = buffer[5];
    uint8_t destination_len = buffer[6];

    if ((info.destination_type != RNS_DST_TYPE_SINGLE && info.destination_type != RNS_DST_TYPE_GROUP) || destination_len != RNS_ADDRESS_SIZE) {
         Serial.println("! Deserialize Error: Unsupported destination type/len.");
         return false;
    }
    memcpy(info.destination, buffer + 7, RNS_ADDRESS_SIZE);

    info.source_type = buffer[15];
    uint8_t source_len = buffer[16];
     if (info.source_type != RNS_DST_TYPE_SINGLE || source_len != RNS_ADDRESS_SIZE) {
         Serial.println("! Deserialize Error: Unsupported source type/len.");
         return false;
     }
    memcpy(info.source, buffer + 17, RNS_ADDRESS_SIZE);

    size_t headerSize = RNS_MIN_HEADER_SIZE; // Assuming Type 1/2 addr lengths
    size_t payloadLen = len - headerSize;

    if (payloadLen > RNS_MAX_PAYLOAD) {
        Serial.println("! Deserialize Error: Payload exceeds max size.");
        return false;
    }
    // Use assign for safer vector copy
    info.payload.assign(buffer + headerSize, buffer + headerSize + payloadLen);
    info.packet_len = len;
    info.valid = true; // Passed basic checks

    // Extract sequence number if applicable (might invalidate if format wrong)
    info.processPayloadForLink();

    if (!info.valid) {
        // Serial.println("! Deserialize Error: Invalid link payload format (e.g., missing seq num).");
        // processPayloadForLink already set valid to false
        return false;
    }

    return true;
}

// Main serializer - handles embedding sequence number
bool serialize(uint8_t *buffer, size_t &len,
               const uint8_t* dest, const uint8_t* src,
               uint8_t dest_type, uint8_t header_type, uint8_t context,
               uint16_t packet_id, uint8_t hops,
               const std::vector<uint8_t>& data_payload, // Actual data
               uint16_t sequence_num) // Sequence number to embed
{
    len = 0; // Reset length
    if (!buffer || !dest || !src) {
        Serial.println("! Serialize Error: Null buffer or address pointer.");
        return false;
    }

    size_t payload_offset = 0;
    size_t total_payload_size = data_payload.size();

    // Check if sequence number needs to be prepended
    bool needs_sequence = (context == RNS_CONTEXT_LINK_DATA || (header_type == RNS_HEADER_TYPE_ACK && context == RNS_CONTEXT_ACK));

    if (needs_sequence) {
        total_payload_size += RNS_SEQ_SIZE;
    }

    if (total_payload_size > RNS_MAX_PAYLOAD) {
        Serial.println("! Serialize Error: Total payload exceeds max size.");
        return false;
    }

    size_t requiredLen = RNS_MIN_HEADER_SIZE + total_payload_size;
    if (requiredLen > MAX_PACKET_SIZE) { // Check against absolute max
        Serial.println("! Serialize Error: Total packet exceeds max buffer size.");
        return false;
    }

    // --- Fill Header ---
    buffer[0] = header_type;
    buffer[1] = context;
    buffer[2] = (packet_id >> 8) & 0xFF; // Network byte order
    buffer[3] = packet_id & 0xFF;
    buffer[4] = hops;
    buffer[5] = dest_type;
    buffer[6] = RNS_ADDRESS_SIZE; // Assuming Type 1/2
    memcpy(buffer + 7, dest, RNS_ADDRESS_SIZE);
    buffer[15] = RNS_DST_TYPE_SINGLE; // Source is always SINGLE
    buffer[16] = RNS_ADDRESS_SIZE;
    memcpy(buffer + 17, src, RNS_ADDRESS_SIZE);

    // --- Fill Payload ---
    uint8_t* payload_ptr = buffer + RNS_MIN_HEADER_SIZE;
    if (needs_sequence) {
        // Prepend sequence number (Network Byte Order - Big Endian)
        payload_ptr[0] = (sequence_num >> 8) & 0xFF;
        payload_ptr[1] = sequence_num & 0xFF;
        payload_offset = RNS_SEQ_SIZE;
    }

    // Copy actual data payload if it exists
    if (!data_payload.empty()) {
        memcpy(payload_ptr + payload_offset, data_payload.data(), data_payload.size());
    }

    len = requiredLen;
    return true;
}

// Helper for simple control packets (ACK, REQ, CLOSE)
bool serialize_control(uint8_t* buffer, size_t& len,
                        const uint8_t* dest, const uint8_t* src,
                        uint8_t header_type, uint8_t context,
                        uint16_t packet_id,
                        uint16_t sequence_num_for_ack) // Embed seq num in payload for ACK
{
    if (!buffer || !dest || !src) {
        Serial.println("! Serialize Control Error: Null buffer or address pointer.");
        return false;
    }

    std::vector<uint8_t> payload_data; // Payload is often empty for control
    uint16_t sequence_to_embed = 0; // Main sequence number field usually 0 for control

    if (header_type == RNS_HEADER_TYPE_ACK && context == RNS_CONTEXT_ACK) {
        // ACKs payload contains the sequence number being acknowledged
        payload_data.resize(RNS_SEQ_SIZE);
        payload_data[0] = (sequence_num_for_ack >> 8) & 0xFF;
        payload_data[1] = sequence_num_for_ack & 0xFF;
    }
    // For REQ, CLOSE - payload_data remains empty

    // Call main serialize
    bool ok = serialize(buffer, len, dest, src, RNS_DST_TYPE_SINGLE, header_type, context, packet_id, 0, payload_data, sequence_to_embed);
    if (!ok) {
        Serial.println("! Serialize Control Error: Underlying serialize failed.");
    }
    return ok;
}

} // namespace ReticulumPacket
