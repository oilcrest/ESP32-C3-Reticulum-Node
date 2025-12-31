#include "InterfaceManager.h"
#include "Config.h"
#include "Utils.h"
#include "RoutingTable.h" // Need full definition now for RouteEntry
#include "ReticulumPacket.h" // For MAX_PACKET_SIZE
#include <WiFi.h>
#include <esp_wifi.h> // For esp_wifi_set_ps
#ifdef LORA_ENABLED
#include <SPI.h>
#endif

#ifdef IPFS_ENABLED
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#endif

// ESP-NOW broadcast MAC address (FF:FF:FF:FF:FF:FF)
static const uint8_t espnow_broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Define static instance pointer
InterfaceManager* InterfaceManager::_instance = nullptr;

InterfaceManager::InterfaceManager(PacketReceiverCallback receiver, RoutingTable& routingTable) :
    _packetReceiver(receiver),
    _routingTableRef(routingTable),
    // Use lambda to capture 'this' for the member function callback
    _serialKissProcessor([this](const std::vector<uint8_t>& data, InterfaceType iface){ this->handleKissPacket(data, iface); })
#if BLUETOOTH_CLASSIC_AVAILABLE
    , _bluetoothKissProcessor([this](const std::vector<uint8_t>& data, InterfaceType iface){ this->handleKissPacket(data, iface); })
#endif
#ifdef LORA_ENABLED
    , _lora(nullptr), _loraInitialized(false)
#endif
#ifdef HAM_MODEM_ENABLED
    , _hamModemKissProcessor([this](const std::vector<uint8_t>& data, InterfaceType iface){ this->handleKissPacket(data, iface); })
    , _hamModemInitialized(false)
#endif
#ifdef IPFS_ENABLED
    , _ipfsInitialized(false)
#endif
{
    if (_instance != nullptr) {
         // This should not happen if InterfaceManager is instantiated only once by ReticulumNode
         DebugSerial.println("! FATAL: Multiple InterfaceManager instances detected!");
         // Handle error: abort?
         return;
    }
    _instance = this; // Set the static instance pointer
}

void InterfaceManager::setup() {
    setupSerial(); // Assumes Serial.begin() already called
    
    // Initialize Bluetooth first (if available)
#if BLUETOOTH_CLASSIC_AVAILABLE
    setupBluetooth();
#endif
    
    // Configure WiFi power save mode BEFORE initializing WiFi
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);  // Use minimum power save mode when both BT and WiFi are active
    
    // Now setup WiFi and ESP-NOW
    setupWiFi();   // Sets mode, connects, starts UDP
    setupESPNow(); // Depends on WiFi mode being set
    
#ifdef LORA_ENABLED
    setupLoRa();
#endif

#ifdef HAM_MODEM_ENABLED
    setupHAMModem();
#endif

#ifdef IPFS_ENABLED
    setupIPFS();
#endif
    
    DebugSerial.println("Interface Manager Setup Complete.");
}

void InterfaceManager::loop() {
    // Process inputs from KISS interfaces
    processSerialInput();
#if BLUETOOTH_CLASSIC_AVAILABLE
    processBluetoothInput();
#endif

    // Process UDP input if WiFi is connected
    if (WiFi.status() == WL_CONNECTED) {
        processWiFiInput();
    }

#ifdef LORA_ENABLED
    processLoRaInput();
#endif

#ifdef HAM_MODEM_ENABLED
    processHAMModemInput();
#endif
}

void InterfaceManager::setupSerial() {
    // KissSerial (Serial2) is started in main.cpp for KISS interface
    DebugSerial.println("IF: KISS Serial interface ready on Serial2 (GPIO16/17).");
}

void InterfaceManager::setupWiFi() {
    WiFi.disconnect(true);  // Disconnect and turn off WiFi
    WiFi.mode(WIFI_OFF);   // Ensure WiFi is off before reconfiguring
    delay(100);            // Small delay to ensure WiFi is fully off
    
    WiFi.mode(WIFI_AP_STA); // ESP-NOW needs STA or AP mode active
    
    // Set WiFi to use reduced power mode when BT is active
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    DebugSerial.print("IF: Connecting to WiFi ");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500); DebugSerial.print("."); attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        DebugSerial.println("\nIF: WiFi connected.");
        DebugSerial.print("IF: IP address: "); DebugSerial.println(WiFi.localIP());
        if (_udp.begin(RNS_UDP_PORT)) {
            DebugSerial.print("IF: UDP Listening on port "); DebugSerial.println(RNS_UDP_PORT);
        } else {
            DebugSerial.println("! ERROR: Failed to start UDP listener!");
        }
    } else {
        DebugSerial.println("\n! IF: WiFi connection failed.");
        // Node might operate without WiFi, but UDP interface won't work
    }
}

