/*
socket.c
*/

#include "socket.h"

int sock_server_init(const char* localDomain, const int localPort) {
  int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (sock_fd < 0) {
    fprintf(stderr, "Error: sock_server_init: failed to create socket\n");
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
    fprintf(stderr, "Error: sock_server_init: failed to bind sock_fd\n");
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
    close(sock_fd);
    return -1;
  }

  return sock_fd;
}

int sock_recv(const int sockfd, char* buffer, const int bufferMax,
              const char* remoteDomain, const int remotePort) {
  int bytesRead = 0;

  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(sockfd, &read_fds);

  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 6500;

  // Use select to wait for incoming data on the socket
  int select_result = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
  if (select_result < 0) {
    fprintf(stderr, "Error: sock_recv: select failed\n");
    return -1;
  } else if (select_result == 0) {
    // No data available within timeout period
    return 0;
  }

  // Incoming data available, accept the incoming connection and read data
  struct sockaddr_in remote_addr;
  socklen_t addr_len = sizeof(remote_addr);

  int client_fd = accept(sockfd, (struct sockaddr*)&remote_addr, &addr_len);
  if (client_fd < 0) {
    fprintf(stderr, "Error: sock_recv: failed to accept\n");
    return -1;
  }

  // Check if the remote address and port match the desired address and port
  if (strcmp(remoteDomain, inet_ntoa(remote_addr.sin_addr)) != 0 ||
      remotePort != ntohs(remote_addr.sin_port)) {
    close(client_fd);
    return 0;  // Connection not from desired remote address and port
  }

  // Incoming data available, read it into the buffer
  bytesRead = recv(client_fd, buffer, bufferMax, 0);
  if (bytesRead < 0) {
    fprintf(stderr, "Error: sock_recv: bytesRead value < 0\n");
    close(client_fd);
    return -1;
  }

  close(client_fd);
  return bytesRead;
}

int sock_send(const char* remoteDomain, const int remotePort, char* msg,
              int msgLen) {
  int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (sock_fd < 0) {
    fprintf(stderr, "Error: sock_send: failed to create socket\n");
    return -1;
  }

  // Connect to remote server
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = inet_addr(remoteDomain);
  server_addr.sin_port = htons(remotePort);
  int connect_result =
      connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
  if (connect_result < 0) {
    fprintf(stderr, "Error: sock_send: connect_result < 0\n");
    close(sock_fd);
    return -1;
  }

  // Send data to remote server
  int bytesSent = send(sock_fd, msg, msgLen, 0);
  if (bytesSent < 0) {
    fprintf(stderr, "Error: sock_send: bytesSent < 0\n");
    close(sock_fd);
    return -1;
  }

  close(sock_fd);
  return bytesSent;
}