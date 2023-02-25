/*
     packet.h
*/

#pragma once

#include "constants.h"

/* Types of packets */
typedef enum {
  PKT_PING_REQ,
  PKT_PING_RESPONSE,
  PKT_FILE_UPLOAD_REQ,
  PKT_FILE_UPLOAD_START,
  PKT_FILE_UPLOAD_CONTINUE,
  PKT_FILE_UPLOAD_END,
  PKT_FILE_DOWNLOAD_REQ,
  PKT_REQUEST_RESPONSE
} packet_type;

struct Packet {
  char src;
  char dst;
  char type;
  int length;
  char payload[PACKET_PAYLOAD_MAX];
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
