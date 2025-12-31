#ifndef INTERFACE_MANAGER_H
#define INTERFACE_MANAGER_H

#include <Arduino.h>
#include <WiFiUdp.h>
#if BLUETOOTH_CLASSIC_AVAILABLE
#include <BluetoothSerial.h> // Requires CONFIG_BT_ENABLED=y and CONFIG_CLASSIC_BT_ENABLED=y in sdkconfig
#endif
#include <esp_now.h>
#include <functional>
#include <vector>
#include <IPAddress.h> // Include IPAddress

#ifdef LORA_ENABLED
#include <RadioLib.h>
#endif

#ifdef IPFS_ENABLED
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#endif

#include "Config.h"
#include "KISS.h"

// Forward declarations
class RoutingTable;
class ReticulumNode;

// Callback type for received packets:
// void packet_receiver(const uint8_t *packetBuffer, size_t packetLen, InterfaceType interface,
//                      const uint8_t* sender_mac, const IPAddress& sender_ip, uint16_t sender_port)
using PacketReceiverCallback = std::function<void(const uint8_t*, size_t, InterfaceType, const uint8_t*, const IPAddress&, uint16_t)>;

class InterfaceManager {
public:
    InterfaceManager(PacketReceiverCallback receiver, RoutingTable& routingTable);

    void setup();
    void loop(); // Process inputs from interfaces

    // Sending methods
    // Sends packet out relevant interfaces based on routing (or broadcast), excluding source interface
    void sendPacket(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr, InterfaceType excludeInterface = InterfaceType::UNKNOWN);
    // Sends packet via a specific interface type (used internally or for specific needs)
    void sendPacketVia(InterfaceType ifType, const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr);
    // Broadcasts an announce packet on relevant interfaces
    void broadcastAnnounce(const uint8_t *packetBuffer, size_t packetLen);
#ifdef LORA_ENABLED
    // LoRa-specific methods
    bool isLoRaInitialized() const { return _loraInitialized; }
#endif

#ifdef IPFS_ENABLED
    // IPFS methods
    bool fetchIPFSContent(const char* ipfsHash, std::vector<uint8_t>& output);
    bool publishToIPFS(const uint8_t* data, size_t len, String& ipfsHash);
#endif

#ifdef HAM_MODEM_ENABLED
    // HAM modem methods
    bool isHAMModemInitialized() const { return _hamModemInitialized; }
    void sendAPRSPacket(const char* destination, const char* message);
    void sendAPRSPosition(float lat, float lon, float altitude = 0, const char* comment = "");
    void sendAPRSWeather(float temp, float humidity, float pressure, const char* comment = "");
    void sendAPRSMessage(const char* addressee, const char* message);
#endif

    // ESP-NOW Peer Management (can be called by RoutingTable during prune)
    bool addEspNowPeer(const uint8_t* mac_addr);
    bool removeEspNowPeer(const uint8_t* mac_addr);
    bool checkEspNowPeer(const uint8_t* mac_addr);

    // Static callbacks needed for C-style APIs like ESP-NOW
    static void staticEspNowRecvCallback(const uint8_t *mac_addr, const uint8_t *incomingData, int len);
    // static void staticEspNowSendCallback(const uint8_t *mac_addr, esp_now_send_status_t status); // Optional

private:
    void setupWiFi();
    void setupESPNow();
    void setupBluetooth();
    void setupSerial();
#ifdef LORA_ENABLED
    void setupLoRa();
#endif
#ifdef HAM_MODEM_ENABLED
    void setupHAMModem();
#endif
#ifdef IPFS_ENABLED
    void setupIPFS();
#endif

    void processWiFiInput();
    void processSerialInput();
    void processBluetoothInput();
#ifdef LORA_ENABLED
    void processLoRaInput();
#endif
#ifdef HAM_MODEM_ENABLED
    void processHAMModemInput();
#endif

    // Specific Send implementations called by public send methods
    void sendPacketViaEspNow(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr);
    void sendPacketViaWiFi(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr);
    void sendPacketViaSerial(const uint8_t *packetBuffer, size_t packetLen);
    void sendPacketViaBluetooth(const uint8_t *packetBuffer, size_t packetLen);
#ifdef LORA_ENABLED
    void sendPacketViaLoRa(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr);
    void processLoRaInput();
#endif
#ifdef HAM_MODEM_ENABLED
    void sendPacketViaHAMModem(const uint8_t *packetBuffer, size_t packetLen);
#endif
#ifdef IPFS_ENABLED
    void sendPacketViaIPFS(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr);
#endif

    PacketReceiverCallback _packetReceiver; // Callback to ReticulumNode::handleReceivedPacket
    RoutingTable& _routingTableRef; // Reference for route lookups / peer management
    WiFiUDP _udp;
#if BLUETOOTH_CLASSIC_AVAILABLE
    BluetoothSerial _serialBT; // Bluetooth Serial object
    KISSProcessor _bluetoothKissProcessor; // KISS processor for Bluetooth
#endif
    KISSProcessor _serialKissProcessor; // KISS processor for USB Serial
#ifdef LORA_ENABLED
    SX1278* _lora; // LoRa radio instance
    bool _loraInitialized;
#endif
#ifdef HAM_MODEM_ENABLED
    KISSProcessor _hamModemKissProcessor; // KISS processor for HAM modem
    bool _hamModemInitialized;
    #ifdef AUDIO_MODEM_ENABLED
    class AudioModem* _audioModem; // Audio modem instance (forward declaration)
    #endif
    #ifdef WINLINK_ENABLED
    class Winlink* _winlink; // Winlink instance (forward declaration)
    #endif
#endif
#ifdef HAM_MODEM_ENABLED
    KISSProcessor _hamModemKissProcessor; // KISS processor for HAM modem
    bool _hamModemInitialized;
#endif
#ifdef IPFS_ENABLED
    HTTPClient _httpClient; // HTTP client for IPFS gateway access
    bool _ipfsInitialized;
#endif

    // Static instance pointer for callbacks to access 'this'
    static InterfaceManager* _instance;

    // Kiss packet handler method (non-static member) called by KISSProcessor instances
    void handleKissPacket(const std::vector<uint8_t>& packetData, InterfaceType interface);

};

#endif // INTERFACE_MANAGER_H