void InterfaceManager::setupESPNow() {
     DebugSerial.print("IF: Device MAC: "); DebugSerial.println(WiFi.macAddress());
    if (esp_now_init() != ESP_OK) {
        DebugSerial.println("! ERROR: Initializing ESP-NOW failed!");
        return; // Cannot proceed with ESP-NOW
    }
    // Register static callback function which calls instance method
    esp_err_t result = esp_now_register_recv_cb(staticEspNowRecvCallback);
    if (result != ESP_OK) {
         DebugSerial.print("! ERROR: Failed to register ESP-NOW recv cb: "); DebugSerial.println(esp_err_to_name(result));
    }
    // esp_now_register_send_cb(staticEspNowSendCallback); // Optional: register send status callback

    // Add broadcast peer initially (needed to receive broadcasts)
    if (!addEspNowPeer(espnow_broadcast_mac)) {
         DebugSerial.println("! WARN: Failed to add initial ESP-NOW broadcast peer");
    }
    DebugSerial.println("IF: ESP-NOW Initialized.");
}

#if BLUETOOTH_CLASSIC_AVAILABLE
void InterfaceManager::setupBluetooth() {
     if (!_serialBT.begin(BT_DEVICE_NAME)) {
         DebugSerial.println("! ERROR: Bluetooth Serial initialization failed!");
     } else {
        DebugSerial.print("IF: Bluetooth ready. Device Name: ");
        DebugSerial.println(BT_DEVICE_NAME);
    }
}
#endif

