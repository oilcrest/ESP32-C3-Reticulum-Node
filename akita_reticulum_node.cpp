#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <BluetoothSerial.h>
#include <esp_wifi.h>
#include <cstring>
#include <random>
#include <EEPROM.h>

// Reticulum Packet Structure (Full)
struct ReticulumPacket {
  uint8_t destination[8];
  uint8_t source[8];
  uint16_t packetId;
  uint8_t flags;
  uint8_t data[250];
  size_t dataLength;
};

// WiFi Configuration
const char *ssid = "YourWiFiSSID"; // Replace with your WiFi SSID
const char *password = "YourWiFiPassword"; // Replace with your WiFi password

// ESP-NOW Configuration
uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Bluetooth Configuration
BluetoothSerial SerialBT;

// Node Address
uint8_t nodeAddress[8];
const int EEPROM_ADDRESS = 0; // EEPROM address to store node address
const int EEPROM_SIZE = 512;

// Function Prototypes
void setupWiFi();
void setupESPNow();
void setupBluetooth();
void onDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len);
void sendPacket(const ReticulumPacket &packet);
void processUSBSerial();
void processBluetoothSerial();
void handleIncomingPacket(const ReticulumPacket &packet);
void extractReticulumAddress(const uint8_t *data, uint8_t *address);
void injectReticulumAddress(const uint8_t *data, const uint8_t *address);
void generateRandomAddress(uint8_t *address);
ReticulumPacket createPacket(const uint8_t *destination, const uint8_t *data, size_t dataLength);
void loadAddressFromEEPROM();
void saveAddressToEEPROM();
void handleCommand(const String &command);
void printNodeAddress();

void setup() {
  Serial.begin(115200);
  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("EEPROM Initialization Failed!");
  }
  loadAddressFromEEPROM();
  if (memcmp(nodeAddress, "\x00\x00\x00\x00\x00\x00\x00\x00", 8) == 0) {
    generateRandomAddress(nodeAddress);
    saveAddressToEEPROM();
  }
  printNodeAddress();
  setupWiFi();
  setupESPNow();
  setupBluetooth();
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
}

void loop() {
  processUSBSerial();
  processBluetoothSerial();
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED){
    Serial.println("\nWiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed.");
  }
}

void setupESPNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(onDataRecv);
  esp_now_peer_info_t peerInfo;
  memcpy(peerInfo.peer_addr, broadcastMac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add ESP-NOW peer");
  }
}

void setupBluetooth() {
  SerialBT.begin("ESP32-C3-Reticulum");
  Serial.println("Bluetooth started! Ready to pair...");
}

void onDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
  ReticulumPacket packet;
  memcpy(packet.source, mac_addr, 6);
  memcpy(packet.data, incomingData, len);
  packet.dataLength = len;
  extractReticulumAddress(packet.data, packet.destination);
  extractReticulumAddress(packet.data + 8, packet.source);
  packet.dataLength -= 16;
  memmove(packet.data, packet.data + 16, packet.dataLength);
  handleIncomingPacket(packet);
}

void sendPacket(const ReticulumPacket &packet) {
  uint8_t sendData[packet.dataLength + 16];
  injectReticulumAddress(sendData, packet.destination);
  injectReticulumAddress(sendData + 8, packet.source);
  memcpy(sendData + 16, packet.data, packet.dataLength);
  esp_now_send(broadcastMac, sendData, packet.dataLength + 16);
}

void processUSBSerial() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    handleCommand(command);
  }
}

void processBluetoothSerial() {
  if (SerialBT.available()) {
    String command = SerialBT.readStringUntil('\n');
    command.trim();
    handleCommand(command);
  }
}

void handleCommand(const String &command) {
  if (command.startsWith("send ")) {
    String addressHex = command.substring(5, 27);
    String data = command.substring(28);
    uint8_t destination[8];
    for (int i = 0; i < 8; i++) {
      destination[i] = strtoul(addressHex.substring(i * 2, (i * 2) + 2).c_str(), nullptr, 16);
    }
    ReticulumPacket packet = createPacket(destination, (uint8_t *)data.c_str(), data.length());
    sendPacket(packet);
  } else if (command == "address") {
    printNodeAddress();
  } else {
    Serial.println("Unknown command.");
  }
}

void handleIncomingPacket(const ReticulumPacket &packet) {
  Serial.print("Received packet, forwarding.\n");
  sendPacket(packet);
}

void extractReticulumAddress(const uint8_t *data, uint8_t *address) {
  memcpy(address, data, 8);
}

void injectReticulumAddress(const uint8_t *data, const uint8_t *address) {
  memcpy(data, address, 8);
}

void generateRandomAddress(uint8_t *address) {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 255);
  for (int i = 0; i < 8; i++) {
    address[i] = dis(gen);
  }
}

ReticulumPacket createPacket(const uint8_t *destination, const uint8_t *data, size_t dataLength) {
  ReticulumPacket packet;
  memcpy(packet.destination, destination, 8);
  memcpy(packet.source, nodeAddress, 8);
  memcpy(packet.data, data, dataLength);
  packet.dataLength = dataLength;
  return packet;
}

void loadAddressFromEEPROM() {
  for (int i = 0; i < 8; i++) {
    nodeAddress[i] = EEPROM.read(EEPROM_ADDRESS + i);
  }
}

void saveAddressToEEPROM() {
  for (int i = 0; i < 8; i++) {
    EEPROM.write(EEPROM_ADDRESS + i, nodeAddress[i]);
  }
  if (!EEPROM.commit()) {
    Serial.println("EEPROM commit failed");
  }
}

void printNodeAddress() {
  Serial.print("Node Address: ");
  for (int i = 0; i < 8; i++) {
    Serial.print(nodeAddress[i], HEX);
    if (i < 7) Serial.print(":");
  }
Serial.println();
}
