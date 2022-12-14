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
#include <time.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "cmu_packet.h"
#include "cmu_tcp.h"

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define FALSE 0
#define TRUE 1

//use for RTT
#define ALPHA 0.125
#define BETA 0.25

//time out 
#define TIMEOUT_MILLSEC 3000

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
 * get the window index for seq
 */
int get_window_index(uint32_t seq) {
  uint32_t window_size = WINDOW_INITIAL_WINDOW_SIZE / MSS;
  return seq / MSS % window_size;
}

/**
 * search next_expected_seq = the max seq,
 * and ensure all previous packets are received.
 */
uint32_t get_next_expected_seq(cmu_socket_t *sock) {
  uint32_t next_expected_seq = sock->window.next_seq_expected;
  uint32_t window_size = WINDOW_INITIAL_WINDOW_SIZE / MSS;
  while (1) {
    int index = get_window_index(next_expected_seq);
    if (sock->window.received_windows[index].seq == 0 || sock->window.received_windows[index].seq != next_expected_seq) {
      break;
    }
    next_expected_seq += sock->window.received_windows[index].payload_len;
  }
  return next_expected_seq;
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
      }
      if (sock->state == SYN_RCVD) {
        sock->state = ESTABLISHED;  // ???????????????ACK???????????????
      }
      break;
    }
    // ??????????????????SYN??????????????????SYN_RCVD
    case SYN_FLAG_MASK: {
      // FIX: sock->window.last_ack_received = get_ack(hdr);
      // should be as follow
      sock->window.next_seq_expected = get_seq(hdr) + 1;
      sock->state = SYN_RCVD;
      break;
    }
    //???????????????????????????????????????
    case ACK_FLAG_MASK | SYN_FLAG_MASK:
      sock->window.last_ack_received = get_ack(hdr);
      sock->window.next_seq_expected = get_seq(hdr) + 1;
      sock->state = ESTABLISHED;
      //???????????????
      uint8_t *msg = create_packet(
          sock->my_port, sock->conn.sin_port, sock->window.last_ack_received,
          sock->window.next_seq_expected, sizeof(cmu_tcp_header_t),
          sizeof(cmu_tcp_header_t), ACK_FLAG_MASK, 1, 0, NULL, NULL, 0);
      sendto(sock->socket, msg, sizeof(cmu_tcp_header_t), 0,
             (struct sockaddr *)&(sock->conn), sizeof(sock->conn));
      printf("client ???san?????????\n");
      free(msg);
      break;
    default: {
      // if not established, then ignore data packet
      if (sock->state != ESTABLISHED) {
        break;
      }
      socklen_t conn_len = sizeof(sock->conn);

      // No payload.
      uint8_t *payload = get_payload(pkt);
      uint16_t payload_len = get_payload_len(pkt);

      // No extension.
      uint16_t ext_len = 0;
      uint8_t *ext_data = NULL;

      uint16_t src = sock->my_port;
      uint16_t dst = ntohs(sock->conn.sin_port);
      uint32_t seq = get_seq(hdr);
      uint32_t new_seq = seq + payload_len;

      int index = get_window_index(seq);
    
      adjust_sock_rtt(sock,index);

      // copy the packet data receive windows
      sock->window.received_windows[index].seq = seq;
      sock->window.received_windows[index].payload_len = payload_len;
      //realloc(sock->window.received_windows[index].payload,payload);
      memcpy(sock->window.received_windows[index].payload, payload,
             payload_len);

      // get the new next expected seq thru received_infos.
      uint32_t next_expected_seq = get_next_expected_seq(sock);
      int next_index = get_window_index(next_expected_seq);
      // the new ACK seq

      uint16_t hlen = sizeof(cmu_tcp_header_t);
      uint16_t plen = hlen + payload_len;
      uint8_t flags = ACK_FLAG_MASK;
      uint16_t adv_window = 1;

      // update the check logic received_buf.
      // DONE: copy all ready payload to received_buf.
      uint32_t curr_expected_seq = sock->window.next_seq_expected;
      uint32_t new_ack = curr_expected_seq;
    
      if (curr_expected_seq < next_expected_seq) {
        new_ack = next_expected_seq +
                  sock->window.received_windows[next_index].payload_len;
        // Copy all data between data [curr_expected_seq, next_expected_seq) to
        // received_buf
        while (curr_expected_seq < next_expected_seq) {
          int curr_index = get_window_index(curr_expected_seq);
          
          uint32_t payload_len =
              sock->window.received_windows[curr_index].payload_len;
          // Make sure there is enough space in the buffer to store the payload.
          sock->received_buf =
              realloc(sock->received_buf, sock->received_len + payload_len);
          memcpy(sock->received_buf + sock->received_len, sock->window.received_windows[curr_index].payload, payload_len);
          sock->received_len += payload_len;
          curr_expected_seq += sock->window.received_windows[curr_index].payload_len;
        }
      }

      uint8_t *response_packet =
          create_packet(src, dst, seq, new_ack, hlen, plen, flags, adv_window,
                        ext_len, ext_data, payload, payload_len);
      sendto(sock->socket, response_packet, plen, 0,
             (struct sockaddr *)&(sock->conn), conn_len);
      free(response_packet);
      sock->window.next_seq_expected = next_expected_seq;
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
    // Fall through.
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
 * Get current milliseconds
 * 
 */
uint64_t get_curr_milliseconds() {
  struct timeval time_now = {0};
  long time_sec = 0;
  long time_mil = 0;

  gettimeofday(&time_now,NULL);
  time_sec = time_now.tv_sec;
  time_mil = time_sec * 1000 + time_now.tv_usec/1000;
  return time_mil;
}


/**
 * Calculate the RTT for current sock
 * TODO: fix me
 */
uint64_t adjust_sock_rtt(cmu_socket_t* sock,int index) { 
  uint64_t nowTime;
  nowTime = get_curr_milliseconds();
  uint64_t sampleRtt = nowTime - sock->window.sending_windows[index].send_time;
  sock->estRtt = (long)(((float)(1-ALPHA))*sock->estRtt + ALPHA*sampleRtt);
  sock->devRtt = (long)((1-BETA)*sock->devRtt + BETA*abs(sampleRtt-sock->estRtt));
  sock->estRto = sock->estRtt + 4*sock->devRtt;
  printf("sock->estRTO %ld\n",sock->estRto);
  return sock->estRto; 
  }

/**
 * send single packet for special seq and payload
 */
void single_send_for_seq(cmu_socket_t *sock, uint8_t *payload,
                         uint16_t payload_len, uint32_t seq) {
  uint16_t src = sock->my_port;
  uint16_t dst = ntohs(sock->conn.sin_port);
  // uint32_t seq = seq;
  uint32_t ack = sock->window.next_seq_expected;
  uint16_t hlen = sizeof(cmu_tcp_header_t);
  uint16_t plen = hlen + payload_len;
  uint8_t flags = 0;
  uint16_t adv_window = 1;
  uint16_t ext_len = 0;
  uint8_t *ext_data = NULL;
  // uint8_t *payload = payload;
  size_t conn_len = sizeof(sock->conn);
  uint8_t *msg =
      create_packet(src, dst, seq, ack, hlen, plen, flags, adv_window, ext_len,
                    ext_data, payload, payload_len);
  sendto(sock->socket, msg, plen, 0, (struct sockaddr *)&(sock->conn),
         conn_len);
  free(msg);
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
    uint32_t windows_size = WINDOW_INITIAL_WINDOW_SIZE / MSS;
    int32_t i = 1;
    // the beginning/start, end/finish seq for data.
    uint32_t seq = sock->window.last_ack_received;
    uint32_t buf_end_seq = sock->window.last_ack_received + buf_len;
    // the max seq has been sent
    uint32_t max_seq_sent = sock->window.last_ack_received;
    // loop until all sent buf received ACK
    while (sock->window.last_ack_received < buf_end_seq) {
      // if have new buf could be sent, and has data not sent
      if (sock->window.last_ack_received + WINDOW_INITIAL_WINDOW_SIZE >
              max_seq_sent &&
          max_seq_sent < buf_end_seq) {
           
        uint16_t payload_len =
            MIN((uint16_t)(buf_len - i * MSS), (uint16_t)MSS);
        uint16_t src = sock->my_port;
        uint16_t dst = ntohs(sock->conn.sin_port);
        // seq += payload_len;

        uint32_t ack = sock->window.next_seq_expected;
        uint16_t hlen = sizeof(cmu_tcp_header_t);
        uint16_t plen = hlen + payload_len;
        uint8_t flags = 0;
        uint16_t adv_window = 1;
        uint16_t ext_len = 0;
        uint8_t *ext_data = NULL;
        uint8_t *payload = data_offset;
        single_send_for_seq(sock, payload, payload_len, seq);
        
        int j = i % windows_size;
        sock->window.sending_windows[j].send_time = get_curr_milliseconds();
        sock->window.sending_windows[j].payload = payload;
        sock->window.sending_windows[j].payload_len = payload_len;
        sock->window.sending_windows[j].seq = seq;

        seq += payload_len;
        data_offset += payload_len;
        max_seq_sent += payload_len;

        i++;
      }
      // check data without waiting..
        check_for_data(sock, NO_WAIT);

      // try to resend all timeout with more than 3 RTT or timeout after 3 seconds
      for (int j = 0; j < windows_size; j++) {
        if (sock->window.sending_windows[j].send_time > 0 &&
            sock->window.sending_windows[j].seq >
                sock->window.last_ack_received && (
            get_curr_milliseconds() >
                3 * sock->estRto + sock->window.sending_windows[j].send_time ||
            get_curr_milliseconds() > TIMEOUT_MILLSEC + sock->window.sending_windows[j].send_time)) {
          // resend packet j...
          single_send_for_seq(sock, sock->window.sending_windows[j].payload,
                              sock->window.sending_windows[j].payload_len,
                              sock->window.sending_windows[j].seq);
          sock->window.sending_windows[j].send_time = get_curr_milliseconds();
        }
      }
    }
  }
}