// --- Input Processing ---
void InterfaceManager::processWiFiInput() {
    int packetSize = _udp.parsePacket();
    if (packetSize > 0) {
        if (packetSize > MAX_PACKET_SIZE) {
             DebugSerial.print("! WARN: Oversized UDP packet received ("); DebugSerial.print(packetSize); DebugSerial.println(" bytes), discarding.");
             _udp.flush(); // Discard data
             return;
        }

        // Use unique_ptr for automatic memory management
        std::unique_ptr<uint8_t[]> udpBuffer(new (std::nothrow) uint8_t[packetSize]);
        if (!udpBuffer) {
             DebugSerial.println("! ERROR: new failed for UDP buffer!");
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
     while (KissSerial.available()) {
        _serialKissProcessor.decodeByte(KissSerial.read(), InterfaceType::SERIAL_PORT);
    }
}

#if BLUETOOTH_CLASSIC_AVAILABLE
void InterfaceManager::processBluetoothInput() {
     while (_serialBT.available()) {
         _bluetoothKissProcessor.decodeByte(_serialBT.read(), InterfaceType::BLUETOOTH);
    }
}
#endif

// --- KISS Packet Handling ---
void InterfaceManager::handleKissPacket(const std::vector<uint8_t>& packetData, InterfaceType interface) {
     // Debug: Print raw received packet
     DebugSerial.print("[KISS] Received ");
     DebugSerial.print(packetData.size());
     DebugSerial.print(" bytes on interface ");
     DebugSerial.print(static_cast<int>(interface));
     DebugSerial.print(": ");
     for (size_t i = 0; i < min(packetData.size(), (size_t)20); i++) {
         if (packetData[i] < 0x10) DebugSerial.print("0");
         DebugSerial.print(packetData[i], HEX);
         DebugSerial.print(" ");
     }
     if (packetData.size() > 20) DebugSerial.print("...");
     DebugSerial.println();

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
        // DebugSerial.print("Broadcasting packet (no route found) for dest: "); Utils::printBytes(destinationAddr, RNS_ADDRESS_SIZE, Serial); DebugSerial.println(); // Verbose
        if (excludeInterface != InterfaceType::ESP_NOW) {
            sendPacketViaEspNow(packetBuffer, packetLen, nullptr); // Broadcast = null dest for internal func
        }
        if (WiFi.status() == WL_CONNECTED && excludeInterface != InterfaceType::WIFI_UDP) {
             sendPacketViaWiFi(packetBuffer, packetLen, nullptr); // Broadcast = null dest for internal func
        }
#ifdef LORA_ENABLED
        if (_loraInitialized && excludeInterface != InterfaceType::LORA) {
            sendPacketViaLoRa(packetBuffer, packetLen, nullptr);
        }
#endif
        // Broadcast on Serial/BT usually only for specific bridging applications, skip by default
        // if (excludeInterface != InterfaceType::SERIAL_PORT) { sendPacketViaSerial(packetBuffer, packetLen); }
#if BLUETOOTH_CLASSIC_AVAILABLE
        // if (_serialBT.connected() && excludeInterface != InterfaceType::BLUETOOTH) { sendPacketViaBluetooth(packetBuffer, packetLen); }
#endif
    }
}

void InterfaceManager::sendPacketVia(InterfaceType ifType, const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr) {
     if (!packetBuffer || packetLen == 0) return;
     switch(ifType) {
        case InterfaceType::ESP_NOW:  sendPacketViaEspNow(packetBuffer, packetLen, destinationAddr); break;
        case InterfaceType::WIFI_UDP: sendPacketViaWiFi(packetBuffer, packetLen, destinationAddr); break;
        case InterfaceType::SERIAL_PORT:   sendPacketViaSerial(packetBuffer, packetLen); break;
#if BLUETOOTH_CLASSIC_AVAILABLE
        case InterfaceType::BLUETOOTH:sendPacketViaBluetooth(packetBuffer, packetLen); break;
#endif
#ifdef LORA_ENABLED
        case InterfaceType::LORA: sendPacketViaLoRa(packetBuffer, packetLen, destinationAddr); break;
#endif
#ifdef HAM_MODEM_ENABLED
        case InterfaceType::HAM_MODEM: sendPacketViaHAMModem(packetBuffer, packetLen); break;
#endif
#ifdef IPFS_ENABLED
        case InterfaceType::IPFS: sendPacketViaIPFS(packetBuffer, packetLen, destinationAddr); break;
#endif
        default: DebugSerial.print("! WARN: sendPacketVia unsupported interface: "); DebugSerial.println(static_cast<int>(ifType)); break;
     }
}

void InterfaceManager::broadcastAnnounce(const uint8_t *packetBuffer, size_t packetLen) {
     if (!packetBuffer || packetLen == 0) return;
     // Use nullptr destination for broadcast variants
     sendPacketViaEspNow(packetBuffer, packetLen, nullptr);
     if (WiFi.status() == WL_CONNECTED) {
         sendPacketViaWiFi(packetBuffer, packetLen, nullptr);
     }
#ifdef LORA_ENABLED
     if (_loraInitialized) {
         sendPacketViaLoRa(packetBuffer, packetLen, nullptr);
     }
#endif
#ifdef HAM_MODEM_ENABLED
     if (_hamModemInitialized) {
         sendPacketViaHAMModem(packetBuffer, packetLen);
     }
#endif
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
    if (result != ESP_OK) { DebugSerial.print("! ESP-NOW Send Error to "); Utils::printBytes(targetMac, 6, DebugSerial); DebugSerial.print(": "); DebugSerial.println(esp_err_to_name(result)); }
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
        DebugSerial.println("! WARN: UDP Target IP is invalid, cannot send.");
        return;
    }

    _udp.beginPacket(targetIp, targetPort);
    size_t sent = _udp.write(packetBuffer, packetLen);
    if (sent != packetLen) { DebugSerial.print("! WARN: UDP write incomplete (sent "); DebugSerial.print(sent); DebugSerial.print("/"); DebugSerial.print(packetLen); DebugSerial.println(" bytes)"); }
    if (!_udp.endPacket()) { DebugSerial.println("! ERROR: UDP endPacket failed!"); }
}

