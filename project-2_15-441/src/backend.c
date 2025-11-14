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
#include "recv_buffer.h"
#include "send_buffer.h"

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

/* ******************************************************************************************* */
/* ******************************************************************************************* */
/* ******************************************************************************************* */

void handle_message(void *in, uint8_t* pkt) {
  cmu_socket_t *sock = (cmu_socket_t *)in;
  socklen_t conn_len = sizeof(sock->conn);
  cmu_tcp_header_t* hdr = (cmu_tcp_header_t*)pkt;

  // require all packets to carry ACK number and advertised_window
  assert(get_flags(hdr) & ACK_FLAG_MASK);
  // should not see a SYN packet after initial handshake
  assert(!(get_flags(hdr) & SYN_FLAG_MASK));

  if (get_flags(hdr) & FIN_FLAG_MASK) {
    // TDDO : special handling for a FIN packet

  } else {
    // update ACK
    uint32_t acknum = get_ack(hdr);
    if (acknum == sock->window.last_ack_received) {
      // TODO : duplicate ACK, congestion control

    } else {
      sock->window.last_ack_received = MAX(sock->window.last_ack_received, acknum);
      while (pthread_mutex_lock(&(sock->send_lock)) != 0) {
      }
      send_buffer_update_ack(sock->send_buf, acknum);
      pthread_mutex_unlock(&(sock->send_lock));
    }

    // update advertised window
    sock->window.rcvd_advertised_window = get_advertised_window(hdr);
    if (sock->window.rcvd_advertised_window == 0) {
      // TODO : zero window probe

    }

    uint16_t payload_len = get_payload_len(pkt);
    if (payload_len != 0) {
      // not pure-ACK packet, has some data
      uint32_t seqnum = get_seq(hdr);
      // doesn't matter if the seqnum is what we expected, we still try to receive it
      // after receiving it and update the internal recv_buffer, then send an ACK back
      while (pthread_mutex_lock(&(sock->recv_lock)) != 0) {
      }
      if (recv_buffer_can_receive(sock->recv_buf, seqnum, payload_len) == 0) {
        recv_buffer_receive(sock->recv_buf, seqnum, payload_len, get_payload(pkt));
        pthread_cond_signal(&(sock->wait_cond));
        // printf("notified\n");
      }
      sock->window.next_seq_expected = get_next_byte_expected_seqnum(sock->recv_buf);
      pthread_mutex_unlock(&(sock->recv_lock));
      
      // respond with a pure ACK message
      payload_len = 0;
      uint8_t *payload = NULL;
      uint16_t ext_len = 0;
      uint8_t *ext_data = NULL;
      uint16_t src = sock->my_port;
      uint16_t dst = ntohs(sock->conn.sin_port);
      uint32_t seq = sock->window.last_ack_received;
      uint32_t ack = sock->window.next_seq_expected;
      uint16_t hlen = sizeof(cmu_tcp_header_t);
      uint16_t plen = hlen + ext_len + payload_len;
      uint8_t flags = ACK_FLAG_MASK;
      while (pthread_mutex_lock(&(sock->recv_lock)) != 0) {
      }
      uint16_t adv_window = recv_buffer_max_receive(sock->recv_buf);
      pthread_mutex_unlock(&(sock->recv_lock));

      uint8_t *packet =
          create_packet(src, dst, seq, ack, hlen, plen, flags, adv_window,
                        ext_len, ext_data, payload, payload_len);
      
      sendto(sock->socket, packet, plen, 0,
          (struct sockaddr *)&(sock->conn), conn_len);
      free(packet);
    }
  }
}

int counter1 = 0;
int counter2 = 0;
int counter3 = 0;

int counter1_lim = 0;
int counter2_lim = 0;
int counter3_lim = 0;

