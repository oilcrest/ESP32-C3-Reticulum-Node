#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <IPAddress.h>
#include <vector>
#include <array> // For group addresses

// --- WiFi Credentials ---
const char *WIFI_SSID = "YourWiFiSSID"; // <<< CHANGE ME
const char *WIFI_PASSWORD = "YourWiFiPassword"; // <<< CHANGE ME

// --- Node Configuration ---
const char *BT_DEVICE_NAME = "ESP32-C3-RNSGW"; // Changed slightly from default
const int EEPROM_ADDR_NODE = 0;  // 8 bytes
const int EEPROM_ADDR_PKTID = 8; // 2 bytes (Start after node address)
const int EEPROM_SIZE = 16;      // Min size needed (8+2 = 10, use 16 or 32)

// --- Reticulum Network Parameters ---
const size_t RNS_ADDRESS_SIZE = 8;
const size_t RNS_MAX_PAYLOAD = 200; // Max data payload size (adjust based on memory/MTU)
const uint16_t RNS_UDP_PORT = 4242; // Default Reticulum UDP port
const uint8_t MAX_HOPS = 15;        // Max hop count for packets

// --- Timing & Intervals (milliseconds) ---
const uint16_t PACKET_ID_SAVE_INTERVAL = 100; // Save counter every N packets generated
const unsigned long ANNOUNCE_INTERVAL_MS = 180000; // Announce every 3 minutes
const unsigned long ROUTE_TIMEOUT_MS = ANNOUNCE_INTERVAL_MS * 3 + 15000; // Timeout after ~3 missed announces
const unsigned long PRUNE_INTERVAL_MS = ANNOUNCE_INTERVAL_MS / 2; // Check for old routes periodically
const unsigned long MEM_CHECK_INTERVAL_MS = 15000; // Check memory every 15 seconds
const unsigned long RECENT_ANNOUNCE_TIMEOUT_MS = ANNOUNCE_INTERVAL_MS / 2; // How long to remember forwarded announces

// --- Link Layer Parameters ---
const unsigned long LINK_REQ_TIMEOUT_MS = 10000; // Timeout for initial Link Request ACK
const unsigned long LINK_RETRY_TIMEOUT_MS = 5000; // Timeout for data packet ACK
const unsigned long LINK_INACTIVITY_TIMEOUT_MS = ROUTE_TIMEOUT_MS * 2; // Timeout for closing inactive links
const uint8_t LINK_MAX_RETRIES = 3; // Max retries for a packet before closing link
const size_t LINK_MAX_ACTIVE = 10; // Max concurrent active links (Adjust based on memory)

// --- Routing & Limits ---
const size_t MAX_ROUTES = 20;             // Max entries in routing table
const size_t MAX_RECENT_ANNOUNCES = 40; // Max announce IDs to remember for loop prevention

// --- Group Addresses ---
// Define groups this node belongs to. Example:
const std::vector<std::array<uint8_t, RNS_ADDRESS_SIZE>> SUBSCRIBED_GROUPS = {
    // {0xCA, 0xFE, 0xBA, 0xBE, 0x00, 0x00, 0x00, 0x01}, // Example Group 1
    // {0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34, 0x56, 0x78}  // Example Group 2
};

// --- Interface Identifiers ---
enum class InterfaceType {
    UNKNOWN,
    LOCAL, // For packets originating from this node
    SERIAL,
    BLUETOOTH,
    ESP_NOW,
    WIFI_UDP
};

// --- Packet Contexts (Includes Link and Local Command) ---
#define RNS_CONTEXT_NONE        0x00
#define RNS_CONTEXT_LINK_REQ    0xA1 // Request to establish link
#define RNS_CONTEXT_LINK_CLOSE  0xA2 // Request to close link
#define RNS_CONTEXT_LINK_DATA   0xA3 // Data packet over an established link
#define RNS_CONTEXT_ACK         0xA4 // Context used in ACK header_type packets
#define RNS_CONTEXT_LOCAL_CMD   0xFE // Context for local commands via KISS

// Sequence number size (placed at start of payload for LINK_DATA/ACK)
const size_t RNS_SEQ_SIZE = 2;


#endif // CONFIG_H