// KISS interface sends packaets over dedicated serial link
void InterfaceManager::sendPacketViaSerial(const uint8_t *packetBuffer, size_t packetLen) {
    std::vector<uint8_t> kissEncoded;
    KISSProcessor::encode(packetBuffer, packetLen, kissEncoded);
    size_t sent = KissSerial.write(kissEncoded.data(), kissEncoded.size());
    // if(sent != kissEncoded.size()) { DebugSerial.println("! WARN: Serial write incomplete"); } // Optional check
}
#if BLUETOOTH_CLASSIC_AVAILABLE
void InterfaceManager::sendPacketViaBluetooth(const uint8_t *packetBuffer, size_t packetLen) {
    if (!_serialBT.connected()) return;
    std::vector<uint8_t> kissEncoded;
    KISSProcessor::encode(packetBuffer, packetLen, kissEncoded);
    size_t sent = _serialBT.write(kissEncoded.data(), kissEncoded.size());
     // if(sent != kissEncoded.size()) { DebugSerial.println("! WARN: Bluetooth write incomplete"); } // Optional check
}
#endif

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
         DebugSerial.print("! ERROR: Failed to add ESP-NOW peer "); Utils::printBytes(mac_addr, 6, DebugSerial); DebugSerial.print(": "); DebugSerial.println(esp_err_to_name(add_result));
         return false;
    }
    DebugSerial.print("IF: Added ESP-NOW peer: "); Utils::printBytes(mac_addr, 6, DebugSerial); DebugSerial.println();
    return true;
}

bool InterfaceManager::removeEspNowPeer(const uint8_t* mac_addr) {
     if (!mac_addr) return false;
     if (!checkEspNowPeer(mac_addr)) return false; // Not found

     esp_err_t del_result = esp_now_del_peer(mac_addr);
     if (del_result != ESP_OK) {
         DebugSerial.print("! WARN: Failed to delete ESP-NOW peer "); Utils::printBytes(mac_addr, 6, DebugSerial); DebugSerial.print(": "); DebugSerial.println(esp_err_to_name(del_result));
         return false;
     }
     DebugSerial.print("IF: Removed ESP-NOW peer: "); Utils::printBytes(mac_addr, 6, DebugSerial); DebugSerial.println();
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
             DebugSerial.print("! WARN: Oversized ESP-NOW packet received ("); DebugSerial.print(len); DebugSerial.println(" bytes), discarding.");
        }
    }
}

/* Optional Static Send Callback
void InterfaceManager::staticEspNowSendCallback(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (_instance && mac_addr) {
        // Could notify routing table or link manager about send status
        // DebugSerial.print("IF: ESP-NOW Send Status to MAC "); Utils::printBytes(mac_addr, 6, Serial); DebugSerial.print(": "); DebugSerial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
    }
}
*/

#ifdef LORA_ENABLED
// --- LoRa Implementation ---
void InterfaceManager::setupLoRa() {
    DebugSerial.println("IF: Initializing LoRa...");
    
    // Initialize SPI for LoRa
    // Use VSPI (SPI3) for ESP32, HSPI for other variants
    #if defined(CONFIG_IDF_TARGET_ESP32)
        SPIClass* spi = new SPIClass(VSPI);
    #else
        SPIClass* spi = new SPIClass(HSPI);
    #endif
    
    spi->begin(LORA_SPI_SCK, LORA_SPI_MISO, LORA_SPI_MOSI, LORA_CS_PIN);
    
    // Create LoRa module instance with SPI settings
    Module* loraModule = new Module(LORA_CS_PIN, LORA_DIO0_PIN, LORA_RST_PIN, RADIOLIB_NC, *spi, SPISettings(2000000, MSBFIRST, SPI_MODE0));
    
    // Create SX1278 instance
    _lora = new SX1278(loraModule);
    
    // Initialize LoRa with configuration
    int state = _lora->begin(LORA_FREQUENCY, LORA_BANDWIDTH, LORA_SPREADING_FACTOR, LORA_CODING_RATE, LORA_SYNC_WORD, LORA_OUTPUT_POWER, LORA_PREAMBLE_LENGTH, LORA_GAIN);
    
    if (state == RADIOLIB_ERR_NONE) {
        _loraInitialized = true;
        DebugSerial.println("IF: LoRa initialized successfully.");
        DebugSerial.print("IF: Frequency: "); DebugSerial.print(LORA_FREQUENCY); DebugSerial.println(" MHz");
        DebugSerial.print("IF: Bandwidth: "); DebugSerial.print(LORA_BANDWIDTH); DebugSerial.println(" kHz");
        DebugSerial.print("IF: Spreading Factor: "); DebugSerial.println(LORA_SPREADING_FACTOR);
    } else {
        _loraInitialized = false;
        DebugSerial.print("! ERROR: LoRa initialization failed with code: ");
        DebugSerial.println(state);
        delete _lora;
        _lora = nullptr;
    }
}