void client_handshake(cmu_socket_t *sock) {
  sock->state = CLOSED;
  srand(time(0));
  int seq = rand()%100 +1;
  uint8_t *msg;
  while (1) {
    if (sock->state == CLOSED) {
      msg = create_packet(sock->my_port, sock->conn.sin_port, seq, 0,
                          sizeof(cmu_tcp_header_t), sizeof(cmu_tcp_header_t),
                          SYN_FLAG_MASK, 1, 0, NULL, NULL, 0);
      sendto(sock->socket, msg, sizeof(cmu_tcp_header_t), 0,
             (struct sockaddr *)&(sock->conn), sizeof(sock->conn));
      sock->state = SYN_SENT;
      printf("client ??????????????? seq:%d, ack:%d\n",seq,0);
      free(msg);
    } else if (sock->state == SYN_SENT) {
      check_for_data(sock, TIMEOUT);
      if (sock->state == SYN_SENT) {
        sock->state = CLOSED;
      } else {
        break;
      }
    } else if (sock->state == ESTABLISHED) {
      break;
    }
  }
}

/*
 * ??????SYN???????????????????????????????????????
 * ??????SYNACK?????????SYN
 */

void server_handshake(cmu_socket_t *sock) {
  sock->state = LISTEN;
  // int three_handshake_succ = FALSE;
  srand(time(0));
  int seq = rand()%100 +1;  // ??????????????????
  uint8_t *msg;
  while (1) {
    if (sock->state == LISTEN) {
      check_for_data(
          sock, NO_WAIT);  // ??????SYN???read mode???NO_WAIT?????????????????????????????????
      // printf("LISTEN");

    } else if (sock->state ==
               SYN_RCVD) {  // ????????????SYN?????????SYNACK??????????????????ACK
      // printf("SYN_RCVD");

      sock->window.last_ack_received = seq;
      int ack = sock->window.next_seq_expected;
      msg = create_packet(sock->my_port, sock->conn.sin_port, seq, ack,
                          sizeof(cmu_tcp_header_t), sizeof(cmu_tcp_header_t),
                          ACK_FLAG_MASK | SYN_FLAG_MASK, 1, 0, NULL, NULL, 0);
      sendto(sock->socket, msg, sizeof(cmu_tcp_header_t), 0,
             (struct sockaddr *)&(sock->conn), sizeof(sock->conn));
      printf("server ???????????????, seq:%d, ack:%d\n",seq,ack);
      free(msg);
      check_for_data(
          sock,
          TIMEOUT);  // TIMEOUT???????????????????????????????????????????????????????????????ACK?????????SYNACK
      if (sock->state == ESTABLISHED) {
        //check_for_data(sock,NO_FLAG);
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
  // init
  uint32_t window_size = WINDOW_INITIAL_WINDOW_SIZE / MSS;
  sock->window.sending_windows =
      (sending_window *)malloc(sizeof(sending_window) * window_size);
  memset(sock->window.sending_windows, 0, sizeof(sending_window) * window_size);
  sock->window.received_windows =
      (receiving_window *)malloc(sizeof(receiving_window) * window_size);
  memset(sock->window.received_windows, 0,
         sizeof(receiving_window) * window_size);
  for (int i = 0; i < window_size; i++) {
    sock->window.received_windows[i].payload = NULL;
    sock->window.received_windows[i].payload =
        (uint8_t *)malloc(sizeof(uint8_t) * MSS*2);
  }
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

  free(sock->window.sending_windows);
  for (int i = 0; i < window_size; i++) {
    free(sock->window.received_windows[i].payload);
  }
  free(sock->window.received_windows);

  pthread_exit(NULL);
  return NULL;
}