void init_handshake_client(void *in) {
  // printf("client start handshake\n");
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

  if (counter1 >= counter1_lim) {
    sendto(sock->socket, packet, plen, 0,
          (struct sockaddr *)&(sock->conn), conn_len);
  } else {
    counter1 += 1;
  }
  
  sock->state = SYN_SENT;

  while (sock->state != ESTABLISHED) {
    // wait at most DEFAULT_TIMEOUT for the reply
    // printf("client in while loop\n");
    struct pollfd ack_fd;
    ack_fd.fd = sock->socket;
    ack_fd.events = POLLIN;
    if (poll(&ack_fd, 1, DEFAULT_TIMEOUT) <= 0) {
      // reached timeout and still don't have ack, resend the packet
      // printf("re-send message\n");
      if (counter1 >= counter1_lim) {
        sendto(sock->socket, packet, plen, 0,
              (struct sockaddr *)&(sock->conn), conn_len);
      } else {
        counter1 += 1;
      }
    } else {
      cmu_tcp_header_t hdr;
      ssize_t len = recvfrom(sock->socket, &hdr, sizeof(cmu_tcp_header_t),
                     MSG_PEEK, (struct sockaddr *)&(sock->conn),
                     &conn_len);
      if (len == sizeof(cmu_tcp_header_t)) {
        len = recvfrom(sock->socket, &hdr, sizeof(cmu_tcp_header_t), 0,
                   (struct sockaddr *)&(sock->conn), &conn_len);
        assert(len == sizeof(cmu_tcp_header_t));
      
        // the packet must be SYN-ACK
        assert(get_flags(&hdr) & SYN_FLAG_MASK);
        assert(get_flags(&hdr) & ACK_FLAG_MASK);

        // upon receiving the first SYN packet, 
        // use the ISN to initialize the receive_buffer
        while (pthread_mutex_lock(&(sock->recv_lock)) != 0) {
        }
        recv_buffer_initialize(sock->recv_buf, get_seq(&hdr));
        pthread_mutex_unlock(&(sock->recv_lock));
        
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

        if (counter2 >= counter2_lim) {
          sendto(sock->socket, packet, plen, 0,
                (struct sockaddr *)&(sock->conn), conn_len);
        } else {
          counter2 += 1;
        }
        
        // printf("client sent ack packet back\n");

        free(packet);

        sock->state = ESTABLISHED;
      }
    }
  }

  sock->initialized = true;
  printf("!-- client finished handshake --!\n");
}