void InterfaceManager::processLoRaInput() {
    if (!_loraInitialized || !_lora) return;
    
    // Check if data is available
    if (_lora->available()) {
        // Determine packet size (LoRa can receive variable length packets)
        size_t packetSize = _lora->getPacketLength();
        if (packetSize == 0 || packetSize > MAX_PACKET_SIZE) {
            DebugSerial.print("! WARN: Invalid LoRa packet size: ");
            DebugSerial.println(packetSize);
            _lora->clearIRQFlags();
            return;
        }
        
        // Allocate buffer for received packet
        std::unique_ptr<uint8_t[]> loraBuffer(new (std::nothrow) uint8_t[packetSize]);
        if (!loraBuffer) {
            DebugSerial.println("! ERROR: Failed to allocate LoRa receive buffer!");
            _lora->clearIRQFlags();
            return;
        }
        
        // Read packet data
        int state = _lora->readData(loraBuffer.get(), packetSize);
        if (state == RADIOLIB_ERR_NONE) {
            // Pass received packet to packet receiver callback
            // LoRa doesn't have MAC addresses, so use nullptr
            if (_packetReceiver) {
                _packetReceiver(loraBuffer.get(), packetSize, InterfaceType::LORA, nullptr, IPAddress(), 0);
            }
        } else {
            DebugSerial.print("! WARN: LoRa read failed with code: ");
            DebugSerial.println(state);
        }
        
        // Clear IRQ flags to prepare for next packet
        _lora->clearIRQFlags();
    }
}

void InterfaceManager::sendPacketViaLoRa(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr) {
    if (!_loraInitialized || !_lora || !packetBuffer || packetLen == 0) return;
    
    // LoRa is broadcast by nature, so we ignore destinationAddr for now
    // In a more advanced implementation, you could use different frequencies/channels for different destinations
    
    // Send packet
    int state = _lora->transmit(packetBuffer, packetLen);
    if (state == RADIOLIB_ERR_NONE) {
        // Success - packet sent
        // DebugSerial.println("IF: LoRa packet sent successfully.");
    } else {
        DebugSerial.print("! ERROR: LoRa transmit failed with code: ");
        DebugSerial.println(state);
    }
}
#endif

#ifdef HAM_MODEM_ENABLED
// --- HAM Modem Implementation ---
void InterfaceManager::setupHAMModem() {
    DebugSerial.println("IF: Initializing HAM Modem...");
    
    // Initialize serial port for HAM modem (typically connected to TNC)
    HAM_MODEM_SERIAL.begin(HAM_MODEM_BAUD, SERIAL_8N1, HAM_MODEM_RX_PIN, HAM_MODEM_TX_PIN);
    
    // Most HAM TNCs use KISS protocol, which we already support
    // The KISS processor will handle incoming packets
    _hamModemInitialized = true;
    
    DebugSerial.println("IF: HAM Modem initialized (KISS protocol).");
    DebugSerial.print("IF: Baud rate: "); DebugSerial.println(HAM_MODEM_BAUD);
    DebugSerial.print("IF: Callsign: "); DebugSerial.println(APRS_CALLSIGN);
}

void InterfaceManager::processHAMModemInput() {
    if (!_hamModemInitialized) return;
    
    // Process incoming bytes from HAM modem via KISS
    while (HAM_MODEM_SERIAL.available()) {
        _hamModemKissProcessor.decodeByte(HAM_MODEM_SERIAL.read(), InterfaceType::HAM_MODEM);
    }
}

void InterfaceManager::sendPacketViaHAMModem(const uint8_t *packetBuffer, size_t packetLen) {
    if (!_hamModemInitialized || !packetBuffer || packetLen == 0) return;
    
    // Encode packet with KISS framing
    std::vector<uint8_t> kissEncoded;
    KISSProcessor::encode(packetBuffer, packetLen, kissEncoded);
    
    // Send via HAM modem serial port
    size_t sent = HAM_MODEM_SERIAL.write(kissEncoded.data(), kissEncoded.size());
    if (sent != kissEncoded.size()) {
        DebugSerial.println("! WARN: HAM Modem write incomplete");
    }
}

