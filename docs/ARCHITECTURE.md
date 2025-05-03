# ESP32 Reticulum Gateway Architecture

This document provides a high-level overview of the software architecture for the ESP32 Reticulum Gateway project. The code is organized into several C++ classes to promote modularity and maintainability.

## Core Components

1.  **`ReticulumNode` (`ReticulumNode.h`/`.cpp`)**
    * **Purpose:** The main application class that orchestrates all gateway functions. It acts as the central hub connecting other components.
    * **Responsibilities:**
        * Initializes all subsystems during `setup()`.
        * Manages the main application `loop()`.
        * Owns instances of `InterfaceManager`, `RoutingTable`, and `LinkManager`.
        * Loads and saves node configuration (address, packet ID) from/to EEPROM.
        * Handles periodic tasks like sending Announce packets and checking memory usage.
        * Receives decoded packets from `InterfaceManager` via a callback (`handleReceivedPacket`).
        * Dispatches packets based on type/context to `LinkManager` (for Link packets), `RoutingTable` (for Announces), or handles them locally (`processPacketForSelf`).
        * Implements forwarding logic for non-link/non-announce packets (`forwardPacket`).
        * Manages application-level data handling via the `AppDataHandler` callback.
        * Handles the `RNS_CONTEXT_LOCAL_CMD` for initiating reliable sends.

2.  **`InterfaceManager` (`InterfaceManager.h`/`.cpp`)**
    * **Purpose:** Manages all physical and logical network interfaces.
    * **Responsibilities:**
        * Initializes WiFi (STA mode), UDP listener, ESP-NOW, Bluetooth Serial, and USB Serial.
        * Owns `KISSProcessor` instances for Serial and Bluetooth.
        * Processes incoming bytes from Serial/Bluetooth via `KISSProcessor`.
        * Processes incoming UDP datagrams.
        * Handles ESP-NOW receive callbacks.
        * Calls the `PacketReceiverCallback` (pointing to `ReticulumNode::handleReceivedPacket`) when a complete packet is received from any interface, providing packet data and source information (MAC/IP).
        * Provides methods (`sendPacket`, `sendPacketVia`, `broadcastAnnounce`) for sending packets out the appropriate interfaces, using the `RoutingTable` for lookups or broadcasting as needed.
        * Manages ESP-NOW peer registration and removal, potentially coordinated with `RoutingTable` pruning.

3.  **`RoutingTable` (`RoutingTable.h`/`.cpp`)**
    * **Purpose:** Stores routing information learned from Announce packets.
    * **Responsibilities:**
        * Maintains a list (`std::list<RouteEntry>`) of known destinations, their next hop (MAC or IP), the interface learned from, hop count, and last heard time.
        * Updates the table when valid Announce packets are received (`update`).
        * Provides a lookup method (`findRoute`) used by `InterfaceManager` to determine the next hop for outgoing packets.
        * Periodically removes stale routes (`prune`).
        * Manages a map of recently forwarded Announce packet IDs/sources (`_recentAnnounces`) to prevent forwarding loops (`shouldForwardAnnounce`, `markAnnounceForwarded`, `pruneRecentAnnounces`).

4.  **`LinkManager` (`LinkManager.h`/`.cpp`)**
    * **Purpose:** Manages all active reliable Link instances.
    * **Responsibilities:**
        * Maintains a map (`std::map`) of active `Link` objects, keyed by destination address.
        * Creates new `Link` instances when reliable communication is initiated or a LINK_REQ is received (`getOrCreateLink`).
        * Receives Link-related packets (`LINK_REQ`, `LINK_DATA`, `ACK`, `LINK_CLOSE`) from `ReticulumNode` and dispatches them to the correct `Link` instance (`processPacket`).
        * Provides the interface for the application layer (`ReticulumNode`) to initiate reliable sends (`sendReliableData`).
        * Periodically checks timeouts for all active links (`checkAllTimeouts`) and prunes inactive/closed links (`pruneInactiveLinks`, `removeLink`).
        * Provides necessary node context (node address, packet ID generator, sending function) to individual `Link` instances.
        * Passes successfully received application data up to `ReticulumNode` (`processReceivedLinkData`).

