# ESP32 Reticulum Gateway Packet Formats

This document details how Reticulum packet headers and payloads are used within this specific implementation, particularly concerning the Link layer. It assumes familiarity with the general Reticulum Network Stack technical documentation.

## Reticulum Header Fields Used

The implementation utilizes the following standard RNS header fields (based on a minimum header size of 24 bytes assuming Type 1 source/destination addresses):

* **Byte 0: Header Type & Flags**
    * `RNS_HEADER_TYPE_MASK` (Bits 0-2): Identifies main packet type:
        * `RNS_HEADER_TYPE_DATA` (0x01): General data, Link requests, Link data, Link close.
        * `RNS_HEADER_TYPE_ACK` (0x02): Link acknowledgments.
        * `RNS_HEADER_TYPE_ANN` (0x03): Announce packets.
    * `RNS_HEADER_FLAG_REQUEST_ACK_MASK` (Bit 7): Set on packets requiring a Link layer ACK (primarily Link Data).
    * Other flags (Bits 3-6) are currently unused.
* **Byte 1: Context**
    * Used extensively to differentiate packet functions beyond the basic Header Type. See Context Types below.
* **Bytes 2-3: Packet ID**
    * 16-bit unique identifier for *this specific transmission attempt*. Network Byte Order (Big Endian). Generated sequentially by the sending node. Used for potential future de-duplication or request/reply matching (though Links primarily use sequence numbers). Link Request ACKs conceptually acknowledge the Packet ID of the REQ.
* **Byte 4: Hops**
    * Number of hops traversed. Incremented during forwarding. Max value defined in `Config.h` (`MAX_HOPS`).
* **Byte 5: Destination Type**
    * `RNS_DST_TYPE_SINGLE` (0x01): Addressed to one node.
    * `RNS_DST_TYPE_GROUP` (0x02): Addressed to a group. Used for Announces (implicitly broadcast) and application-level group messages.
* **Byte 6: Destination Length**
    * Always `RNS_ADDRESS_SIZE` (8) as only Type 1/2 addresses are handled.
* **Bytes 7-14: Destination Address**
    * 8-byte RNS address of the destination node or group.
* **Byte 15: Source Type**
    * Always `RNS_DST_TYPE_SINGLE` (0x01).
* **Byte 16: Source Length**
    * Always `RNS_ADDRESS_SIZE` (8).
* **Bytes 17-24: Source Address**
    * 8-byte RNS address of the originating node.

## Context Types (`Context` Field - Byte 1)

The `Context` field is crucial for differentiating packet functions, especially for the Link Layer:

* `RNS_CONTEXT_NONE` (0x00): Default context for standard application data (if not using Links) or Announce packets.
* `RNS_CONTEXT_LINK_REQ` (0xA1): Sent to initiate a reliable Link. (Header Type: DATA)
* `RNS_CONTEXT_LINK_CLOSE` (0xA2): Sent to gracefully close an established Link. (Header Type: DATA)
* `RNS_CONTEXT_LINK_DATA` (0xA3): Contains application data sent over an established Link. Requires sequence number in payload. (Header Type: DATA, usually with REQ_ACK flag)
* `RNS_CONTEXT_ACK` (0xA4): Context used *within* an ACK packet (Header Type: ACK) to indicate it's a Link layer acknowledgment. Requires acknowledged sequence number in payload.
* `RNS_CONTEXT_LOCAL_CMD` (0xFE): **Not a standard RNS context.** Used locally for packets received via KISS interface to trigger gateway actions, specifically `LinkManager::sendReliableData`.

## Link Layer Payload Structure

For packets related to the implemented Link layer, the payload structure is modified:

* **Link Data (`RNS_CONTEXT_LINK_DATA`)**
    * **Bytes 0-1:** 16-bit Sequence Number (Network Byte Order - Big Endian). Incremented for each data packet sent on the link.
    * **Bytes 2+:** Actual application data payload.
* **Link ACK (`RNS_HEADER_TYPE_ACK`, `RNS_CONTEXT_ACK`)**
    * **Bytes 0-1:** 16-bit Sequence Number (Network Byte Order - Big Endian). This contains the sequence number of the DATA packet *being acknowledged*.
    * **Bytes 2+:** Typically empty for this simple ACK implementation.
* **Link Request (`RNS_CONTEXT_LINK_REQ`)**
    * Payload is typically empty. The ACK response conceptually acknowledges the `packet_id` of the REQ, using sequence number 0 in the ACK payload.
* **Link Close (`RNS_CONTEXT_LINK_CLOSE`)**
    * Payload is typically empty. The ACK response uses sequence number 0 in its payload.

## Local Command Payload Structure (`RNS_CONTEXT_LOCAL_CMD`)

Packets sent *to* the gateway *via KISS* using this context have a specific payload format to initiate reliable sending:

* **Bytes 0-7:** The 8-byte RNS address of the **final destination** for the reliable data.
* **Bytes 8+:** The actual application data payload to be sent reliably.