void InterfaceManager::sendAPRSPacket(const char* destination, const char* message) {
    if (!_hamModemInitialized) {
        DebugSerial.println("! ERROR: HAM Modem not initialized for APRS");
        return;
    }
    
    // APRS packet format: SOURCE>DEST,RELAY:message
    // This is a simplified implementation - full APRS would require AX.25 encoding
    String aprsPacket = String(APRS_CALLSIGN);
    aprsPacket += "-";
    aprsPacket += String(APRS_SSID);
    aprsPacket += ">";
    aprsPacket += String(destination);
    aprsPacket += ":";
    aprsPacket += String(message);
    
    DebugSerial.print("IF: Sending APRS packet: ");
    DebugSerial.println(aprsPacket);
    
    // For now, send as plain text over KISS
    // In a full implementation, this would be encoded as AX.25 frames
    std::vector<uint8_t> aprsData(aprsPacket.length());
    memcpy(aprsData.data(), aprsPacket.c_str(), aprsPacket.length());
    
    std::vector<uint8_t> kissEncoded;
    KISSProcessor::encode(aprsData.data(), aprsData.size(), kissEncoded);
    HAM_MODEM_SERIAL.write(kissEncoded.data(), kissEncoded.size());
}

void InterfaceManager::sendAPRSPosition(float lat, float lon, float altitude, const char* comment) {
    if (!_hamModemInitialized) {
        DebugSerial.println("! ERROR: HAM Modem not initialized for APRS");
        return;
    }
    
    // Format: !DDMM.MMNS/DDDMM.MMEW[comment]
    char latStr[10], lonStr[11];
    int latDeg = abs((int)lat);
    int lonDeg = abs((int)lon);
    float latMin = (abs(lat) - latDeg) * 60.0;
    float lonMin = (abs(lon) - lonDeg) * 60.0;
    
    snprintf(latStr, sizeof(latStr), "%02d%05.2f", latDeg, latMin);
    snprintf(lonStr, sizeof(lonStr), "%03d%05.2f", lonDeg, lonMin);
    
    String aprsPacket = String(APRS_CALLSIGN);
    aprsPacket += "-";
    aprsPacket += String(APRS_SSID);
    aprsPacket += ">APRS:!";
    aprsPacket += String(latStr);
    aprsPacket += (lat >= 0) ? "N" : "S";
    aprsPacket += String(APRS_SYMBOL);
    aprsPacket += String(lonStr);
    aprsPacket += (lon >= 0) ? "E" : "W";
    if (altitude > 0) {
        aprsPacket += "/A=";
        aprsPacket += String((int)(altitude * 3.28084)); // Convert to feet
    }
    if (comment && strlen(comment) > 0) {
        aprsPacket += String(comment);
    }
    
    DebugSerial.print("IF: Sending APRS position: ");
    DebugSerial.println(aprsPacket);
    
    std::vector<uint8_t> aprsData(aprsPacket.length());
    memcpy(aprsData.data(), aprsPacket.c_str(), aprsPacket.length());
    
    std::vector<uint8_t> kissEncoded;
    KISSProcessor::encode(aprsData.data(), aprsData.size(), kissEncoded);
    HAM_MODEM_SERIAL.write(kissEncoded.data(), kissEncoded.size());
}