void init_handshake_server(void *in) {
  // printf("server start handshake\n");
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
      // printf("server in LISTEN\n");
      cmu_tcp_header_t hdr;
      ssize_t len = recvfrom(sock->socket, &hdr, sizeof(cmu_tcp_header_t),
                     0, (struct sockaddr *)&(sock->conn), &conn_len);

      assert(len == sizeof(cmu_tcp_header_t));

      // the packet must be SYN
      assert(get_flags(&hdr) == SYN_FLAG_MASK);

      // upon receiving the first SYN packet, 
      // use the ISN to initialize the receive_buffer
      while (pthread_mutex_lock(&(sock->recv_lock)) != 0) {
      }
      recv_buffer_initialize(sock->recv_buf, get_seq(&hdr));
      pthread_mutex_unlock(&(sock->recv_lock));
      sock->initialized = true;

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

      if (counter3 >= counter3_lim) {
        sendto(sock->socket, packet, plen, 0,
          (struct sockaddr *)&(sock->conn), conn_len);
      } else {
        counter3 += 1;
      }

      sock->state = SYN_RCVD;
    } else {
      assert(sock->state == SYN_RCVD);
      // printf("in SYN_RCVD state \n");

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
          if (counter3 >= counter3_lim) {
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

  printf("!-- server finished handshake --!\n");
}

void check_for_data(cmu_socket_t *sock, cmu_read_mode_t flags) {
  cmu_tcp_header_t hdr;
  uint8_t *pkt;
  socklen_t conn_len = sizeof(sock->conn);
  ssize_t len = 0;
  uint32_t plen = 0, buf_size = 0, n = 0;

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
      // Timeout after DEFAULT_TIMEOUT.
      if (poll(&ack_fd, 1, DEFAULT_TIMEOUT) <= 0) {
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
}

// try to send data that was not previously sent before
// do so by calculating 'next_byte_written_index - last_byte_sent_index'
// timeout resend is not handled here
void multiple_send(cmu_socket_t *sock) {
  uint8_t *msg;
  int sockfd = sock->socket;
  size_t conn_len = sizeof(sock->conn);

  uint32_t curr_adv_window = recv_buffer_max_receive(sock->recv_buf);
 
  uint32_t num_unacknowledged = get_unacknowledged_count(sock->send_buf);
  if (num_unacknowledged < sock->window.rcvd_advertised_window) {
    uint32_t num_fresh_data_available = send_buffer_max_new_dump(sock->send_buf);
    uint32_t max_fresh_data_allowed = sock->window.rcvd_advertised_window - num_unacknowledged;
    uint32_t target_send_len = num_fresh_data_available < max_fresh_data_allowed ? num_fresh_data_available : max_fresh_data_allowed;

    // construction the packet to send
    uint16_t payload_len;
    uint16_t src = sock->my_port;
    uint16_t dst = ntohs(sock->conn.sin_port);
    uint32_t seq;
    uint32_t ack = sock->window.next_seq_expected; // this field will not change during the execution of this function, since the lock is held
    uint16_t hlen = sizeof(cmu_tcp_header_t);
    uint16_t plen;
    uint8_t flags = ACK_FLAG_MASK;
    uint16_t adv_window = curr_adv_window;
    uint16_t ext_len = 0;
    uint8_t *ext_data = NULL;
    uint8_t *payload;

    while (target_send_len > 0) {
      payload_len = MIN(target_send_len, (uint32_t)MSS);
      seq = get_last_byte_sent_seqnum(sock->send_buf) + 1;
      plen = hlen + payload_len;
      uint32_t start_index = (sock->send_buf->last_byte_sent_index+1)%(sock->send_buf->capacity);
      payload = malloc(payload_len * sizeof(uint8_t));
      send_buffer_dump(sock->send_buf, start_index, payload_len, payload);
    
      msg = create_packet(src, dst, seq, ack, hlen, plen, flags, adv_window,
                          ext_len, ext_data, payload, payload_len);
      
      sendto(sockfd, msg, plen, 0, (struct sockaddr *)&(sock->conn),
               conn_len);
      
      free(msg);
      target_send_len -= payload_len;        
      free(payload);
    }
  } else {
    // printf("num_unacknowledged : %d\n", num_unacknowledged);
    // printf("sock->window.rcvd_advertised_window : %d\n", sock->window.rcvd_advertised_window);
  }
}

// send_lock already hold by the caller before calling this function
void resend_unacknowledged(cmu_socket_t *sock) {
  uint8_t *msg;
  int sockfd = sock->socket;
  size_t conn_len = sizeof(sock->conn);

  uint16_t payload_len;
  uint16_t src = sock->my_port;
  uint16_t dst = ntohs(sock->conn.sin_port);
  uint32_t seq;
  uint32_t ack = sock->window.next_seq_expected; // this field will not change during the execution of this function, since the lock is held
  uint16_t hlen = sizeof(cmu_tcp_header_t);
  uint16_t plen;
  uint8_t flags = ACK_FLAG_MASK;
  uint16_t adv_window = recv_buffer_max_receive(sock->recv_buf); // not holding recv_lock here; hopefully this will not cause problem
  uint16_t ext_len = 0;
  uint8_t *ext_data = NULL;

  uint32_t num_unacknowledged = get_unacknowledged_count(sock->send_buf);
  assert(num_unacknowledged > 0);
  uint32_t target_send_len = MIN(num_unacknowledged, sock->window.rcvd_advertised_window);
  if (target_send_len == 0) {
    // the advertised window is 0, do zero window probe, send a single byte
    payload_len = 1;
    seq = sock->send_buf->last_byte_acked_seqnum + 1;
    plen = hlen + payload_len;
    uint32_t start_index = (sock->send_buf->last_byte_acked_index+1)%(sock->send_buf->capacity);
    // no need to use send_buffer_dump here because we don't want to update the last_sent_index
    msg = create_packet(src, dst, seq, ack, hlen, plen, flags, adv_window,
                          ext_len, ext_data, sock->send_buf->buffer+start_index, payload_len);

    sendto(sockfd, msg, plen, 0, (struct sockaddr *)&(sock->conn),
               conn_len);

    free(msg);
  } else {
    uint32_t start_index = (sock->send_buf->last_byte_acked_index+1)%(sock->send_buf->capacity);
    uint32_t start_seq = sock->send_buf->last_byte_acked_seqnum + 1;
    uint32_t num_sent = 0;

    while (target_send_len > 0) {
      uint32_t curr_index = (start_index + num_sent)%(sock->send_buf->capacity);
      uint32_t curr_seq = start_seq + num_sent;

      payload_len = MIN(target_send_len, (uint32_t)MSS);
      plen = hlen + payload_len;
      // no need to use send_buffer_dump here because we don't want to update the last_sent_index
      msg = create_packet(src, dst, curr_seq, ack, hlen, plen, flags, adv_window,
                          ext_len, ext_data, sock->send_buf->buffer+curr_index, payload_len);

      sendto(sockfd, msg, plen, 0, (struct sockaddr *)&(sock->conn),
               conn_len);

      free(msg);

      num_sent += payload_len;
      target_send_len -= payload_len;
    }
  }
}

void *begin_backend(void *in) {
  cmu_socket_t *sock = (cmu_socket_t *)in;
  int death;

  // 3-way handshake
  if (sock->type == TCP_INITIATOR) {
    init_handshake_client(in);
  } else {
    init_handshake_server(in);
  }

  while (1) {
    // printf("start the while loop\n");
    while (pthread_mutex_lock(&(sock->death_lock)) != 0) {
    }
    death = sock->dying;
    pthread_mutex_unlock(&(sock->death_lock));

    while (pthread_mutex_lock(&(sock->send_lock)) != 0) {
    }
    uint32_t num_unacknowledged = get_unacknowledged_count(sock->send_buf);
    uint32_t num_fresh = send_buffer_max_new_dump(sock->send_buf);
    pthread_mutex_unlock(&(sock->send_lock));

    if (death && (num_unacknowledged + num_fresh) == 0) {
      break;
    }

    // check if any data arrives, and update sock->window.ack and such.    
    check_for_data(sock, NO_WAIT);

    // check if need to resend due to timeout
    long last_ack_ts;
    while (pthread_mutex_lock(&(sock->send_lock)) != 0) {
    }
    num_unacknowledged = get_unacknowledged_count(sock->send_buf);
    num_fresh = send_buffer_max_new_dump(sock->send_buf);
    last_ack_ts = sock->send_buf->last_byte_acked_ts;
    long curr_ts = get_time_ms();

    if (curr_ts - last_ack_ts > DEFAULT_TIMEOUT && num_unacknowledged > 0) {
      // printf("got here 2\n");
      resend_unacknowledged(sock);
    } else {
      // otherwise, send 'fresh' data on the buffer
      // printf("trying to send");
      multiple_send(sock);
    }
    pthread_mutex_unlock(&(sock->send_lock));

    // TODO : zero window probe

    // alert the application of receiving new data
    uint32_t available_to_read;
    while (pthread_mutex_lock(&(sock->recv_lock)) != 0) {
    }
    available_to_read = recv_buffer_max_read(sock->recv_buf);
    pthread_mutex_unlock(&(sock->recv_lock));

    if (available_to_read > 0) {
      pthread_cond_signal(&(sock->wait_cond));
    }
  }

  pthread_exit(NULL);
  return NULL;
}
