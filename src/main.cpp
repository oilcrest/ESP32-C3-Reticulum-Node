#include <Arduino.h>
#include "ReticulumNode.h"
#include "Utils.h" // For printBytes if needed here

// Global instance of the main node application class
ReticulumNode reticulumNode;

// Example Application Data Handler function
// This function will be called by ReticulumNode when data arrives over a Link
void myAppDataReceiver(const uint8_t *source_address, const std::vector<uint8_t> &data)
{
  DebugSerial.print("<<<< App Layer Received ");
  DebugSerial.print(data.size());
  DebugSerial.print(" bytes from ");
  Utils::printBytes(source_address, RNS_ADDRESS_SIZE, DebugSerial);
  DebugSerial.print(": \"");
  for (uint8_t byte : data)
  {
    if (isprint(byte))
      DebugSerial.print((char)byte);
    else
      DebugSerial.print('.');
  }
  DebugSerial.println("\"");

  // TODO: Add your application logic here
  // e.g., control an LED, send data to another interface, display on screen...
}

void setup()
{
  // Initialize Serial (USB/UART0) for debug output - normal Arduino Serial Monitor
  DebugSerial.begin(115200);
  delay(100); // Give Serial time to initialize

  // Start KISS serial interface (platform-specific UART)
  // Note: Pin configuration is handled in Config.h based on platform
  KissSerial.begin(KISS_SERIAL_SPEED, SERIAL_8N1, KISS_UART_RX, KISS_UART_TX);

  DebugSerial.println("\n\n===================================");
  DebugSerial.println(" ESP32 Reticulum Gateway - Booting ");
  DebugSerial.println("===================================");

  // Initialize the Reticulum node subsystems
  reticulumNode.setup();

  // Register the application data handler
  reticulumNode.setAppDataHandler(myAppDataReceiver);

  DebugSerial.println("-----------------------------------");
  DebugSerial.println(" Setup Complete. Entering main loop.");
  DebugSerial.println("-----------------------------------");
}

void loop()
{
  // Run the main node loop function
  reticulumNode.loop();

  // SEND MESSAGE EVERY 10 SECONDS
  static uint32_t last_send = 0;
  if (millis() - last_send >= 10000) {
    last_send = millis();

    // Full 16-byte destination hash for PLAIN destination ["esp32", "node"]
    // Calculated using tests/get_dest_hash.py
    // This matches the Python receiver in tests/readhellofromesp.py
    uint8_t dest_hash[16] = {0xB6, 0x01, 0x0E, 0xA1, 0x1F, 0xDF, 0xC0, 0x4E,
                             0x01, 0x88, 0x3B, 0xD6, 0x06, 0xC5, 0x42, 0xD7};

    // Prepare message payload
    const char* msg = "Hola desde ESP32";
    std::vector<uint8_t> payload((uint8_t*)msg, (uint8_t*)msg + strlen(msg));

    // Serialize packet using OFFICIAL Reticulum wire format
    // Format: [FLAGS 1] [HOPS 1] [DEST_HASH 16] [CONTEXT 1] [DATA]
    uint8_t buffer[MAX_PACKET_SIZE];
    size_t packet_len = 0;

    bool success = ReticulumPacket::serialize(
      buffer, packet_len,
      dest_hash,                        // Full 16-byte destination hash
      RNS_PACKET_DATA,                  // Packet type: DATA
      RNS_DEST_PLAIN,                   // Destination type: PLAIN (unencrypted)
      RNS_PROPAGATION_BROADCAST,        // Propagation: BROADCAST
      RNS_CONTEXT_NONE,                 // Context: NONE
      0,                                // Hops: 0 (originating packet)
      payload                           // Data payload
    );

    if (success) {
      // Debug: Print packet details before sending
      DebugSerial.println("\n==== SENDING PACKET (Official Reticulum Format) ====");
      DebugSerial.print("Packet size: ");
      DebugSerial.println(packet_len);
      DebugSerial.print("Destination hash (16 bytes): ");
      Utils::printBytes(dest_hash, 16, DebugSerial);
      DebugSerial.println();
      DebugSerial.print("Message: ");
      DebugSerial.println(msg);
      DebugSerial.print("Raw packet (hex): ");
      for (size_t i = 0; i < packet_len; i++) {
        if (buffer[i] < 0x10) DebugSerial.print("0");
        DebugSerial.print(buffer[i], HEX);
      }
      DebugSerial.println();
      DebugSerial.println("========================================\n");

      // Send via Serial interface with KISS framing
      reticulumNode.getInterfaceManager().sendPacketVia(InterfaceType::SERIAL_PORT, buffer, packet_len, dest_hash);
    } else {
      DebugSerial.println("ERROR: Failed to serialize packet!");
    }
  }
}
