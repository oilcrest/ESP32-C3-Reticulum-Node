#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <IPAddress.h>
#include <vector>
#include <array> // For group addresses

// --- Debug and Interface Configuration ---
// Use Serial (UART0/USB) for debug messages (normal Arduino Serial Monitor)
// Use Serial1/Serial2 (UART1/UART2) for KISS interface with Reticulum

#define DEBUG_ENABLED 1  // Set to 0 to completely disable debug

#define DebugSerial Serial    // Use USB/UART0 for debug (Arduino Serial Monitor)

// ESP32-C3 only has UART0 and UART1 (no UART2)
// ESP32 has UART0, UART1, and UART2
#if defined(CONFIG_IDF_TARGET_ESP32C3)
    #define KissSerial Serial1    // ESP32-C3: Use UART1 for KISS
    #define KISS_UART_RX 18       // ESP32-C3 UART1 default RX pin
    #define KISS_UART_TX 19       // ESP32-C3 UART1 default TX pin
#else
    #define KissSerial Serial2    // ESP32: Use UART2 for KISS
    #define KISS_UART_RX 16       // ESP32 UART2 RX pin
    #define KISS_UART_TX 17       // ESP32 UART2 TX pin
#endif

#define KISS_SERIAL_SPEED 115200

// --- WiFi Credentials ---
extern const char *WIFI_SSID; // <<< CHANGE ME in Config.cpp
extern const char *WIFI_PASSWORD; // <<< CHANGE ME in Config.cpp

// --- Node Configuration ---
extern const char *BT_DEVICE_NAME;
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
    // Destination hash for PLAIN destination ["esp32", "node"] - calculated by tests/read_from_reticulum.py
    // This allows the ESP32 to receive messages sent to this destination
    {0xB6, 0x01, 0x0E, 0xA1, 0x1F, 0xDF, 0xC0, 0x4E} // First 8 bytes of 16-byte hash (truncated for group)

};

// --- Interface Identifiers ---
enum class InterfaceType {
    UNKNOWN,
    LOCAL, // For packets originating from this node
    SERIAL_PORT,
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
