/*
socket.c
*/

#include "socket.h"

int sock_server_init(const char* localDomain, const int localPort) {
  int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (sock_fd < 0) {
    fprintf(stderr, "\nError: sock_server_init: failed to create socket\n");
    return -1;
  }

  // bind to local domain and port
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = inet_addr(localDomain);
  server_addr.sin_port = htons(localPort);
  int bind_result =
      bind(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
  if (bind_result < 0) {
    fprintf(stderr, "\nError: sock_server_init: failed to bind %s:%d\n",
            localDomain, localPort);
    perror("\t");
    close(sock_fd);
    return -1;
  }

  // set socket to listen for incoming connections
  int listen_result = listen(sock_fd, SOMAXCONN);
  if (listen_result < 0) {
    fprintf(
        stderr,
        "Error: sock_server_init: failed set set socket to listen on %s:%d\n",
        localDomain, localPort);
    perror("\t");
    close(sock_fd);
    return -1;
  }

  return sock_fd;
}

int sock_recv(const int sockfd, char* buffer, const int bufferMax,
              const char* remoteDomain) {
  struct sockaddr_in remote_addr;
  socklen_t addr_len = sizeof(remote_addr);
  int bytesRead = 0;

  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(sockfd, &read_fds);

  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 6500;

  while (1) {
    // Use select to wait for incoming data on the socket
    int select_result = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
    if (select_result < 0) {
      fprintf(stderr, "\nError: sock_recv: select failed\n");
      perror("\t");
      return -1;
    } else if (select_result == 0) {
      // No data available within timeout period
      return 0;
    }

    // Incoming data available, accept the incoming connection and read data
    int client_fd = accept(sockfd, (struct sockaddr*)&remote_addr, &addr_len);
    if (client_fd < 0) {
      fprintf(stderr, "\nError: sock_recv: failed to accept connection\n");
      perror("\t");
      return -1;
    } else {
#ifdef DEBUG
      printf("\x1b[1;30mSOCK_RECV: accepted connection from %s:%d\x1b[0m\n",
             inet_ntoa(remote_addr.sin_addr), ntohs(remote_addr.sin_port));
#endif
    }

    // Check if the remote address and port match the desired address
    if (strcmp(remoteDomain, inet_ntoa(remote_addr.sin_addr))) {
      printf("Connection not from desired remote address\n");
      close(client_fd);
      continue;  // Connection not from desired remote address and port,
                 // continue waiting
    }

    // Incoming data available, read packet header
    char header[7];
    bytesRead = recv(client_fd, header, 7, 0);
    if (bytesRead < 0) {
      fprintf(stderr, "\nError: sock_recv: failed to read packet header\n");
      perror("\t");
      close(client_fd);
      return -1;
    }

    // Extract payload size and offset from packet header
    short total_payload = *((short*)(header));
    short payload_offset = *((short*)(header + 2));

    // Read payload data
    int payload_remaining = total_payload - payload_offset;
    while (payload_remaining > 0) {
      int bytesToRead = min(payload_remaining, PAYLOAD_MAX);
      bytesRead = recv(client_fd, buffer + (total_payload - payload_remaining), bytesToRead, 0);
      if (bytesRead < 0) {
        fprintf(stderr, "\nError: sock_recv: failed to read payload data\n");
        perror("\t");
        close(client_fd);
        return -1;
      }
      payload_remaining -= bytesRead;
    }
    
    close(client_fd);
    break;  // Successfully read data from the desired remote address and port,
            // exit loop
  }

#ifdef DEBUG
  printf("\x1b[1;30mSOCK_RECV: received %d bytes from %s:%d\x1b[0m\n",
         bytesRead, inet_ntoa(remote_addr.sin_addr),
         ntohs(remote_addr.sin_port));
#endif

  return bytesRead;
}


int sock_send(const char* localDomain, const char* remoteDomain,
              const int remotePort, char* msg, int msgLen) {
  int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (sock_fd < 0) {
    fprintf(stderr, "\nError: sock_send: failed to create socket\n");
    perror("\t");
    return -1;
  }

  // Bind to local port
  struct sockaddr_in local_addr;
  local_addr.sin_family = AF_INET;
  local_addr.sin_addr.s_addr = inet_addr(localDomain);
  local_addr.sin_port = htons(0);
  int bind_result =
      bind(sock_fd, (struct sockaddr*)&local_addr, sizeof(local_addr));
  if (bind_result < 0) {
    fprintf(stderr, "\nError: sock_send: failed to bind local port\n");
    perror("\t");
    close(sock_fd);
    return -1;
  }

  // Get the local port assigned by the OS
  struct sockaddr_in local_addr_assigned;
  socklen_t addr_len = sizeof(local_addr_assigned);
  getsockname(sock_fd, (struct sockaddr*)&local_addr_assigned, &addr_len);

  // Connect to remote server
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = inet_addr(remoteDomain);
  server_addr.sin_port = htons(remotePort);
  int connect_result =
      connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
  if (connect_result < 0) {
    fprintf(stderr, "\nError: sock_send: connect_result < 0\n");
    perror("\t");
    close(sock_fd);
    return -1;
  }

 // Send data to remote server in packets
  int bytesSent = 0;
  int payload_offset = 0;
  while (payload_offset < msgLen) {
    struct packet p;
    p.src = 0;
    p.dst = 0;
    p.type = PKT_FILE_UPLOAD_CONTINUE;
    p.payload_offset = payload_offset;
    p.total_payload = msgLen;
    int bytesToSend = min(msgLen - payload_offset, PAYLOAD_MAX);
    memcpy(p.payload, msg + payload_offset, bytesToSend);

  bytesSent += bytesToSend;
  payload_offset += bytesToSend;
}

  // Send final packet with payload length < PAYLOAD_MAX
  if (payload_offset < msgLen) {
    struct packet p;
    p.src = 0;
    p.dst = 0;
    p.type = PKT_FILE_UPLOAD_END;
    p.payload_offset = payload_offset;
    p.total_payload = msgLen;
    memcpy(p.payload, msg + payload_offset, msgLen - payload_offset);

    bytesSent += msgLen - payload_offset;
  }

  close(sock_fd);

#ifdef DEBUG
  printf(
      "\x1b[1;30mSOCK_SEND: sent %d bytes to %s:%d from local port "
      "%d\x1b[0m\n",
      bytesSent, remoteDomain, remotePort, ntohs(local_addr_assigned.sin_port));
#endif

  return bytesSent;
}