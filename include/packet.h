/*
     packet.h
*/

#pragma once

#include "common.h"

/* Types of packets */
#define PKT_PING_REQ 0
#define PKT_PING_REPLY 1
#define PKT_FILE_UPLOAD_START 2
#define PKT_FILE_UPLOAD_CONTINUE 3
#define PKT_FILE_UPLOAD_END 4
#define PKT_FILE_DOWNLOAD_REQUEST 5
#define PKT_REQUEST_RESPONSE 6

struct packet { /* struct for a packet */
  char src;
  char dst;
  char type;
  int length;
  char payload[PKT_PAYLOAD_MAX];
};

// Forward declaration, defined in net.h
struct net_port;

// receive packet on port
int packet_recv(struct net_port *port, struct packet *p);

// send packet on port
void packet_send(struct net_port *port, struct packet *p);

struct packet *createBlankPacket();

char *get_packet_type_literal(int pktType);

void printPacket(struct packet *p);
