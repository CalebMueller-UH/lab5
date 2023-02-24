/*
     packet.h
*/

#pragma once

#include "common.h"

/* Types of packets */
#define PKT_PING_REQ 0
#define PKT_PING_REPLY 1
#define PKT_FILE_UPLOAD_REQ 2
#define PKT_FILE_UPLOAD_START 3
#define PKT_FILE_UPLOAD_CONTINUE 4
#define PKT_FILE_UPLOAD_END 5
#define PKT_FILE_DOWNLOAD_REQ 6
#define PKT_REQUEST_RESPONSE 7

struct Packet { /* struct  for a packet */
  char src;
  char dst;
  char type;
  int length;
  char payload[PKT_PAYLOAD_MAX];
};

// Forward declaration, defined in net.h
struct Net_port;

/* Sends a network packet through a pipe or socket by parsing the packet into a
 * message buffer and then sending it. */
int packet_recv(struct Net_port *port, struct Packet *p);

/* Receives a network packet through a pipe or socket by reading a message
 * buffer and then parsing it into a packet. */
void packet_send(struct Net_port *port, struct Packet *p);

/* Allocates memory for a new packet and initializes it to zeros. */
struct Packet *createBlankPacket();

/* Returns a string representation of the packet type. */
char *get_packet_type_literal(int pktType);

/* Prints the contents of a packet with its source, destination, type, length,
 * and payload. */
void printPacket(struct Packet *p);
