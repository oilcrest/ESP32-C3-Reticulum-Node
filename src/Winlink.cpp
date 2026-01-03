#include "Winlink.h"

// NOTE: This is a stub implementation to ensure build linkage.
// Full Winlink protocol handling is not implemented here.

Winlink::Winlink()
: _state(ConnectionState::DISCONNECTED),
  _sequenceNumber(0)
{}

bool Winlink::begin(const char* callsign, const char* password) {
    _callsign = callsign ? callsign : "";
    _password = password ? password : "";
    _state = ConnectionState::DISCONNECTED;
    return true;
}

bool Winlink::connect(const char* bbsCallsign) {
    _bbsCallsign = bbsCallsign ? bbsCallsign : "";
    _state = ConnectionState::CONNECTED;
    return true; // Stub success
}

void Winlink::disconnect() {
    _state = ConnectionState::DISCONNECTED;
}

bool Winlink::sendMessage(const Message& msg) {
    (void)msg;
    // Stub: pretend send succeeded
    return true;
}

bool Winlink::receiveMessages(std::vector<Message>& messages) {
    messages.clear();
    return true;
}

bool Winlink::checkForMessages() {
    return false;
}

bool Winlink::processFrame(const AX25::Frame& frame) {
    (void)frame;
    return false;
}

bool Winlink::handleConnect(const AX25::Frame& frame) { (void)frame; return false; }
bool Winlink::handleDisconnect(const AX25::Frame& frame) { (void)frame; return false; }
bool Winlink::handleAck(const AX25::Frame& frame) { (void)frame; return false; }
bool Winlink::handleNak(const AX25::Frame& frame) { (void)frame; return false; }
bool Winlink::handleMessage(const AX25::Frame& frame) { (void)frame; return false; }

bool Winlink::encodeMessage(const Message& msg, std::vector<uint8_t>& output) {
    (void)msg;
    output.clear();
    return false;
}

bool Winlink::decodeMessage(const uint8_t* data, size_t len, Message& msg) {
    (void)data; (void)len; (void)msg;
    return false;
}

bool Winlink::sendFrame(const std::vector<uint8_t>& data, bool requiresAck) {
    (void)data; (void)requiresAck;
    return true;
}
