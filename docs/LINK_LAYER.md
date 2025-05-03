# ESP32 Reticulum Gateway - Link Layer Implementation Details

This document describes the implementation of the reliable ACK/Link layer in the ESP32 Reticulum Gateway firmware. It provides guaranteed, in-order delivery between two directly communicating nodes but has some simplifications compared to a full Reticulum implementation.

## Overview

The Link layer ensures that data sent between two nodes arrives correctly and in order. It handles:

* **Link Establishment:** Handshake process to activate a link.
* **Sequencing:** Assigning sequence numbers to data packets.
* **Acknowledgments (ACKs):** Confirming receipt of data packets.
* **Timeouts:** Detecting lost packets or ACKs.
* **Retransmissions:** Resending packets that were not acknowledged within the timeout period.
* **Link Closure:** Graceful termination of the link.

This implementation resides primarily within the `Link` and `LinkManager` classes.

## Packet Types and Contexts

The Link layer uses specific Reticulum packet header fields:

* **Header Type:**
    * `RNS_HEADER_TYPE_DATA`: Used for LINK_REQ, LINK_DATA, LINK_CLOSE packets.
    * `RNS_HEADER_TYPE_ACK`: Used for ACK packets.
* **Context:**
    * `RNS_CONTEXT_LINK_REQ` (0xA1): Initiates link setup.
    * `RNS_CONTEXT_LINK_DATA` (0xA3): Carries reliable application data.
    * `RNS_CONTEXT_LINK_CLOSE` (0xA2): Initiates graceful link closure.
    * `RNS_CONTEXT_ACK` (0xA4): Used within ACK packets.
* **Flags:**
    * `RNS_HEADER_FLAG_REQUEST_ACK_MASK`: Set on LINK_DATA packets to indicate an ACK is required.

## Payload Structure

* **LINK_DATA & ACK packets:** The first two bytes of the RNS packet `payload` contain the 16-bit sequence number (Network Byte Order - Big Endian).
    * For DATA, this is the sequence number of the data itself.
    * For ACK, this is the sequence number *being acknowledged*.
* **LINK_REQ & LINK_CLOSE packets:** The payload is typically empty. ACKs for these control packets use sequence number 0 in the ACK payload.

## Link State Machine (`Link::LinkState`)

Each `Link` object maintains a state:

1.  **`CLOSED`:** The initial and final state. No communication possible.
2.  **`PENDING_REQ`:** A `LINK_REQ` has been sent, waiting for an `ACK` (with sequence 0). Responds to concurrent `LINK_REQ` from the peer by sending an ACK and transitioning to `ESTABLISHED`. Times out back to `CLOSED` if no ACK is received (`LINK_REQ_TIMEOUT_MS`).
3.  **`ESTABLISHED`:** Link is active.
    * Can send `LINK_DATA` packets (one at a time, waits for ACK).
    * Processes incoming `LINK_DATA` packets: checks sequence number, passes data to application layer, sends `ACK`. Handles duplicates by re-sending ACK. Ignores out-of-order packets.
    * Processes incoming `ACK` packets: matches sequence number against the pending outgoing packet, removes it from the queue if matched.
    * Handles incoming `LINK_REQ` (peer might have reset) by re-sending ACK (seq 0).
    * Handles incoming `LINK_CLOSE` by sending ACK (seq 0) and transitioning to `CLOSED`.
    * Monitors for outgoing packet ACK timeout (`LINK_RETRY_TIMEOUT_MS`). Retransmits up to `LINK_MAX_RETRIES` times before tearing down (`teardown`).
4.  **`CLOSING`:** A `LINK_CLOSE` has been sent, waiting for an `ACK` (with sequence 0). Times out to `CLOSED` if no ACK received (`LINK_RETRY_TIMEOUT_MS`).

## Timeouts and Retransmissions

* **Link Request:** `LINK_REQ_TIMEOUT_MS`. If no ACK (seq 0) received, the link attempt fails and state returns to `CLOSED`. No retries for the request itself.
* **Data Packet:** `LINK_RETRY_TIMEOUT_MS`. If no ACK for the sent data sequence number received, the packet is retransmitted with a *new Packet ID*. This repeats up to `LINK_MAX_RETRIES` times. If still no ACK, the link is forcibly torn down (`teardown`).
* **Link Close:** `LINK_RETRY_TIMEOUT_MS`. If no ACK (seq 0) received for the `LINK_CLOSE` packet, the link transitions to `CLOSED` locally anyway. No retries for CLOSE.
* **Inactivity:** `LINK_INACTIVITY_TIMEOUT_MS`. If no packets (of any kind related to the link) are sent or received for this duration, the `LinkManager` forcibly tears down the link via `Link::teardown`.

## Simplifications and Limitations

* **Window Size:** Only **one** data packet can be "in flight" (awaiting ACK) at a time per link. This simplifies implementation but limits throughput compared to implementations with sliding windows.
* **Sequence Number Wrap-around:** Basic incrementing is used. While standard sequence number comparison logic handles wrap-around gracefully in theory, extensive testing under wrap-around conditions hasn't been performed.
* **Timeout Calculation:** Uses fixed timeouts defined in `Config.h`. Does not implement dynamic RTT (Round Trip Time) estimation for adjusting timeouts.
* **No RTT Probes:** Does not actively probe link RTT.
* **No Explicit Keep-Alives:** Link state is maintained by data/ACK traffic or eventually times out via `LINK_INACTIVITY_TIMEOUT_MS`. No dedicated keep-alive packets are sent.
* **Out-of-Order Packets:** Strictly ignores out-of-order data packets rather than buffering them.
* **Error Reporting:** Limited error reporting back to the initiating application (e.g., if `sendReliableData` fails, it just returns `false`).

This implementation provides basic reliable delivery suitable for many microcontroller use cases but lacks the performance optimizations and robustness features of a full Reticulum Link implementation.
