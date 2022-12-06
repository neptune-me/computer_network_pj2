/**
 * Copyright (C) 2022 Carnegie Mellon University
 *
 * This file is part of the TCP in the Wild course project developed for the
 * Computer Networks course (15-441/641) taught at Carnegie Mellon University.
 *
 * No part of the project may be copied and/or distributed without the express
 * permission of the 15-441/641 course staff.
 *
 *
 * This file implements the CMU-TCP backend. The backend runs in a different
 * thread and handles all the socket operations separately from the application.
 *
 * This is where most of your code should go. Feel free to modify any function
 * in this file.
 */

#include "backend.h"

#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "cmu_packet.h"
#include "cmu_tcp.h"

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define FALSE 0
#define TRUE 1

/**
 * Tells if a given sequence number has been acknowledged by the socket.
 *
 * @param sock The socket to check for acknowledgements.
 * @param seq Sequence number to check.
 *
 * @return 1 if the sequence number has been acknowledged, 0 otherwise.
 */
int has_been_acked(cmu_socket_t *sock, uint32_t seq) {
  int result;
  while (pthread_mutex_lock(&(sock->window.ack_lock)) != 0) {
  }
  result = after(sock->window.last_ack_received, seq);
  pthread_mutex_unlock(&(sock->window.ack_lock));
  return result;
}

/**
 * Updates the socket information to represent the newly received packet.
 *
 * In the current stop-and-wait implementation, this function also sends an
 * acknowledgement for the packet.
 *
 * @param sock The socket used for handling packets received.
 * @param pkt The packet data received by the socket.
 */
void handle_message(cmu_socket_t *sock, uint8_t *pkt) {
  cmu_tcp_header_t *hdr = (cmu_tcp_header_t *)pkt;
  uint8_t flags = get_flags(hdr);

  switch (flags) {
    case ACK_FLAG_MASK: {
      uint32_t ack = get_ack(hdr);
      uint32_t seq = get_seq(hdr);
      if (after(ack, sock->window.last_ack_received)) {
        sock->window.last_ack_received = ack;
        sock->window.next_seq_expected = seq;
      }
      if (sock->state == SYN_RCVD) {
        sock->state = ESTABLISHED;  // 服务器收到ACK，握手完成
      }
      break;
    }
    // 服务器端收到SYN，状态切换到SYN_RCVD
    case SYN_FLAG_MASK: {
      sock->window.last_ack_received = get_ack(hdr);
      sock->state = SYN_RCVD;
      break;
    }
    //服务端响应后客户端状态更新
    case ACK_FLAG_MASK | SYN_FLAG_MASK:
      sock->window.last_ack_received = get_ack(hdr);
      sock->window.next_seq_expected = get_seq(hdr);
      sock->state = ESTABLISHED;
      //第三次握手
      uint8_t *msg = create_packet(sock->my_port,sock->conn.sin_port,sock->window.last_ack_received+1,sock->window.next_seq_expected,
      sizeof(cmu_tcp_header_t), sizeof(cmu_tcp_header_t),ACK_FLAG_MASK, 1, 0, NULL, NULL, 0);
      sendto(sock->socket,msg,sizeof(cmu_tcp_header_t),0,(struct sockaddr *)&(sock->conn), sizeof(sock->conn));
      
      printf("第三次握手\n");
      free(msg);
      break;
    default: {
      socklen_t conn_len = sizeof(sock->conn);
      uint32_t seq = sock->window.last_ack_received;

      // No payload.
      uint8_t *payload = NULL;
      uint16_t payload_len = 0;

      // No extension.
      uint16_t ext_len = 0;
      uint8_t *ext_data = NULL;

      uint16_t src = sock->my_port;
      uint16_t dst = ntohs(sock->conn.sin_port);
      uint32_t ack = get_seq(hdr) + get_payload_len(pkt);
      uint16_t hlen = sizeof(cmu_tcp_header_t);
      uint16_t plen = hlen + payload_len;
      uint8_t flags = ACK_FLAG_MASK;
      uint16_t adv_window = 1;
      uint8_t *response_packet =
          create_packet(src, dst, seq, ack, hlen, plen, flags, adv_window,
                        ext_len, ext_data, payload, payload_len);

      sendto(sock->socket, response_packet, plen, 0,
             (struct sockaddr *)&(sock->conn), conn_len);
      free(response_packet);

      seq = get_seq(hdr);

      if (seq == sock->window.next_seq_expected) {
        sock->window.next_seq_expected = seq + get_payload_len(pkt);
        payload_len = get_payload_len(pkt);
        payload = get_payload(pkt);

        // Make sure there is enough space in the buffer to store the payload.
        sock->received_buf =
            realloc(sock->received_buf, sock->received_len + payload_len);
        memcpy(sock->received_buf + sock->received_len, payload, payload_len);
        sock->received_len += payload_len;
      }
    }
  }
}