void InterfaceManager::sendAPRSWeather(float temp, float humidity, float pressure, const char* comment) {
    if (!_hamModemInitialized) {
        DebugSerial.println("! ERROR: HAM Modem not initialized for APRS");
        return;
    }
    
    // Format: _DDHHMMc...s...g...t...r...p...P...h..b...
    String aprsPacket = String(APRS_CALLSIGN);
    aprsPacket += "-";
    aprsPacket += String(APRS_SSID);
    aprsPacket += ">APRS:_";
    
    // Time (simplified - use current time if available)
    aprsPacket += "000000";  // Placeholder for time
    
    // Wind direction (3 digits, 000-360)
    aprsPacket += "000";
    
    // Wind speed (3 digits, mph)
    aprsPacket += "000";
    
    // Gust speed (3 digits, mph)
    aprsPacket += "000";
    
    // Temperature (3 digits, Fahrenheit, with sign)
    int tempF = (int)(temp * 9.0/5.0 + 32.0);
    if (tempF < 0) {
        aprsPacket += "/";
        tempF = -tempF;
    } else {
        aprsPacket += "c";
    }
    char tempStr[4];
    snprintf(tempStr, sizeof(tempStr), "%03d", tempF);
    aprsPacket += String(tempStr);
    
    // Rainfall (3 digits, 1 hour, hundredths of inches)
    aprsPacket += "000";
    
    // Rainfall (3 digits, 24 hour)
    aprsPacket += "000";
    
    // Rainfall (3 digits, since midnight)
    aprsPacket += "000";
    
    // Humidity (2 digits, percent)
    char humStr[3];
    snprintf(humStr, sizeof(humStr), "%02d", (int)humidity);
    aprsPacket += String(humStr);
    
    // Pressure (5 digits, millibars, with decimal)
    char pressStr[6];
    snprintf(pressStr, sizeof(pressStr), "%05d", (int)(pressure * 10));
    aprsPacket += String(pressStr);
    
    if (comment && strlen(comment) > 0) {
        aprsPacket += String(comment);
    }
    
    DebugSerial.print("IF: Sending APRS weather: ");
    DebugSerial.println(aprsPacket);
    
    std::vector<uint8_t> aprsData(aprsPacket.length());
    memcpy(aprsData.data(), aprsPacket.c_str(), aprsPacket.length());
    
    std::vector<uint8_t> kissEncoded;
    KISSProcessor::encode(aprsData.data(), aprsData.size(), kissEncoded);
    HAM_MODEM_SERIAL.write(kissEncoded.data(), kissEncoded.size());
}

void InterfaceManager::sendAPRSMessage(const char* addressee, const char* message) {
    if (!_hamModemInitialized) {
        DebugSerial.println("! ERROR: HAM Modem not initialized for APRS");
        return;
    }
    
    // Format: :ADDRESSEE :message{message_id}
    String aprsPacket = String(APRS_CALLSIGN);
    aprsPacket += "-";
    aprsPacket += String(APRS_SSID);
    aprsPacket += ">APRS::";
    
    // Addressee (9 characters, padded with spaces)
    char addr[10] = {0};
    strncpy(addr, addressee, 9);
    for (int i = strlen(addr); i < 9; i++) {
        addr[i] = ' ';
    }
    aprsPacket += String(addr);
    aprsPacket += ":";
    aprsPacket += String(message);
    
    DebugSerial.print("IF: Sending APRS message: ");
    DebugSerial.println(aprsPacket);
    
    std::vector<uint8_t> aprsData(aprsPacket.length());
    memcpy(aprsData.data(), aprsPacket.c_str(), aprsPacket.length());
    
    std::vector<uint8_t> kissEncoded;
    KISSProcessor::encode(aprsData.data(), aprsData.size(), kissEncoded);
    HAM_MODEM_SERIAL.write(kissEncoded.data(), kissEncoded.size());
}
#endif

#ifdef IPFS_ENABLED
// --- IPFS Implementation (Lightweight Client) ---
void InterfaceManager::setupIPFS() {
    DebugSerial.println("IF: Initializing IPFS client...");
    
    // IPFS client is lightweight - just needs WiFi connection
    if (WiFi.status() == WL_CONNECTED) {
        _ipfsInitialized = true;
        DebugSerial.print("IF: IPFS Gateway: ");
        DebugSerial.println(IPFS_GATEWAY_URL);
        DebugSerial.println("IF: IPFS client ready (gateway mode).");
    } else {
        _ipfsInitialized = false;
        DebugSerial.println("! WARN: IPFS requires WiFi connection. Disabled.");
    }
}

