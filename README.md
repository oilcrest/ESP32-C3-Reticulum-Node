# ESP32-C3 Reticulum Gateway

**Developed by Akita Engineering**


## Overview

This project provides firmware to turn an ESP32-C3 (or compatible ESP32 variant) into a multi-interface Reticulum Network Stack gateway node. It allows devices communicating via different physical layers (WiFi UDP, ESP-NOW, Serial, Bluetooth) to exchange Reticulum packets seamlessly.

The gateway implements core Reticulum functionalities including path discovery via announces, basic routing, and a simplified reliable transport mechanism using an ACK/Link layer. Communication over Serial and Bluetooth interfaces uses standard KISS framing for compatibility with tools like `rnsbridge`.

This project is built upon a refactored C++ codebase using PlatformIO (recommended) or the Arduino IDE.

## Features

* **Reticulum Gateway:** Acts as a transparent gateway bridging multiple interfaces.
* **Multiple Interfaces:**
    * **WiFi (Station Mode):** Connects to an existing WiFi network.
    * **WiFi (UDP Interface):** Listens and sends on UDP port 4242 (broadcast and unicast based on routing).
    * **ESP-NOW:** Direct ESP32-to-ESP32 communication.
    * **Serial (USB/UART):** Communicates using KISS framing.
    * **Bluetooth Classic (Serial Profile):** Communicates using KISS framing.
* **Persistent Configuration:** Stores the unique Reticulum node address in EEPROM.
* **Auto Address Generation:** Creates a random RNS address on first boot if none is found in EEPROM.
* **KISS Framing:** Standardized and robust packet framing for serial links.
* **Path Discovery & Routing:** Uses Reticulum Announce packets to discover neighbors and build a basic routing table. Forwards packets based on learned routes or broadcasts if the destination is unknown.
* **Announce Re-broadcasting:** Forwards valid Announce packets received from peers (with hop limit and loop prevention) to extend network reach.
* **ACK/Link Layer (Basic):** Implements a simplified reliable delivery mechanism (ACKs, retransmissions, timeouts) for point-to-point links established on demand. (Note: Uses a window size of 1).
* **Group Address Support:** Processes packets addressed to subscribed group addresses locally *and* forwards them for other group members.
* **Refactored Code:** Organized into C++ classes (`ReticulumNode`, `InterfaceManager`, `RoutingTable`, `LinkManager`, etc.) for better maintainability and extensibility.
* **Local Command Interface:** Allows initiating reliable (Link layer) transmissions via specially formatted packets sent over KISS interfaces.

## Hardware Requirements

* An ESP32-C3 based development board (e.g., ESP32-C3-DevKitM-1, Seeed Studio XIAO ESP32C3).
* Other ESP32 variants (ESP32, ESP32-S2, ESP32-S3) might work with minor code adjustments (pin definitions, library compatibility), but testing is required.

## Software Requirements & Dependencies

* **PlatformIO IDE:** Recommended for easy project management and dependency handling. VSCode with the PlatformIO extension is a good choice.
* **Arduino IDE:** Possible, but managing the multiple `.h`/`.cpp` files and potential library dependencies might be more complex.
* **Espressif ESP32 Board Support:** Required for both PlatformIO (`espressif32` platform) and Arduino IDE.
* **C++ Standard Library:** Uses features like `<vector>`, `<list>`, `<map>`, `<array>`, `<functional>`, `<memory>`, which are typically included with the ESP32 core for Arduino/PlatformIO.

## Installation & Setup

1.  **Clone:** Clone this repository to your local machine:
    ```bash
    git clone [https://github.com/AkitaEngineering/ESP32-C3-Reticulum-Node]
    cd esp32-c3-reticulum-gateway
    ```
2.  **Configure:**
    * Open the `include/Config.h` file.
    * Modify the `WIFI_SSID` and `WIFI_PASSWORD` constants with your WiFi network credentials.
    * **(Optional)** Modify `SUBSCRIBED_GROUPS` to add any Reticulum group addresses this node should listen to.
    * **(Optional)** Advanced users can adjust timing parameters (`ANNOUNCE_INTERVAL_MS`, `ROUTE_TIMEOUT_MS`, `LINK_*` timeouts), but the defaults are generally reasonable starting points.
