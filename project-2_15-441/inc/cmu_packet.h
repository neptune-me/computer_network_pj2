/**
 * Copyright (C) 2022 Carnegie Mellon University
 *
 * This file is part of the TCP in the Wild course project developed for the
 * Computer Networks course (15-441/641) taught at Carnegie Mellon University.
 *
 * No part of the project may be copied and/or distributed without the express
 * permission of the 15-441/641 course staff.
 *
 * Defines a CMU-TCP packet and define helper functions to create and manipulate
 * packets.
 *
 * Do NOT modify this file.
 */

#ifndef PROJECT_2_15_441_INC_CMU_PACKET_H_
#define PROJECT_2_15_441_INC_CMU_PACKET_H_

#include <stdint.h>

typedef struct {
  uint32_t identifier;         // Identifier for the CMU-TCP protocol.
  uint16_t source_port;        // Source port.
  uint16_t destination_port;   // Destination port.
  uint32_t seq_num;            // Sequence number.
  uint32_t ack_num;            // Acknowledgement number.
  uint16_t hlen;               // Header length.
  uint16_t plen;               // Packet length.
  uint8_t flags;               // Flags.
  uint16_t advertised_window;  // Advertised window.
  uint16_t extension_length;   // Extension length.
  uint8_t extension_data[];    // Extension data.
} __attribute__((__packed__)) cmu_tcp_header_t;

#define SYN_FLAG_MASK 0x8
#define ACK_FLAG_MASK 0x4
#define FIN_FLAG_MASK 0x2
#define IDENTIFIER 15441

// Maximum Segment Size. Make sure to update this if your CCA requires extension
// data for all packets, as this reduces the payload and thus the MSS.
#define MSS (MAX_LEN - sizeof(cmu_tcp_header_t))

/* Helper functions to get/set fields in the header */

uint16_t get_src(cmu_tcp_header_t* header);
uint16_t get_dst(cmu_tcp_header_t* header);
uint32_t get_seq(cmu_tcp_header_t* header);
uint32_t get_ack(cmu_tcp_header_t* header);
uint16_t get_hlen(cmu_tcp_header_t* header);
uint16_t get_plen(cmu_tcp_header_t* header);
uint8_t get_flags(cmu_tcp_header_t* header);
uint16_t get_advertised_window(cmu_tcp_header_t* header);
uint16_t get_extension_length(cmu_tcp_header_t* header);
uint8_t* get_extension_data(cmu_tcp_header_t* header);

void set_src(cmu_tcp_header_t* header, uint16_t src);
void set_dst(cmu_tcp_header_t* header, uint16_t dst);
void set_seq(cmu_tcp_header_t* header, uint32_t seq);
void set_ack(cmu_tcp_header_t* header, uint32_t ack);
void set_hlen(cmu_tcp_header_t* header, uint16_t hlen);
void set_plen(cmu_tcp_header_t* header, uint16_t plen);
void set_flags(cmu_tcp_header_t* header, uint8_t flags);
void set_advertised_window(cmu_tcp_header_t* header,
                           uint16_t advertised_window);
void set_extension_length(cmu_tcp_header_t* header, uint16_t extension_length);
void set_extension_data(cmu_tcp_header_t* header, uint8_t* extension_data);

/**
 * Sets all the header fields.
 *
 * Review TCP headers for more information about what each field means.
 *
 * @param header The header to set the fields.
 * @param src Source port.
 * @param dst Destination port.
 * @param seq Sequence number.
 * @param ack Acknowledgement number.
 * @param hlen Header length.
 * @param plen Packet length.
 * @param flags Packet flags.
 * @param advertised_window Advertised window.
 * @param extension_length Header extension length.
 * @param extension_data Header extension data.
 */
void set_header(cmu_tcp_header_t* header, uint16_t src, uint16_t dst,
                uint32_t seq, uint32_t ack, uint16_t hlen, uint16_t plen,
                uint8_t flags, uint16_t adv_window, uint16_t ext,
                uint8_t* ext_data);

/**
 * Gets a pointer to the packet payload.
 *
 * @param pkt The packet to get the payload.
 *
 * @return A pointer to the payload.
 */
uint8_t* get_payload(uint8_t* pkt);

/**
 * Gets the length of the packet payload.
 *
 * @param pkt The packet to get the payload length.
 *
 * @return The length of the payload.
 */
uint16_t get_payload_len(uint8_t* pkt);

/**
 * Sets the packet payload.
 *
 * @param pkt The packet to set the payload.
 * @param payload A pointer to the payload to be set.
 * @param payload_len The length of the payload.
 */
void set_payload(uint8_t* pkt, uint8_t* payload, uint16_t payload_len);

/**
 * Allocates and initializes a packet.
 *
 * @param src The source port.
 * @param dst The destination port.
 * @param seq The sequence number.
 * @param ack The acknowledgement number.
 * @param hlen The header length.
 * @param plen The packet length.
 * @param flags The flags.
 * @param adv_window The advertised window.
 * @param ext_len The header extension length.
 * @param ext_data The header extension data.
 * @param payload The payload.
 * @param payload_len The length of the payload.
 *
 * @return A pointer to the newly allocated packet. User must `free` after use.
 */
uint8_t* create_packet(uint16_t src, uint16_t dst, uint32_t seq, uint32_t ack,
                       uint16_t hlen, uint16_t plen, uint8_t flags,
                       uint16_t adv_window, uint16_t ext_len, uint8_t* ext_data,
                       uint8_t* payload, uint16_t payload_len);

/**
 * Checks if a given sequence number comes before another sequence number.
 *
 * @param seq1 the first sequence number.
 * @param seq2 the second sequence number.
 * @return 1 if seq1 comes before seq2, 0 otherwise.
 */
static inline int before(uint32_t seq1, uint32_t seq2) {
  return (int32_t)(seq1 - seq2) < 0;
}

/**
 * Checks if a given sequence number comes after another sequence number.
 *
 * @param seq1 the first sequence number.
 * @param seq2 the second sequence number.
 * @return 1 if seq1 comes after seq2, 0 otherwise.
 */
static inline int after(uint32_t seq1, uint32_t seq2) {
  return before(seq2, seq1);
}

/**
 * Checks if a given sequence number is between two others.
 *
 * @param seq the sequence number.
 * @param low the lower bound.
 * @param high the upper bound.
 * @return 1 if low <= seq <= high, 0 otherwise.
 */
static inline int between(uint32_t seq, uint32_t low, uint32_t high) {
  return high - low >= seq - low;
}

#endif  // PROJECT_2_15_441_INC_CMU_PACKET_H_
