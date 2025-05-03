#ifndef INTERFACE_MANAGER_H
#define INTERFACE_MANAGER_H

#include <Arduino.h>
#include <WiFiUdp.h>
#include <BluetoothSerial.h> // Requires CONFIG_BT_ENABLED=y and CONFIG_CLASSIC_BT_ENABLED=y in sdkconfig
#include <esp_now.h>
#include <functional>
#include <vector>
#include <IPAddress.h> // Include IPAddress

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

    void processWiFiInput();
    void processSerialInput();
    void processBluetoothInput();

    // Specific Send implementations called by public send methods
    void sendPacketViaEspNow(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr);
    void sendPacketViaWiFi(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr);
    void sendPacketViaSerial(const uint8_t *packetBuffer, size_t packetLen);
    void sendPacketViaBluetooth(const uint8_t *packetBuffer, size_t packetLen);

    PacketReceiverCallback _packetReceiver; // Callback to ReticulumNode::handleReceivedPacket
    RoutingTable& _routingTableRef; // Reference for route lookups / peer management
    WiFiUDP _udp;
    BluetoothSerial _serialBT; // Bluetooth Serial object
    KISSProcessor _serialKissProcessor; // KISS processor for USB Serial
    KISSProcessor _bluetoothKissProcessor; // KISS processor for Bluetooth

    // Static instance pointer for callbacks to access 'this'
    static InterfaceManager* _instance;

    // Kiss packet handler method (non-static member) called by KISSProcessor instances
    void handleKissPacket(const std::vector<uint8_t>& packetData, InterfaceType interface);

};

#endif // INTERFACE_MANAGER_H
