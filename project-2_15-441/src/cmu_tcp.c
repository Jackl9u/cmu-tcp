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
 * This file implements the high-level API for CMU-TCP sockets.
 */

#include "cmu_tcp.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>

#include "backend.h"
#include "recv_buffer.h"
#include "send_buffer.h"

uint32_t DEFAULT_BUFF_SIZE = 1024;

int cmu_socket(cmu_socket_t *sock, const cmu_socket_type_t socket_type,
               const int port, const char *server_ip) {
  int sockfd, optval;
  socklen_t len;
  struct sockaddr_in conn, my_addr;
  len = sizeof(my_addr);

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0) {
    perror("ERROR opening socket");
    return EXIT_ERROR;
  }
  sock->socket = sockfd;
  sock->type = socket_type;

  sock->dying = 0;
  pthread_mutex_init(&(sock->death_lock), NULL);

  if (pthread_cond_init(&sock->wait_cond, NULL) != 0) {
    perror("ERROR condition variable not set\n");
    return EXIT_ERROR;
  }

  srand(time(NULL));
  sock->window.last_ack_received = (uint32_t)rand();    // randomly initialized to be used as ISN
  sock->window.next_seq_expected = 0;                   // NOT USED; set by the Sequence number of the SYN packet of the other end
  sock->window.rcvd_advertised_window = CP1_WINDOW_SIZE;

  sock->recv_buf = recv_buffer_create(DEFAULT_BUFF_SIZE);
  // receive buffer needs to be initialize during the handshake SYN
  // recv_buffer_initialize( .. );
  pthread_mutex_init(&(sock->recv_lock), NULL);

  sock->send_buf = send_buffer_create(DEFAULT_BUFF_SIZE);
  send_buffer_initialize(sock->send_buf, sock->window.last_ack_received);
  pthread_mutex_init(&(sock->send_lock), NULL);

  sock->state = CLOSED;
  sock->initialized = false;
  sock->last_send_ms = 0;

  switch (socket_type) {
    case TCP_INITIATOR:
      if (server_ip == NULL) {
        perror("ERROR server_ip NULL");
        return EXIT_ERROR;
      }
      memset(&conn, 0, sizeof(conn));
      conn.sin_family = AF_INET;
      conn.sin_addr.s_addr = inet_addr(server_ip);
      conn.sin_port = htons(port);
      sock->conn = conn;

      my_addr.sin_family = AF_INET;
      my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
      my_addr.sin_port = 0;
      if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(my_addr)) < 0) {
        perror("ERROR on binding");
        return EXIT_ERROR;
      }

      break;

    case TCP_LISTENER:
      memset(&conn, 0, sizeof(conn));
      conn.sin_family = AF_INET;
      conn.sin_addr.s_addr = htonl(INADDR_ANY);
      conn.sin_port = htons((uint16_t)port);

      optval = 1;
      setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval,
                 sizeof(int));
      if (bind(sockfd, (struct sockaddr *)&conn, sizeof(conn)) < 0) {
        perror("ERROR on binding");
        return EXIT_ERROR;
      }
      sock->conn = conn;
      break;

    default:
      perror("Unknown Flag");
      return EXIT_ERROR;
  }
  getsockname(sockfd, (struct sockaddr *)&my_addr, &len);
  sock->my_port = ntohs(my_addr.sin_port);

  pthread_create(&(sock->thread_id), NULL, begin_backend, (void *)sock);
  return EXIT_SUCCESS;
}

int cmu_close(cmu_socket_t *sock) {
  while (!sock->initialized) {}
  
  while (pthread_mutex_lock(&(sock->death_lock)) != 0) {
  }
  sock->dying = 1;
  pthread_mutex_unlock(&(sock->death_lock));

  pthread_join(sock->thread_id, NULL);

  // after the pthread_join(sock->thread_id, NULL);
  // there's only one thread accessing recv_buf and send_buf
  // so no lock needed
  if (sock != NULL) {
    recv_buffer_clean(sock->recv_buf);
    send_buffer_clean(sock->send_buf);
  } else {
    perror("ERROR null socket\n");
    return EXIT_ERROR;
  }
  return close(sock->socket);
}

int cmu_read(cmu_socket_t *sock, void *buf, int length, cmu_read_mode_t flags) {
  while (!sock->initialized) {}
  
  printf("begin to read\n");

  int read_len = 0;

  if (length < 0) {
    perror("ERROR negative length");
    return EXIT_ERROR;
  }

  // while (pthread_mutex_lock(&(sock->recv_lock)) != 0) {
  // }

  switch (flags) {
    case NO_FLAG:
      while (recv_buffer_max_read(sock->recv_buf) == 0) {
        printf("stuck in recv_buffer_max_read(sock->recv_buf) == 0 \n");
        pthread_cond_wait(&(sock->wait_cond), &(sock->recv_lock));
      }
    // Fall through.
    case NO_WAIT:
      if (recv_buffer_max_read(sock->recv_buf) > 0) {
        printf("recv_buffer_max_read(sock->recv_buf) : %d\n", recv_buffer_max_read(sock->recv_buf));
        if (recv_buffer_max_read(sock->recv_buf) > (uint32_t)length) {
          read_len = length;
        } else {
          read_len = recv_buffer_max_read(sock->recv_buf);
        }
        recv_buffer_read(sock->recv_buf, buf, read_len);
      }
      break;
    default:
      perror("ERROR Unknown flag.\n");
      read_len = EXIT_ERROR;
  }
  // pthread_mutex_unlock(&(sock->recv_lock));
  return read_len;
}

int cmu_write(cmu_socket_t *sock, const void *buf, int length) {
  while (!sock->initialized) {}
  
  printf("begin to write\n");

  uint32_t written = 0;

  while (length > 0) {
    while (pthread_mutex_lock(&(sock->send_lock)) != 0) {
    }
    
    uint32_t write_len;
    if ((uint32_t)length > send_buffer_max_write(sock->send_buf)) {
      write_len = send_buffer_max_write(sock->send_buf);
    } else {
      write_len = length;
    }
    if (write_len > 0) {
      send_buffer_write(sock->send_buf, (uint8_t*)buf+written, write_len);
      written += write_len;
      length -= write_len;
    }

    pthread_mutex_unlock(&(sock->send_lock));
  }

  return EXIT_SUCCESS;
}
