/*
socket.h
*/

#pragma once

#include "constants.h"

int sock_server_init(const char* localDomain, const int localPort);

int sock_recv(const int sockfd, char* buffer, const int bufferMax,
              const char* remoteDomain);

int sock_send(const char* localDomain, const char* remoteDomain,
              const int remotePort, char* msg, int msgLen);