3.  **Build & Upload:**
    * **Using PlatformIO (Recommended):**
        * Open the project folder in VSCode with the PlatformIO extension installed.
        * Select the correct environment for your board in the PlatformIO status bar (e.g., `esp32-c3-devkitm-1`).
        * Click the "Upload" button (right-pointing arrow) in the PlatformIO status bar or run `pio run -t upload` in the terminal.
    * **Using Arduino IDE:**
        * You may need to restructure the code slightly (e.g., move all code into the main `.ino` file or use Arduino's sketch tabs correctly).
        * Ensure you have the ESP32 board support installed (`Tools > Board > Boards Manager...`).
        * Select your specific ESP32-C3 board and the correct COM port under the `Tools` menu.
        * Verify/Compile the sketch (check mark button).
        * Upload the sketch (right arrow button).

## Usage

### Initial Boot
On the first boot, the gateway will generate a unique 8-byte Reticulum address and save it to EEPROM. It will print this address to the Serial Monitor.

### Interfaces

* **Serial Monitor:** Connect via USB at **115200 baud**. Provides status messages, debug output, received packet information, and routing table prints.
* **Serial/Bluetooth (KISS):**
    * These interfaces require a client application on the connected computer (or other device) that speaks **KISS framing** and sends/receives **raw Reticulum packets**.
    * Standard tools like `rnsbridge` from the official Python Reticulum package can be used.
    * Example `rnsbridge` config snippet:
        ```
        [My ESP32 Gateway]
        type = KISSInterface
        device = /dev/ttyUSB0 # Or your ESP32's serial port / Bluetooth serial port
        speed = 115200
        # mtu = <Adjust based on RNS_MAX_PAYLOAD if needed>
        ```
* **WiFi (UDP):**
    * The gateway listens on UDP port `4242`.
    * It sends Announce packets via UDP broadcast on the local network.
    * It can send packets via UDP unicast if a route to the destination IP is learned via Announce packets received over UDP.
    * Other Reticulum nodes on the same WiFi network using a UDP interface can communicate with the gateway.
* **ESP-NOW:**
    * Automatically communicates with other ESP32 devices running compatible Reticulum firmware on the same WiFi channel.
    * Uses broadcast for Announce packets and attempts direct MAC-to-MAC communication if a route is learned.

### Initiating Reliable (Link) Transmissions

To send data reliably using the ACK/Link layer *from* a computer connected via Serial/Bluetooth:

1.  **Construct a command packet:**
    * **Destination:** The gateway's own RNS address (printed on boot).
    * **Source:** Your computer's RNS address (or any valid address).
    * **Context:** `RNS_CONTEXT_LOCAL_CMD` (defined as `0xFE` in `Config.h`).
    * **Header Type:** `RNS_HEADER_TYPE_DATA`.
    * **Hops:** 0.
    * **Payload:**
        * Bytes 0-7: The **final** 8-byte RNS destination address you want to send reliable data *to*.
        * Bytes 8+: The actual data payload you want to send reliably.
2.  **Serialize:** Create the raw byte representation of this Reticulum packet.
3.  **Encode:** Apply KISS framing to the serialized packet bytes.
4.  **Send:** Transmit the KISS-encoded bytes over the Serial/Bluetooth connection to the gateway.

The gateway will receive this, parse the command, establish a Link (if needed) to the target destination, and send the actual data payload reliably.

## Project Structure

* `platformio.ini`: PlatformIO project configuration (board, libraries, build flags).
* `include/`: Header files (`.h`) defining classes and constants.
    * `Config.h`: Main user configuration file.
* `src/`: Source files (`.cpp`) implementing the logic.
    * `main.cpp`: Arduino entry point (`setup()` and `loop()`).
* `lib/`: (Optional) Local project libraries if needed.

## Limitations & Future Work

* **Link Layer:** The ACK/Link implementation is basic (window size 1). It lacks features like dynamic timeout adjustment or more advanced flow control.
* **Routing Metrics:** Routing currently uses hop count and last-heard time. RSSI is not used due to Arduino API limitations for ESP-NOW. Link quality metrics are not yet integrated.
* **ESP-NOW Peers:** Basic dynamic peer management is used. May encounter ESP-NOW peer limits (~20) on dense networks.
* **Transport Layer:** Does not implement RNS Transport features like automatic packet segmentation/reassembly for payloads larger than the interface MTU. `RNS_MAX_PAYLOAD` limits the size.
* **Error Handling:** While improved, error handling can be made more robust in various scenarios (e.g., memory allocation failures under load).
* **Testing:** Requires extensive testing in various network topologies with multiple nodes.
* **Memory:** Active links and large routing tables consume RAM. Performance on very busy networks or memory-constrained ESP32 variants needs evaluation.
* **Security:** No encryption or authentication is implemented at the Reticulum level (ESP-NOW encryption can be enabled in `Config.h`/code if needed).

## Contributing

Contributions are welcome! Please feel free to:

* Open issues to report bugs or suggest features.
* Fork the repository and submit pull requests with improvements.
* Adhere to basic coding standards and provide clear commit messages.


### About Akita Engineering

This project is developed by Akita Engineering, a group dedicated to open-source hardware and software projects. Visit our website at akitaengineering.com for more information about our projects.