bool InterfaceManager::fetchIPFSContent(const char* ipfsHash, std::vector<uint8_t>& output) {
    if (!_ipfsInitialized || WiFi.status() != WL_CONNECTED) {
        DebugSerial.println("! ERROR: IPFS not available (WiFi not connected)");
        return false;
    }
    
    if (!ipfsHash || strlen(ipfsHash) == 0) {
        DebugSerial.println("! ERROR: Invalid IPFS hash");
        return false;
    }
    
    // Build URL: gateway + hash
    String url = String(IPFS_GATEWAY_URL) + String(ipfsHash);
    
    DebugSerial.print("IF: Fetching IPFS content: ");
    DebugSerial.println(url);
    
    _httpClient.begin(url);
    _httpClient.setTimeout(IPFS_TIMEOUT_MS);
    
    int httpCode = _httpClient.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        int contentLength = _httpClient.getSize();
        
        if (contentLength > 0 && contentLength <= IPFS_MAX_CONTENT_SIZE) {
            // Read content
            WiFiClient* stream = _httpClient.getStreamPtr();
            output.resize(contentLength);
            
            size_t bytesRead = 0;
            while (bytesRead < (size_t)contentLength && stream->available()) {
                bytesRead += stream->readBytes(output.data() + bytesRead, contentLength - bytesRead);
            }
            
            _httpClient.end();
            DebugSerial.print("IF: IPFS content fetched: ");
            DebugSerial.print(bytesRead);
            DebugSerial.println(" bytes");
            return true;
        } else {
            DebugSerial.print("! ERROR: IPFS content too large: ");
            DebugSerial.println(contentLength);
            _httpClient.end();
            return false;
        }
    } else {
        DebugSerial.print("! ERROR: IPFS fetch failed, HTTP code: ");
        DebugSerial.println(httpCode);
        _httpClient.end();
        return false;
    }
}

bool InterfaceManager::publishToIPFS(const uint8_t* data, size_t len, String& ipfsHash) {
    if (!_ipfsInitialized || WiFi.status() != WL_CONNECTED) {
        DebugSerial.println("! ERROR: IPFS not available (WiFi not connected)");
        return false;
    }
    
#if IPFS_LOCAL_NODE_ENABLED
    // Use local IPFS node API
    String url = String(IPFS_LOCAL_NODE_URL) + "/api/v0/add";
    
    DebugSerial.print("IF: Publishing to IPFS via local node: ");
    DebugSerial.println(url);
    
    _httpClient.begin(url);
    _httpClient.setTimeout(IPFS_PUBLISH_TIMEOUT_MS);
    _httpClient.addHeader("Content-Type", "multipart/form-data");
    
    // Create multipart form data
    String boundary = "----WebKitFormBoundary" + String(random(10000, 99999));
    String formData = "--" + boundary + "\r\n";
    formData += "Content-Disposition: form-data; name=\"file\"; filename=\"data.bin\"\r\n";
    formData += "Content-Type: application/octet-stream\r\n\r\n";
    
    // Note: This is a simplified implementation
    // Full multipart encoding would be more complex
    int httpCode = _httpClient.POST(data, len);
    
    if (httpCode == HTTP_CODE_OK) {
        String response = _httpClient.getString();
        
        // Parse JSON response to extract hash
        // Response format: {"Name":"data.bin","Hash":"Qm...","Size":"123"}
        int hashStart = response.indexOf("\"Hash\":\"");
        if (hashStart >= 0) {
            hashStart += 8;
            int hashEnd = response.indexOf("\"", hashStart);
            if (hashEnd > hashStart) {
                ipfsHash = response.substring(hashStart, hashEnd);
                _httpClient.end();
                DebugSerial.print("IF: IPFS content published, hash: ");
                DebugSerial.println(ipfsHash);
                return true;
            }
        }
        _httpClient.end();
        DebugSerial.println("! ERROR: Failed to parse IPFS response");
        return false;
    } else {
        DebugSerial.print("! ERROR: IPFS publish failed, HTTP code: ");
        DebugSerial.println(httpCode);
        _httpClient.end();
        return false;
    }
#else
    // No local node - suggest alternatives
    DebugSerial.println("! WARN: IPFS local node not enabled.");
    DebugSerial.println("! INFO: Set IPFS_LOCAL_NODE_ENABLED to 1 in Config.h");
    DebugSerial.println("! INFO: Or use a pinning service (Pinata, Infura, etc.)");
    return false;
#endif
}

void InterfaceManager::sendPacketViaIPFS(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr) {
    // IPFS is primarily for content addressing, not direct packet transmission
    // This could be used to:
    // 1. Store packet content in IPFS
    // 2. Send IPFS hash as a reference in Reticulum packet
    // 3. Recipient fetches content from IPFS using hash
    
    if (!_ipfsInitialized) {
        DebugSerial.println("! WARN: IPFS not initialized, cannot send packet");
        return;
    }
    
    // For now, this is a placeholder
    // In a full implementation, you would:
    // 1. Publish packet content to IPFS
    // 2. Create a Reticulum packet containing the IPFS hash
    // 3. Send the Reticulum packet via normal interfaces
    
    DebugSerial.println("! INFO: IPFS packet sending requires publishing implementation.");
}
#endif
