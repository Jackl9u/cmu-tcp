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
#include <assert.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>

#include "cmu_packet.h"
#include "cmu_tcp.h"

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

/* ******************************************************************************************* */
/* ******************************************************************************************* */
/* ******************************************************************************************* */

long get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void handle_message(void *, uint8_t*) {

}

int counter1 = 0;
int counter2 = 0;

void init_handshake_client(void *in) {
  printf("client start handshake\n");
  cmu_socket_t *sock = (cmu_socket_t *)in;
  assert(sock->type == TCP_INITIATOR);

  // send the initial SYN packet
  uint16_t payload_len = 0;
  uint8_t *payload = NULL;
  uint16_t ext_len = 0;
  uint8_t *ext_data = NULL;
  uint16_t src = sock->my_port;
  uint16_t dst = ntohs(sock->conn.sin_port);
  uint32_t seq = sock->window.last_ack_received;
  uint32_t ack = 0; // ack doesn't matter in this SYN
  uint16_t hlen = sizeof(cmu_tcp_header_t);
  uint16_t plen = hlen + ext_len + payload_len;
  uint8_t flags = SYN_FLAG_MASK;
  uint16_t adv_window = CP1_WINDOW_SIZE;
  
  uint8_t *packet =
      create_packet(src, dst, seq, ack, hlen, plen, flags, adv_window,
                    ext_len, ext_data, payload, payload_len);

  socklen_t conn_len = sizeof(sock->conn);

  if (counter1 >= 3) {
    sendto(sock->socket, packet, plen, 0,
          (struct sockaddr *)&(sock->conn), conn_len);
  } else {
    counter1 += 1;
  }
  
  sock->state = SYN_SENT;

  while (sock->state != ESTABLISHED) {
    // wait at most DEFAULT_TIMEOUT for the reply
    printf("client in while loop\n");
    struct pollfd ack_fd;
    ack_fd.fd = sock->socket;
    ack_fd.events = POLLIN;
    if (poll(&ack_fd, 1, DEFAULT_TIMEOUT) <= 0) {
      // reached timeout and still don't have ack, resend the packet
      printf("re-send message\n");
      if (counter1 >= 3) {
        sendto(sock->socket, packet, plen, 0,
              (struct sockaddr *)&(sock->conn), conn_len);
      } else {
        counter1 += 1;
      }
    } else {
      cmu_tcp_header_t hdr;
      ssize_t len = recvfrom(sock->socket, &hdr, sizeof(cmu_tcp_header_t),
                     MSG_DONTWAIT | MSG_PEEK, (struct sockaddr *)&(sock->conn),
                     &conn_len);
      printf("client 1\n");
      if (len == sizeof(cmu_tcp_header_t)) {
        printf("client 2\n");
        len = recvfrom(sock->socket, &hdr, sizeof(cmu_tcp_header_t), 0,
                   (struct sockaddr *)&(sock->conn), &conn_len);
        assert(len == sizeof(cmu_tcp_header_t));
      
        // the packet must be SYN-ACK
        assert(get_flags(&hdr) & SYN_FLAG_MASK);
        assert(get_flags(&hdr) & ACK_FLAG_MASK);
        
        assert(get_ack(&hdr) == sock->window.last_ack_received + 1);
        sock->window.last_ack_received = get_ack(&hdr);
        sock->window.next_seq_expected = get_seq(&hdr)+1;

        // send ACK packet
        free(packet);

        payload_len = 0;
        payload = NULL;
        ext_len = 0;
        ext_data = NULL;
        src = sock->my_port;
        dst = ntohs(sock->conn.sin_port);
        seq = sock->window.last_ack_received;
        ack = sock->window.next_seq_expected;
        hlen = sizeof(cmu_tcp_header_t);
        plen = hlen + ext_len + payload_len;
        flags = ACK_FLAG_MASK;
        adv_window = CP1_WINDOW_SIZE;
        
        packet =
            create_packet(src, dst, seq, ack, hlen, plen, flags, adv_window,
                          ext_len, ext_data, payload, payload_len);

        if (counter2 >= 0) {
          sendto(sock->socket, packet, plen, 0,
                (struct sockaddr *)&(sock->conn), conn_len);
        } else {
          counter2 += 1;
        }
        
        printf("client sent ack packet back\n");

        free(packet);

        sock->state = ESTABLISHED;
      }
    }
  }
  printf("client finished handshake\n");
}

int counter3 = 0;

