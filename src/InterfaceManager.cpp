#include "InterfaceManager.h"
#include "Config.h"
#include "Utils.h"
#include "RoutingTable.h" // Need full definition now for RouteEntry
#include <WiFi.h>
#include <esp_wifi.h> // For esp_wifi_set_ps

// Define static instance pointer
InterfaceManager* InterfaceManager::_instance = nullptr;

InterfaceManager::InterfaceManager(PacketReceiverCallback receiver, RoutingTable& routingTable) :
    _packetReceiver(receiver),
    _routingTableRef(routingTable),
    // Use lambda to capture 'this' for the member function callback
    _serialKissProcessor([this](const std::vector<uint8_t>& data, InterfaceType iface){ this->handleKissPacket(data, iface); }),
    _bluetoothKissProcessor([this](const std::vector<uint8_t>& data, InterfaceType iface){ this->handleKissPacket(data, iface); })
{
    if (_instance != nullptr) {
         // This should not happen if InterfaceManager is instantiated only once by ReticulumNode
         Serial.println("! FATAL: Multiple InterfaceManager instances detected!");
         // Handle error: abort?
         return;
    }
    _instance = this; // Set the static instance pointer
}

void InterfaceManager::setup() {
    setupSerial(); // Assumes Serial.begin() already called
    setupWiFi();   // Sets mode, connects, starts UDP
    setupESPNow(); // Depends on WiFi mode being set
    setupBluetooth();
    esp_wifi_set_ps(WIFI_PS_NONE); // Disable WiFi power saving for potentially better responsiveness
    Serial.println("Interface Manager Setup Complete.");
}

void InterfaceManager::loop() {
    // Process inputs from KISS interfaces
    processSerialInput();
    processBluetoothInput();

    // Process UDP input if WiFi is connected
    if (WiFi.status() == WL_CONNECTED) {
        processWiFiInput();
    }
}

void InterfaceManager::setupSerial() {
    // Serial Monitor is usually started in main.cpp for early debugging
    Serial.println("IF: Serial interface ready.");
}

void InterfaceManager::setupWiFi() {
    WiFi.mode(WIFI_AP_STA); // ESP-NOW needs STA or AP mode active
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("IF: Connecting to WiFi ");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500); Serial.print("."); attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nIF: WiFi connected.");
        Serial.print("IF: IP address: "); Serial.println(WiFi.localIP());
        if (_udp.begin(RNS_UDP_PORT)) {
            Serial.print("IF: UDP Listening on port "); Serial.println(RNS_UDP_PORT);
        } else {
            Serial.println("! ERROR: Failed to start UDP listener!");
        }
    } else {
        Serial.println("\n! IF: WiFi connection failed.");
        // Node might operate without WiFi, but UDP interface won't work
    }
}

void InterfaceManager::setupESPNow() {
     Serial.print("IF: Device MAC: "); Serial.println(WiFi.macAddress());
    if (esp_now_init() != ESP_OK) {
        Serial.println("! ERROR: Initializing ESP-NOW failed!");
        return; // Cannot proceed with ESP-NOW
    }
    // Register static callback function which calls instance method
    esp_err_t result = esp_now_register_recv_cb(staticEspNowRecvCallback);
    if (result != ESP_OK) {
         Serial.print("! ERROR: Failed to register ESP-NOW recv cb: "); Serial.println(esp_err_to_name(result));
    }
    // esp_now_register_send_cb(staticEspNowSendCallback); // Optional: register send status callback

    // Add broadcast peer initially (needed to receive broadcasts)
    if (!addEspNowPeer(espnow_broadcast_mac)) {
         Serial.println("! WARN: Failed to add initial ESP-NOW broadcast peer");
    }
    Serial.println("IF: ESP-NOW Initialized.");
}

void InterfaceManager::setupBluetooth() {
     if (!_serialBT.begin(BT_DEVICE_NAME)) {
         Serial.println("! ERROR: Bluetooth Serial initialization failed!");
     } else {
        Serial.println("IF: Bluetooth ready. Device Name: " BT_DEVICE_NAME);
    }
}

// --- Input Processing ---
void InterfaceManager::processWiFiInput() {
    int packetSize = _udp.parsePacket();
    if (packetSize > 0) {
        if (packetSize > MAX_PACKET_SIZE) {
             Serial.print("! WARN: Oversized UDP packet received ("); Serial.print(packetSize); Serial.println(" bytes), discarding.");
             _udp.flush(); // Discard data
             return;
        }

        // Use unique_ptr for automatic memory management
        std::unique_ptr<uint8_t[]> udpBuffer(new (std::nothrow) uint8_t[packetSize]);
        if (!udpBuffer) {
             Serial.println("! ERROR: new failed for UDP buffer!");
             _udp.flush();
             return;
        }

        int len = _udp.read(udpBuffer.get(), packetSize);
        if (len > 0 && _packetReceiver) {
            _packetReceiver(udpBuffer.get(), len, InterfaceType::WIFI_UDP, nullptr, _udp.remoteIP(), _udp.remotePort());
        }
    }
}

