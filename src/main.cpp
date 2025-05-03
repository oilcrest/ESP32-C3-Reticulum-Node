#include <Arduino.h>
#include "ReticulumNode.h"
#include "Utils.h" // For printBytes if needed here

// Global instance of the main node application class
ReticulumNode reticulumNode;

// Example Application Data Handler function
// This function will be called by ReticulumNode when data arrives over a Link
void myAppDataReceiver(const uint8_t* source_address, const std::vector<uint8_t>& data) {
  Serial.print("<<<< App Layer Received ");
  Serial.print(data.size());
  Serial.print(" bytes from ");
  Utils::printBytes(source_address, RNS_ADDRESS_SIZE, Serial);
  Serial.print(": \"");
  for(uint8_t byte : data) {
    if(isprint(byte)) Serial.print((char)byte); else Serial.print('.');
  }
  Serial.println("\"");

  // TODO: Add your application logic here
  // e.g., control an LED, send data to another interface, display on screen...
}


void setup() {
  // Start Serial for debugging FIRST
  Serial.begin(115200);
  // Wait a moment for serial plotter/monitor to connect
  // while (!Serial && millis() < 2000); // Optional wait
  Serial.println("\n\n===================================");
  Serial.println(" ESP32 Reticulum Gateway - Booting ");
  Serial.println("===================================");

  // Initialize the Reticulum node subsystems
  reticulumNode.setup();

  // Register the application data handler
  reticulumNode.setAppDataHandler(myAppDataReceiver);

  Serial.println("-----------------------------------");
  Serial.println(" Setup Complete. Entering main loop.");
  Serial.println("-----------------------------------");
}

void loop() {
  // Run the main node loop function
  reticulumNode.loop();
}