void init_handshake_server(void *in) {
  printf("server start handshake\n");
  cmu_socket_t *sock = (cmu_socket_t *)in;
  socklen_t conn_len = sizeof(sock->conn);
  assert(sock->type == TCP_LISTENER);

  sock->state = LISTEN;

  uint16_t payload_len;
  uint8_t *payload;
  uint16_t ext_len;
  uint8_t *ext_data;
  uint16_t src;
  uint16_t dst;
  uint32_t seq;
  uint32_t ack;
  uint16_t hlen;
  uint16_t plen;
  uint8_t flags;
  uint16_t adv_window;
  
  uint8_t *packet;

  while (sock->state != ESTABLISHED) {
    if (sock->state == LISTEN) {
      printf("server in LISTEN\n");
      cmu_tcp_header_t hdr;
      ssize_t len = recvfrom(sock->socket, &hdr, sizeof(cmu_tcp_header_t),
                     0, (struct sockaddr *)&(sock->conn), &conn_len);

      assert(len == sizeof(cmu_tcp_header_t));

      // the packet must be SYN
      assert(get_flags(&hdr) == SYN_FLAG_MASK);

      sock->window.next_seq_expected = get_seq(&hdr) + 1;

      // send the SYN-ACK packet
      payload_len = 0;
      payload = NULL;
      ext_len = 0;
      ext_data = NULL;
      src = sock->my_port;
      dst = ntohs(sock->conn.sin_port);
      seq = sock->window.last_ack_received;
      ack = sock->window.next_seq_expected;
      hlen = sizeof(cmu_tcp_header_t);
      plen = hlen + ext_len + payload_len;
      flags = SYN_FLAG_MASK | ACK_FLAG_MASK;
      adv_window = CP1_WINDOW_SIZE;
      
      packet = create_packet(src, dst, seq, ack, hlen, plen, flags, adv_window,
                    ext_len, ext_data, payload, payload_len);

      if (counter3 >= 3) {
        sendto(sock->socket, packet, plen, 0,
          (struct sockaddr *)&(sock->conn), conn_len);
      } else {
        counter3 += 1;
      }

      sock->state = SYN_RCVD;
    } else {
      assert(sock->state == SYN_RCVD);
      printf("in SYN_RCVD state \n");

      cmu_tcp_header_t hdr;
      ssize_t len = recvfrom(sock->socket, &hdr, sizeof(cmu_tcp_header_t),
                    MSG_WAITALL | MSG_PEEK, (struct sockaddr *)&(sock->conn),
                    &conn_len);

      if (len == (ssize_t)sizeof(cmu_tcp_header_t)) {
        uint32_t packet_len = get_plen(&hdr);
        uint8_t* pkt = malloc(packet_len);
        uint32_t recv_len = 0;
        while (recv_len < packet_len) {
          uint32_t n = recvfrom(sock->socket, pkt + recv_len, packet_len - recv_len, 0,
                  (struct sockaddr *)&(sock->conn), &conn_len);
          recv_len += n;
        }

        // can transit to ESTABLISHED if the received packet
        // 1) has ACK flag set
        // 2) ack_num = sock->window.last_ack_received + 1
        // this should be true no matter this is the pure ACK sent by the client when it first received
        // the SYN-ACK from the server, or when it's some later packets when the client is already ESTABLISHED
        if ((get_flags(&hdr) & ACK_FLAG_MASK) && get_ack(&hdr) == sock->window.last_ack_received + 1) {
          sock->state = ESTABLISHED;
          handle_message(in, pkt);
        } else {
          // the SYN-ACK was not received by the client, so client repeatly send SYN packet
          assert(get_flags(&hdr) & SYN_FLAG_MASK);
          if (counter3 >= 3) {
            sendto(sock->socket, packet, plen, 0,
              (struct sockaddr *)&(sock->conn), conn_len);
          } else {
            counter3 += 1;
          }
        }

        free(pkt);
      }
    }
  }

  if (packet != NULL) {
    free(packet);
  }

  printf("server finished handshake\n");
}

void *begin_backend(void *in) {
  cmu_socket_t *sock = (cmu_socket_t *)in;
  // int death, buf_len, send_signal;
  // uint8_t *data;

  // 3-way handshake
  if (sock->type == TCP_INITIATOR) {
    init_handshake_client(in);
  } else {
    init_handshake_server(in);
  }

  // while (1) {
  //   while (pthread_mutex_lock(&(sock->death_lock)) != 0) {
  //   }
  //   death = sock->dying;
  //   pthread_mutex_unlock(&(sock->death_lock));

  //   while (pthread_mutex_lock(&(sock->send_lock)) != 0) {
  //   }
  //   buf_len = send_buffer_max_send(sock->send_buf);

  //   if (death && buf_len == 0) {
  //     break;
  //   }

  //   // check the time passed since the last send
  //   if (sock->last_send_ms == 0) {
  //     // everything sent so far got ACKed
  //   } else {
  //     long now = get_time_ms();
  //     if (now - sock->last_send_ms > DEFAULT_TIMEOUT) {
  //       // send again


  //     }
  //   }

  //   // receive data
  //   // ..

  //   if (buf_len > 0) {
  //     data = malloc(buf_len);
  //     memcpy(data, sock->sending_buf, buf_len);
  //     sock->sending_len = 0;
  //     free(sock->sending_buf);
  //     sock->sending_buf = NULL;
  //     pthread_mutex_unlock(&(sock->send_lock));
  //     single_send(sock, data, buf_len);
  //     free(data);
  //   } else {
  //     pthread_mutex_unlock(&(sock->send_lock));
  //   }

  //   check_for_data(sock, NO_WAIT);

  //   while (pthread_mutex_lock(&(sock->recv_lock)) != 0) {
  //   }

  //   send_signal = sock->received_len > 0;

  //   pthread_mutex_unlock(&(sock->recv_lock));

  //   if (send_signal) {
  //     pthread_cond_signal(&(sock->wait_cond));
  //   }
  // }

  pthread_exit(NULL);
  return NULL;
}