5.  **`Link` (`Link.h`/`.cpp`)**
    * **Purpose:** Represents and manages the state of a single reliable point-to-point connection.
    * **Responsibilities:**
        * Implements the Link state machine (`LinkState`: CLOSED, PENDING_REQ, ESTABLISHED, CLOSING).
        * Handles sending specific Link control packets (`LINK_REQ`, `LINK_CLOSE`, `ACK`) via `LinkManager`.
        * Processes incoming packets relevant to this specific link (`handlePacket`).
        * Manages outgoing (`_outgoingSequence`) and expected incoming (`_expectedIncomingSequence`) sequence numbers.
        * Queues outgoing data packets (`_pendingOutgoingPackets` - currently window size 1).
        * Manages ACK timeouts (`_stateTimer`) and retransmissions (`checkTimeouts`, `retransmitOldestPending`).
        * Handles link teardown on errors or timeouts (`teardown`).

6.  **`KISSProcessor` (`KISS.h`/`.cpp`)**
    * **Purpose:** Handles KISS framing and de-framing for serial interfaces.
    * **Responsibilities:**
        * Decodes incoming byte streams, handling escape sequences and frame boundaries (`decodeByte`).
        * Calls a `PacketHandler` callback when a complete, valid frame is received.
        * Provides a static method (`encode`) to frame raw packet data for sending.

7.  **`ReticulumPacket` (`ReticulumPacket.h`/`.cpp`)**
    * **Purpose:** Defines the `RnsPacketInfo` structure and provides functions for serializing/deserializing Reticulum packets according to the implemented specification.
    * **Responsibilities:**
        * `RnsPacketInfo` struct holds deserialized header fields and payloads. Includes logic (`processPayloadForLink`) to handle Link sequence numbers embedded in the payload.
        * `deserialize` function parses raw bytes into `RnsPacketInfo`.
        * `serialize` function constructs raw bytes from packet info, embedding sequence numbers where needed.
        * `serialize_control` helper simplifies creating ACK/REQ/CLOSE packets.

8.  **`Utils` (`Utils.h`/`.cpp`)**
    * **Purpose:** Contains general helper functions.
    * **Responsibilities:**
        * `printBytes`: Prints byte arrays in HEX format.
        * `compareAddresses`: Safely compares RNS addresses.

9.  **`Config.h`**
    * **Purpose:** Central location for all user-configurable parameters and system constants (timeouts, ports, sizes, pins, contexts, etc.).

## Data Flow (Simplified)

* **Incoming Packet:** Interface Hardware -> `InterfaceManager` (e.g., `processWiFiInput`) -> `PacketReceiverCallback` (`ReticulumNode::handleReceivedPacket`) -> Dispatch based on type/context (`LinkManager::processPacket` or `RoutingTable::update` or `ReticulumNode::processPacketForSelf`) -> Application Layer (`ReticulumNode::processAppData`) OR Forwarding (`ReticulumNode::forwardPacket`/`forwardAnnounce` -> `InterfaceManager::sendPacket`/`broadcastAnnounce` -> Interface Hardware).
* **Outgoing Packet (Unreliable):** Application Logic -> `InterfaceManager::sendPacket` -> `RoutingTable::findRoute` -> `InterfaceManager::sendPacketVia[Interface]` -> Interface Hardware.
* **Outgoing Packet (Reliable):** Application Logic (e.g., via KISS Command) -> `ReticulumNode::processPacketForSelf` -> `LinkManager::sendReliableData` -> `LinkManager::getOrCreateLink` -> `Link::sendData` -> `Link::sendPacketInternal` -> `LinkManager::sendPacketRaw` -> `InterfaceManager::sendPacket` -> ... -> Interface Hardware.

This modular architecture aims to separate concerns, making the code easier to understand, debug, and extend.