void InterfaceManager::processSerialInput() {
     while (Serial.available()) {
        _serialKissProcessor.decodeByte(Serial.read(), InterfaceType::SERIAL);
    }
}

void InterfaceManager::processBluetoothInput() {
     while (_serialBT.available()) {
         _bluetoothKissProcessor.decodeByte(_serialBT.read(), InterfaceType::BLUETOOTH);
    }
}

// --- KISS Packet Handling ---
void InterfaceManager::handleKissPacket(const std::vector<uint8_t>& packetData, InterfaceType interface) {
     if (_packetReceiver) {
         // Pass received packet up to ReticulumNode, indicate no specific sender MAC/IP/Port
         _packetReceiver(packetData.data(), packetData.size(), interface, nullptr, IPAddress(), 0);
     }
}


// --- Sending Logic ---
void InterfaceManager::sendPacket(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr, InterfaceType excludeInterface) {
    if (!packetBuffer || packetLen == 0) return;

    // Determine target interface(s) based on routing (or broadcast if unknown)
    RouteEntry* route = _routingTableRef.findRoute(destinationAddr);

    // Send via specific interface if route found, otherwise broadcast on relevant interfaces
    if (route) {
        if (route->interface != excludeInterface) {
             // Send only via the routed interface
             sendPacketVia(route->interface, packetBuffer, packetLen, destinationAddr);
        }
    } else {
        // No route, broadcast on primary interfaces (excluding source)
        // Serial.print("Broadcasting packet (no route found) for dest: "); Utils::printBytes(destinationAddr, RNS_ADDRESS_SIZE, Serial); Serial.println(); // Verbose
        if (excludeInterface != InterfaceType::ESP_NOW) {
            sendPacketViaEspNow(packetBuffer, packetLen, nullptr); // Broadcast = null dest for internal func
        }
        if (WiFi.status() == WL_CONNECTED && excludeInterface != InterfaceType::WIFI_UDP) {
             sendPacketViaWiFi(packetBuffer, packetLen, nullptr); // Broadcast = null dest for internal func
        }
        // Broadcast on Serial/BT usually only for specific bridging applications, skip by default
        // if (excludeInterface != InterfaceType::SERIAL) { sendPacketViaSerial(packetBuffer, packetLen); }
        // if (_serialBT.connected() && excludeInterface != InterfaceType::BLUETOOTH) { sendPacketViaBluetooth(packetBuffer, packetLen); }
    }
}

void InterfaceManager::sendPacketVia(InterfaceType ifType, const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr) {
     if (!packetBuffer || packetLen == 0) return;
     switch(ifType) {
        case InterfaceType::ESP_NOW:  sendPacketViaEspNow(packetBuffer, packetLen, destinationAddr); break;
        case InterfaceType::WIFI_UDP: sendPacketViaWiFi(packetBuffer, packetLen, destinationAddr); break;
        case InterfaceType::SERIAL:   sendPacketViaSerial(packetBuffer, packetLen); break;
        case InterfaceType::BLUETOOTH:sendPacketViaBluetooth(packetBuffer, packetLen); break;
        default: Serial.print("! WARN: sendPacketVia unsupported interface: "); Serial.println(static_cast<int>(ifType)); break;
     }
}

void InterfaceManager::broadcastAnnounce(const uint8_t *packetBuffer, size_t packetLen) {
     if (!packetBuffer || packetLen == 0) return;
     // Use nullptr destination for broadcast variants
     sendPacketViaEspNow(packetBuffer, packetLen, nullptr);
     if (WiFi.status() == WL_CONNECTED) {
         sendPacketViaWiFi(packetBuffer, packetLen, nullptr);
     }
}

// Internal send implementations
void InterfaceManager::sendPacketViaEspNow(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr) {
    const uint8_t* targetMac = espnow_broadcast_mac; // Default to broadcast
    RouteEntry* route = nullptr;

    if (destinationAddr != nullptr) { // If destination provided, try to find route
        route = _routingTableRef.findRoute(destinationAddr);
         if (route && route->interface == InterfaceType::ESP_NOW) {
             targetMac = route->next_hop_mac;
             // Ensure peer exists - crucial for direct send
             if (!checkEspNowPeer(targetMac)) {
                 if (!addEspNowPeer(targetMac)) {
                     targetMac = espnow_broadcast_mac; // Fallback if add fails
                 }
             }
         } else { targetMac = espnow_broadcast_mac; } // No route / wrong interface
    } // else: destinationAddr is null -> use broadcastMac

    esp_err_t result = esp_now_send(targetMac, packetBuffer, packetLen);
    if (result != ESP_OK) { Serial.print("! ESP-NOW Send Error to "); Utils::printBytes(targetMac, 6, Serial); Serial.print(": "); Serial.println(esp_err_to_name(result)); }
}

