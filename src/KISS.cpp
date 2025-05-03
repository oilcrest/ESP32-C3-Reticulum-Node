#include "KISS.h"
#include "Config.h"   // For MAX_PACKET_SIZE
#include <Arduino.h> // For Serial debug

KISSProcessor::KISSProcessor(PacketHandler handler) :
    _packetHandler(handler),
    _inEscapeState(false)
{
     // Reserve some buffer space upfront if desired
     // _receiveBuffer.reserve(MAX_PACKET_SIZE / 2);
}

void KISSProcessor::decodeByte(uint8_t byte, InterfaceType interface) {
    // Handle FEND indicating end of packet OR start padding
    if (byte == KISS_FEND) {
        if (!_receiveBuffer.empty()) {
            // End of a packet, process it
            if (_packetHandler) {
                _packetHandler(_receiveBuffer, interface);
            }
            _receiveBuffer.clear(); // Clear buffer for next packet
        }
        // Ignore FEND if buffer is empty (start padding or multiple FENDs)
        _inEscapeState = false; // Reset escape state on FEND
        return; // Done processing this FEND byte
    }

    // Handle escape sequences
    if (_inEscapeState) {
        if (byte == KISS_TFEND) {
            byte = KISS_FEND; // Store unescaped byte
        } else if (byte == KISS_TFESC) {
            byte = KISS_FESC; // Store unescaped byte
        } else {
             // Protocol error: FESC followed by invalid byte
             Serial.print("! KISS Decode Error: Invalid escape sequence on interface "); Serial.println(static_cast<int>(interface));
             _receiveBuffer.clear(); // Discard partial packet
             _inEscapeState = false; // Reset escape state
             return; // Discard this byte too
        }
        _inEscapeState = false; // Handled escape sequence
        // Now process the unescaped 'byte' below
    } else if (byte == KISS_FESC) {
        // Start of an escape sequence, wait for next byte
        _inEscapeState = true;
        return; // Don't store the FESC itself
    }

    // Store regular or unescaped data byte
    if (_receiveBuffer.size() < MAX_PACKET_SIZE + 50) { // Allow buffer for KISS overhead + header leeway
        _receiveBuffer.push_back(byte);
    } else {
        // Buffer overflow
        Serial.print("! KISS Decode Error: Packet buffer overflow on interface "); Serial.println(static_cast<int>(interface));
        _receiveBuffer.clear(); // Discard oversized packet
        _inEscapeState = false; // Reset escape state
        // If overflow occurs, the current packet is lost. FEND will reset.
    }
}

// Static encode method
void KISSProcessor::encode(const uint8_t *input, size_t len, std::vector<uint8_t> &output) {
    if (!input) return;
    output.clear();
    // output.reserve(len + 2 + (len / 50)); // Pre-allocate heuristic
    output.push_back(KISS_FEND); // Start with FEND is recommended by some KISS variants

    for (size_t i = 0; i < len; ++i) {
        if (input[i] == KISS_FEND) {
            output.push_back(KISS_FESC);
            output.push_back(KISS_TFEND);
        } else if (input[i] == KISS_FESC) {
            output.push_back(KISS_FESC);
            output.push_back(KISS_TFESC);
        } else {
            output.push_back(input[i]);
        }
    }
    output.push_back(KISS_FEND); // End with FEND
}
