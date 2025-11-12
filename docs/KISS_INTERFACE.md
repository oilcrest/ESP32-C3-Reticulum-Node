# ESP32 Reticulum Gateway - KISS Interface Guide

This document explains how to interact with the ESP32 Reticulum Gateway using its Serial (USB/UART) and Bluetooth Classic Serial Profile interfaces, both of which utilize the KISS (Keep It Simple, Stupid) framing protocol.

## KISS Framing Overview

KISS is a simple protocol used to send datagrams (like Reticulum packets) over serial links. It handles escaping special characters to ensure packet boundaries are correctly identified.

### KISS Special Characters

* **`FEND` (Frame End - `0xC0`):** Marks the beginning and end of a packet frame. Multiple `FEND` bytes between packets are ignored.
* **`FESC` (Frame Escape - `0xDB`):** Indicates that the *next* byte has a special meaning.
* **`TFEND` (Transposed FEND - `0xDC`):** Represents a literal `0xC0` byte within the packet data. Sent as `FESC, TFEND`.
* **`TFESC` (Transposed FESC - `0xDD`):** Represents a literal `0xDB` byte within the packet data. Sent as `FESC, TFESC`.

### KISS Frame Structure

A complete KISS frame has the following structure:

```
[FEND] [Command] [Escaped Data...] [FEND]
```

* **FEND (0xC0):** Frame delimiter (start and end)
* **Command byte:** Indicates the frame type (see below)
* **Escaped Data:** The actual packet data with special characters escaped
* **FEND (0xC0):** Frame delimiter (end)

### KISS Command Bytes

The command byte immediately follows the opening FEND and indicates what type of frame it is:

* **`0x00`:** Data frame (normal packet data) - **This is what we use**
* **`0x01`-`0x0F`:** TNC configuration commands (not used by this gateway)

**Example KISS frame for a data packet:**
```
C0 00 [escaped packet bytes] C0
└─┘ └─┘ └──────────────────┘ └─┘
FEND CMD  Packet data        FEND
```

### Sending Packets

To send a raw Reticulum packet *to* the gateway via KISS:

1.  Escape any `FEND` (`0xC0`) bytes in your packet data by replacing them with `FESC, TFEND` (`0xDB, 0xDC`).
2.  Escape any `FESC` (`0xDB`) bytes in your packet data by replacing them with `FESC, TFESC` (`0xDB, 0xDD`).
3.  Build the frame: `FEND` + `0x00` (command byte) + escaped data + `FEND`.
4.  Transmit these bytes over the serial connection (USB or Bluetooth).

### Receiving Packets

To receive packets *from* the gateway via KISS:

1.  Read bytes from the serial connection.
2.  Discard bytes until the first `FEND` is found (start of frame).
3.  Read the next byte (command byte) - for data frames it should be `0x00`.
4.  Buffer subsequent bytes.
5.  If `FESC` is received, the *next* byte determines the actual data byte: `TFEND` becomes `FEND`, and `TFESC` becomes `FESC`. Any other byte following `FESC` is a protocol error.
6.  If `FEND` is received, the buffered data (with escapes processed) constitutes one complete raw Reticulum packet. Start looking for the next packet.

## Setting Up Your Reticulum Gateway

### Hardware Requirements

**Important:** You need a USB-to-serial adapter connected to the ESP32's Serial2 pins:
* **TX (Pin 16)** on ESP32 → RX on adapter
* **RX (Pin 17)** on ESP32 → TX on adapter
* Connect GND between ESP32 and adapter

This serial connection will be used for the KISS interface communication between your PC and the ESP32 gateway.

### Software Setup

1. **Configure your Reticulum instance** on your PC by adding a KISS interface to your Reticulum configuration file (typically `~/.reticulum/config`):

   ```ini
   [[Default Interface]]
   type = KISSInterface
   enabled = yes
   port = /dev/ttyUSB1
   speed = 115200
   ```

   **Note:** Change `/dev/ttyUSB1` to match the actual port where your USB-to-serial adapter is connected (e.g., `/dev/ttyUSB0`, `/dev/ttyACM0` on Linux, `COM3` on Windows, or `/dev/cu.usbserial-*` on macOS).

2. **Generate the destination hash** by running the provided Python script:
   ```bash
   python3 tests/read_from_reticulum.py
   ```
   This script will:
   - Initialize a Reticulum instance
   - Create a PLAIN destination with path `["esp32", "node"]`
   - Display the destination hash in multiple formats (hex, C array, bytes)
   - Listen for incoming messages from the Reticulum network

3. **Copy the destination hash** from the script output and update your ESP32 code with it.

4. **Compile and flash** your ESP32 binary with the correct destination hash.

5. **Connect your serial adapter** (Serial2) to your PC, ensuring it's connected to the port you specified in the Reticulum config.

6. **Run the listener script** and watch the magic happen - your ESP32 will now communicate with the Reticulum network!

### Additional Python Scripts

The [tests](../tests/) folder includes several utility scripts:
* **`read_from_reticulum.py`:** Listens for messages and displays the destination hash (primary setup script)
* **`hello_to_reticulum.py`:** Sends periodic test messages to the ESP32 node
* **`raw_serial_sniffer_kiss_decoder.py`:** Debug tool for inspecting KISS frames on the serial port
* **Custom Scripts:** You can write your own scripts (e.g., in Python using the `kiss` library) to directly send/receive KISS-framed Reticulum packets.

## Sending Unreliable Data via KISS

To send a standard, unreliable Reticulum packet *through* the gateway:

1.  Construct the raw Reticulum packet bytes with the desired final destination, source, context, payload, etc.
2.  Encode it using KISS.
3.  Send it to the gateway via Serial/Bluetooth.

The gateway will receive, decode KISS, deserialize the packet, look up the destination in its routing table, and forward it via the appropriate interface (ESP-NOW, WiFi UDP, or another Serial/BT interface if configured differently).

## Initiating Reliable (Link) Transmissions via KISS

To tell the gateway to send data *reliably* using its Link layer *to a final destination*:

1.  **Construct a special "Local Command" Reticulum packet:**
    * **Destination Address:** The gateway's *own* RNS address.
    * **Source Address:** Your RNS address (or any valid source).
    * **Context:** `RNS_CONTEXT_LOCAL_CMD` (defined as `0xFE` in `Config.h`).
    * **Header Type:** `RNS_HEADER_TYPE_DATA`.
    * **Hops:** `0`.
    * **Payload:** This is specially formatted:
        * **Bytes 0-7:** The 8-byte RNS address of the **final destination** node for the reliable data.
        * **Bytes 8+:** The actual application data payload you want to send reliably.
2.  **Serialize:** Get the raw bytes of this command packet.
3.  **Encode:** Apply KISS framing.
4.  **Send:** Transmit the KISS-encoded bytes to the gateway.

The gateway's `ReticulumNode::processPacketForSelf` method will detect the `RNS_CONTEXT_LOCAL_CMD`, parse the target destination and payload, and then call `LinkManager::sendReliableData`. The Link Manager will establish/use a Link to the target destination and send the actual application data reliably. Status messages regarding the Link setup or data transmission will appear on the gateway's Serial Monitor.

**Note:** You will *not* directly receive Link layer ACKs back on the KISS interface using this method. The gateway handles the reliability aspect itself. You would need application-level acknowledgments if end-to-end confirmation back to the originating KISS client is required. Data received *by* the gateway *via* a Link *for* an application handler will be printed to the Serial monitor by default (see `myAppDataReceiver` in `main.cpp`).