void InterfaceManager::sendPacketViaWiFi(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr) {
     if (WiFi.status() != WL_CONNECTED) return;

    IPAddress targetIp;
    uint16_t targetPort = RNS_UDP_PORT;
    IPAddress broadcastIp = WiFi.broadcastIP();
    targetIp = broadcastIp; // Default to broadcast

     if (destinationAddr != nullptr) { // If destination provided, try to find route
        RouteEntry* route = _routingTableRef.findRoute(destinationAddr);
        if (route && route->interface == InterfaceType::WIFI_UDP && route->next_hop_ip) {
            targetIp = route->next_hop_ip;
            // targetPort = route->next_hop_port; // Use standard port
        } // else: use broadcast IP
     } // else: destinationAddr is null -> use broadcast IP

    if (!targetIp || targetIp == INADDR_NONE) {
        Serial.println("! WARN: UDP Target IP is invalid, cannot send.");
        return;
    }

    _udp.beginPacket(targetIp, targetPort);
    size_t sent = _udp.write(packetBuffer, packetLen);
    if (sent != packetLen) { Serial.print("! WARN: UDP write incomplete (sent "); Serial.print(sent); Serial.print("/"); Serial.print(packetLen); Serial.println(" bytes)"); }
    if (!_udp.endPacket()) { Serial.println("! ERROR: UDP endPacket failed!"); }
}

void InterfaceManager::sendPacketViaSerial(const uint8_t *packetBuffer, size_t packetLen) {
    std::vector<uint8_t> kissEncoded;
    KISSProcessor::encode(packetBuffer, packetLen, kissEncoded);
    size_t sent = Serial.write(kissEncoded.data(), kissEncoded.size());
    // if(sent != kissEncoded.size()) { Serial.println("! WARN: Serial write incomplete"); } // Optional check
}
void InterfaceManager::sendPacketViaBluetooth(const uint8_t *packetBuffer, size_t packetLen) {
    if (!_serialBT.connected()) return;
    std::vector<uint8_t> kissEncoded;
    KISSProcessor::encode(packetBuffer, packetLen, kissEncoded);
    size_t sent = _serialBT.write(kissEncoded.data(), kissEncoded.size());
     // if(sent != kissEncoded.size()) { Serial.println("! WARN: Bluetooth write incomplete"); } // Optional check
}

// --- ESP-NOW Peer Management ---
bool InterfaceManager::addEspNowPeer(const uint8_t* mac_addr) {
    if (!mac_addr) return false;
    if (checkEspNowPeer(mac_addr)) return true; // Already exists

    esp_now_peer_info_t peerInfo = {}; // Initialize all fields to 0/false/etc.
    memcpy(peerInfo.peer_addr, mac_addr, 6);
    // peerInfo.channel = 0; // Use current channel by default
    // peerInfo.encrypt = false; // TODO: Add configuration for encryption
    // peerInfo.ifidx = WIFI_IF_STA; // Use station interface? Or AP? Test which works best. WIFI_IF_AP might also be needed.
    esp_err_t add_result = esp_now_add_peer(&peerInfo);
    if (add_result != ESP_OK) {
         Serial.print("! ERROR: Failed to add ESP-NOW peer "); Utils::printBytes(mac_addr, 6, Serial); Serial.print(": "); Serial.println(esp_err_to_name(add_result));
         return false;
    }
    Serial.print("IF: Added ESP-NOW peer: "); Utils::printBytes(mac_addr, 6, Serial); Serial.println();
    return true;
}

bool InterfaceManager::removeEspNowPeer(const uint8_t* mac_addr) {
     if (!mac_addr) return false;
     if (!checkEspNowPeer(mac_addr)) return false; // Not found

     esp_err_t del_result = esp_now_del_peer(mac_addr);
     if (del_result != ESP_OK) {
         Serial.print("! WARN: Failed to delete ESP-NOW peer "); Utils::printBytes(mac_addr, 6, Serial); Serial.print(": "); Serial.println(esp_err_to_name(del_result));
         return false;
     }
     Serial.print("IF: Removed ESP-NOW peer: "); Utils::printBytes(mac_addr, 6, Serial); Serial.println();
     return true;
}

bool InterfaceManager::checkEspNowPeer(const uint8_t* mac_addr) {
    if (!mac_addr) return false;
    // esp_now_is_peer_exist() is deprecated/removed in later IDF versions.
    // Use esp_now_get_peer() and check result.
    esp_now_peer_info_t peer_info;
    return (esp_now_get_peer(mac_addr, &peer_info) == ESP_OK);
    // return esp_now_is_peer_exist(mac_addr); // Older IDF versions
}


// --- Static Callbacks ---
void InterfaceManager::staticEspNowRecvCallback(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
    if (_instance && _instance->_packetReceiver && mac_addr && incomingData && len > 0) {
        if (len <= MAX_PACKET_SIZE) {
             // Pass to instance's packet receiver callback
            _instance->_packetReceiver(incomingData, (size_t)len, InterfaceType::ESP_NOW, mac_addr, IPAddress(), 0);
        } else {
             Serial.print("! WARN: Oversized ESP-NOW packet received ("); Serial.print(len); Serial.println(" bytes), discarding.");
        }
    }
}

/* Optional Static Send Callback
void InterfaceManager::staticEspNowSendCallback(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (_instance && mac_addr) {
        // Could notify routing table or link manager about send status
        // Serial.print("IF: ESP-NOW Send Status to MAC "); Utils::printBytes(mac_addr, 6, Serial); Serial.print(": "); Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
    }
}
*/