/**
 * Checks if the socket received any data.
 *
 * It first peeks at the header to figure out the length of the packet and then
 * reads the entire packet.
 *
 * @param sock The socket used for receiving data on the connection.
 * @param flags Flags that determine how the socket should wait for data. Check
 *             `cmu_read_mode_t` for more information.
 */
void check_for_data(cmu_socket_t *sock, cmu_read_mode_t flags) {
  cmu_tcp_header_t hdr;
  uint8_t *pkt;
  socklen_t conn_len = sizeof(sock->conn);
  ssize_t len = 0;
  uint32_t plen = 0, buf_size = 0, n = 0;

  while (pthread_mutex_lock(&(sock->recv_lock)) != 0) {
  }
  switch (flags) {
    case NO_FLAG:
      len = recvfrom(sock->socket, &hdr, sizeof(cmu_tcp_header_t), MSG_PEEK,
                     (struct sockaddr *)&(sock->conn), &conn_len);
      break;
    case TIMEOUT: {
      // Using `poll` here so that we can specify a timeout.
      struct pollfd ack_fd;
      ack_fd.fd = sock->socket;
      ack_fd.events = POLLIN;
      // Timeout after 3 seconds.
      if (poll(&ack_fd, 1, 3000) <= 0) {
        break;
      }
    }
    // Fallthrough.
    case NO_WAIT:
      len = recvfrom(sock->socket, &hdr, sizeof(cmu_tcp_header_t),
                     MSG_DONTWAIT | MSG_PEEK, (struct sockaddr *)&(sock->conn),
                     &conn_len);
      break;
    default:
      perror("ERROR unknown flag");
  }
  if (len >= (ssize_t)sizeof(cmu_tcp_header_t)) {
    plen = get_plen(&hdr);
    pkt = malloc(plen);
    while (buf_size < plen) {
      n = recvfrom(sock->socket, pkt + buf_size, plen - buf_size, 0,
                   (struct sockaddr *)&(sock->conn), &conn_len);
      buf_size = buf_size + n;
    }
    handle_message(sock, pkt);
    free(pkt);
  }
  pthread_mutex_unlock(&(sock->recv_lock));
}

/**
 * Breaks up the data into packets and sends a single packet at a time.
 *
 * You should most certainly update this function in your implementation.
 *
 * @param sock The socket to use for sending data.
 * @param data The data to be sent.
 * @param buf_len The length of the data being sent.
 */
void single_send(cmu_socket_t *sock, uint8_t *data, int buf_len) {
  uint8_t *msg;
  uint8_t *data_offset = data;
  size_t conn_len = sizeof(sock->conn);

  int sockfd = sock->socket;
  if (buf_len > 0) {
    while (buf_len != 0) {
      uint16_t payload_len = MIN((uint16_t)buf_len, (uint16_t)MSS);

      uint16_t src = sock->my_port;
      uint16_t dst = ntohs(sock->conn.sin_port);
      uint32_t seq = sock->window.last_ack_received;
      uint32_t ack = sock->window.next_seq_expected;
      uint16_t hlen = sizeof(cmu_tcp_header_t);
      uint16_t plen = hlen + payload_len;
      uint8_t flags = 0;
      uint16_t adv_window = 1;
      uint16_t ext_len = 0;
      uint8_t *ext_data = NULL;
      uint8_t *payload = data_offset;

      msg = create_packet(src, dst, seq, ack, hlen, plen, flags, adv_window,
                          ext_len, ext_data, payload, payload_len);
      buf_len -= payload_len;

      while (1) {
        // FIXME: This is using stop and wait, can we do better?
        sendto(sockfd, msg, plen, 0, (struct sockaddr *)&(sock->conn),
               conn_len);
        check_for_data(sock, TIMEOUT);
        if (has_been_acked(sock, seq)) {
          break;
        }
      }
      data_offset += payload_len;
    }
  }
}

