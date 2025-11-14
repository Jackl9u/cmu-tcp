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
 * This file implements a simple CMU-TCP client. Its purpose is to provide
 * simple test cases and demonstrate how the sockets will be used.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "cmu_tcp.h"

void functionality(cmu_socket_t *sock) {
  uint8_t buf[9898];
  int n;
  FILE *fp;

  cmu_write(sock, "hi there", 8);
  cmu_write(sock, " https://www.youtube.com/watch?v=dQw4w9WgXcQ", 44);
  cmu_write(sock, " https://www.youtube.com/watch?v=Yb6dZ1IFlKc", 44);
  cmu_write(sock, " https://www.youtube.com/watch?v=xvFZjo5PgG0", 44);
  cmu_write(sock, " https://www.youtube.com/watch?v=8ybW48rKBME", 44);
  cmu_write(sock, " https://www.youtube.com/watch?v=xfr64zoBTAQ", 45);
  
  printf("read1\n");
  n = cmu_read(sock, buf, 200, NO_FLAG);
  printf("N1: %d\n", n);
  printf("R1: %s\n", buf);

  cmu_write(sock, "hi there", 9);
  
  printf("read2\n");
  n = cmu_read(sock, buf, 200, NO_FLAG);
  printf("N2: %d\n", n);
  printf("R2: %s\n", buf);

  printf("read3\n");
  n = cmu_read(sock, buf, 200, NO_WAIT);
  printf("N3: %d\n", n);
  printf("R3: %s\n", buf);

  fp = fopen("/vagrant/project-2_15-441/src/cmu_tcp.c", "rb");
  n = 1;
  while (n > 0) {
    n = fread(buf, 1, 2000, fp);
    if (n > 0) {
      cmu_write(sock, buf, n);
    }
  }
  printf("functionality ends\n");
}

int main() {
  int portno;
  char *serverip;
  char *serverport;
  cmu_socket_t socket;

  serverip = getenv("server15441");
  if (!serverip) {
    serverip = "10.0.1.1";
  }

  serverport = getenv("serverport15441");
  if (!serverport) {
    serverport = "15441";
  }
  portno = (uint16_t)atoi(serverport);

  if (cmu_socket(&socket, TCP_INITIATOR, portno, serverip) < 0) {
    exit(EXIT_FAILURE);
  }

  functionality(&socket);

  if (cmu_close(&socket) < 0) {
    exit(EXIT_FAILURE);
  }

  return EXIT_SUCCESS;
}
