/*
socket.h
*/

#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_DOMAIN_NAME_LENGTH 100

int sock_server_init(const char* localDomain, const int localPort);

int sock_recv(const int sockfd, char* buffer, const int bufferMax,
              const char* remoteDomain);

int sock_send(const char* localDomain, const char* remoteDomain,
              const int remotePort, char* msg, int msgLen);