void client_handshake(cmu_socket_t *sock) {
  sock->state = CLOSED;
  int seq = rand();
  uint8_t *msg;
  while(1){
    if (sock->state == CLOSED) {
      msg = create_packet(sock->my_port, sock->conn.sin_port, seq, 0,
                          sizeof(cmu_tcp_header_t), sizeof(cmu_tcp_header_t),
                          SYN_FLAG_MASK, 1, 0, NULL, NULL, 0);
      sendto(sock->socket, msg, sizeof(cmu_tcp_header_t), 0,
             (struct sockaddr *)&(sock->conn), sizeof(sock->conn));
      sock->state = SYN_SENT;
      printf("第一次握手\n");
      free(msg);
    } else if (sock->state == SYN_SENT) {
      check_for_data(sock,TIMEOUT);
      if (sock->state == SYN_SENT) {
        sock->state = CLOSED;
      }else{
        break;
      }
    } else if (sock->state == ESTABLISHED ) {
      break;
    }
  }
}

/*
 * 检查SYN是否有效，如果无效，不应答
 * 发送SYNACK来应答SYN
 */

void server_handshake(cmu_socket_t *sock) {
  sock->state = LISTEN;
  // int three_handshake_succ = FALSE;
  int seq = rand();  // 随机生成序号
  uint8_t *msg;
  while (1) {
    if (sock->state == LISTEN) {
      check_for_data(
          sock, NO_WAIT);  // 监听SYN，read mode是NO_WAIT，如果没有数据立即返回
      // printf("LISTEN");

    } else if (sock->state == SYN_RCVD) {  // 如果收到SYN，应答SYNACK，等客户回复ACK
      // printf("SYN_RCVD");

      sock->window.last_ack_received = seq;
      int ack = sock->window.next_seq_expected;
      msg = create_packet(sock->my_port, sock->conn.sin_port, seq, ack,
                          sizeof(cmu_tcp_header_t), sizeof(cmu_tcp_header_t),
                          ACK_FLAG_MASK | SYN_FLAG_MASK, 1, 0, NULL, NULL, 0);
      sendto(sock->socket, msg, sizeof(cmu_tcp_header_t), 0,
             (struct sockaddr *)&(sock->conn), sizeof(sock->conn));
      printf("第二次握手\n");
      free(msg);
      check_for_data(
          sock,
          TIMEOUT);  // TIMEOUT：阻塞，直到收到数据或超时。如果超时没收到ACK则重传SYNACK
      if (sock->state == ESTABLISHED) {
        break;
      }
    }
  }
}

void init_handshake(cmu_socket_t *sock) {
  if (sock->type == TCP_INITIATOR) {
    client_handshake(sock);
  } else if (sock->type == TCP_LISTENER) {
    server_handshake(sock);
  }
}

void *begin_backend(void *in) {
  cmu_socket_t *sock = (cmu_socket_t *)in;
  int death, buf_len, send_signal;
  uint8_t *data;

  printf("start handshake\n");
  init_handshake(sock);
  printf("end handshake\n");

  while (1) {
    while (pthread_mutex_lock(&(sock->death_lock)) != 0) {
    }
    death = sock->dying;
    pthread_mutex_unlock(&(sock->death_lock));

    while (pthread_mutex_lock(&(sock->send_lock)) != 0) {
    }
    buf_len = sock->sending_len;

    if (death && buf_len == 0) {
      break;
    }

    if (buf_len > 0) {
      data = malloc(buf_len);
      memcpy(data, sock->sending_buf, buf_len);
      sock->sending_len = 0;
      free(sock->sending_buf);
      sock->sending_buf = NULL;
      pthread_mutex_unlock(&(sock->send_lock));
      single_send(sock, data, buf_len);
      free(data);
    } else {
      pthread_mutex_unlock(&(sock->send_lock));
    }

    check_for_data(sock, NO_WAIT);

    while (pthread_mutex_lock(&(sock->recv_lock)) != 0) {
    }

    send_signal = sock->received_len > 0;

    pthread_mutex_unlock(&(sock->recv_lock));

    if (send_signal) {
      pthread_cond_signal(&(sock->wait_cond));
    }
  }

  pthread_exit(NULL);
  return NULL;
}
