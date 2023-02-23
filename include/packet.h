/*
     packet.h
*/

#pragma once

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "color.h"
#include "host.h"
#include "main.h"
#include "net.h"
#include "socket.h"

#define PAYLOAD_MAX 100

struct packet { /* struct for a packet */
  char src;
  char dst;
  char type;
  int length;
  char payload[PAYLOAD_MAX];
};

/* Types of packets */
#define PKT_PING_REQ 0
#define PKT_PING_REPLY 1
#define PKT_FILE_UPLOAD_START 2
#define PKT_FILE_UPLOAD_CONTINUE 3
#define PKT_FILE_UPLOAD_END 4
#define PKT_FILE_DOWNLOAD_REQUEST 5
#define PKT_REQUEST_RESPONSE 6

// receive packet on port
int packet_recv(struct net_port *port, struct packet *p);

// send packet on port
void packet_send(struct net_port *port, struct packet *p);

char *get_packet_type_literal(int pktType);

void printPacket(struct packet *p);
