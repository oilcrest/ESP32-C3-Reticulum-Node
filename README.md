# ESP32-C3 Reticulum Node - Akita Engineering

This project, developed by Akita Engineering, provides a ready-to-deploy Reticulum node implementation for the ESP32-C3 microcontroller. It allows you to create a plug-and-play Reticulum node that can be accessed via WiFi, ESP-NOW, USB serial, or Bluetooth.

## Features

* **Reticulum Packet Handling:** Implements the full Reticulum packet structure with 8-byte source and destination addresses.
* **Multiple Communication Interfaces:** Supports WiFi, ESP-NOW, USB serial, and Bluetooth communication.
* **Persistent Node Address:** Stores the node's Reticulum address in EEPROM, ensuring it persists across power cycles.
* **Random Address Generation:** Generates a random Reticulum address on first boot if no address is stored in EEPROM.
* **Simplified Serial Interface:** Provides a simple text-based serial interface for sending Reticulum packets and displaying node information.
* **Robust Error Handling:** Includes error checking for EEPROM, WiFi, and ESP-NOW operations.
* **Plug-and-Play:** Designed to be as self-contained as possible, requiring minimal configuration.

## Getting Started

### Prerequisites

* Arduino IDE
* ESP32 board support installed in Arduino IDE
* ESP32-C3 microcontroller

### Installation

1.  **Clone the Repository:**
    ```bash
    git clone [repository_url]
    cd [repository_directory]
    ```
2.  **Open the Arduino IDE:**
    * Open the `[your_sketch_name].ino` file in the Arduino IDE.
3.  **Configure WiFi:**
    * Replace `"YourWiFiSSID"` and `"YourWiFiPassword"` in the code with your actual WiFi network credentials.
4.  **Select Board and Port:**
    * Select the ESP32-C3 board from the "Tools" > "Board" menu.
    * Select the correct port from the "Tools" > "Port" menu.
5.  **Upload the Code:**
    * Click the "Upload" button to upload the code to your ESP32-C3.

### Usage

1.  **Serial Monitor:**
    * Open the Serial Monitor in the Arduino IDE (set the baud rate to 115200).
    * The node's Reticulum address will be displayed.
2.  **Serial Commands:**
    * `send <destination_address> <data>`: Sends a Reticulum packet to the specified destination.
        * `<destination_address>`: 16-character hexadecimal Reticulum address (e.g., `0001020304050607`).
        * `<data>`: The payload of the packet.
    * `address`: Displays the node's Reticulum address.
3.  **Laptop Integration:**
    * Use a Reticulum library or software on your laptop to communicate with the ESP32-C3 via serial (USB or Bluetooth) or WiFi.
    * Configure your laptop's Reticulum software to use the ESP32-C3 as a gateway.
4.  **Bluetooth Pairing:**
    * If using Bluetooth, pair your laptop with the ESP32-C3 ("ESP32-C3-Reticulum").
5.  **Testing:**
    * Send Reticulum packets from your laptop.
    * Observe the Serial Monitor on the ESP32-C3 to verify that packets are being received and forwarded.
    * Test ESP-NOW by using another ESP32 device that is sending Reticulum packets.

### Considerations

* **Reticulum Software:** Ensure you have appropriate Reticulum software on your laptop.
* **Address Management:** Assign unique Reticulum addresses to all devices on your network.
* **EEPROM Wear:** EEPROM has a limited number of write cycles.
* **Security:** This implementation does not include security features.
* **Testing:** Thoroughly test your setup in a safe environment.
* **Power usage:** Consider power saving implementations.

### Contributing

Contributions are welcome! Please feel free to submit pull requests or open issues.

### About Akita Engineering

This project is developed by Akita Engineering, a group dedicated to open-source hardware and software projects. Visit our website at [Akita Engineering Website - if you have one] for more information about our projects.
