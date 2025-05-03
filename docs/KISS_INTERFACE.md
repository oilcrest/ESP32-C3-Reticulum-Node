# ESP32 Reticulum Gateway - KISS Interface Guide

This document explains how to interact with the ESP32 Reticulum Gateway using its Serial (USB/UART) and Bluetooth Classic Serial Profile interfaces, both of which utilize the KISS (Keep It Simple, Stupid) framing protocol.

## KISS Framing Overview

KISS is a simple protocol used to send datagrams (like Reticulum packets) over serial links. It handles escaping special characters to ensure packet boundaries are correctly identified.

* **`FEND` (Frame End - `0xC0`):** Marks the beginning and end of a packet frame. Multiple `FEND` bytes between packets are ignored.
* **`FESC` (Frame Escape - `0xDB`):** Indicates that the *next* byte has a special meaning.
* **`TFEND` (Transposed FEND - `0xDC`):** Represents a literal `0xC0` byte within the packet data. Sent as `FESC, TFEND`.
* **`TFESC` (Transposed FESC - `0xDD`):** Represents a literal `0xDB` byte within the packet data. Sent as `FESC, TFESC`.

**Sending:** To send a raw Reticulum packet *to* the gateway via KISS:
1.  Escape any `FEND` (`0xC0`) bytes in your packet data by replacing them with `FESC, TFEND` (`0xDB, 0xDC`).
2.  Escape any `FESC` (`0xDB`) bytes in your packet data by replacing them with `FESC, TFESC` (`0xDB, 0xDD`).
3.  Prepend and append an `FEND` (`0xC0`) byte to the resulting escaped data.
4.  Transmit these bytes over the serial connection (USB or Bluetooth).

**Receiving:** To receive packets *from* the gateway via KISS:
1.  Read bytes from the serial connection.
2.  Discard bytes until the first `FEND` is found (start of frame).
3.  Buffer subsequent bytes.
4.  If `FESC` is received, the *next* byte determines the actual data byte: `TFEND` becomes `FEND`, and `TFESC` becomes `FESC`. Any other byte following `FESC` is a protocol error.
5.  If `FEND` is received, the buffered data (with escapes processed) constitutes one complete raw Reticulum packet. Start looking for the next packet.

## Required Client Software

You need software on the connected computer or device that can handle KISS framing and understand raw Reticulum packets.

* **`rnsbridge`:** A utility included with the official Python [Reticulum](https://github.com/markqvist/reticulum) package. It can create a virtual network interface on your computer linked to a KISS serial port.
    * Example `rnsbridge` configuration:
        ```ini
        [MyESP32Gateway]
        type = KISSInterface
        # Linux serial port example (adjust as needed for MacOS/Windows)
        device = /dev/ttyUSB0
        # Or Bluetooth serial port (e.g., /dev/rfcomm0 after pairing)
        # device = /dev/rfcomm0
        speed = 115200
        # mtu = 250 # Optional: Match RNS_MAX_PAYLOAD + Header if needed
        ```
* **Custom Scripts:** You can write scripts (e.g., in Python using the `kiss` library) to directly send/receive KISS-framed Reticulum packets.

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
