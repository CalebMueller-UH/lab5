/*
     packet.h
*/

#pragma once

#include "constants.h"

/* Types of packets */
typedef enum {
  PKT_PING_REQ,
  PKT_UPLOAD_REQ,
  PKT_DOWNLOAD_REQ,
  PKT_DNS_REQ,
  PKT_PING_RESPONSE,
  PKT_UPLOAD_RESPONSE,
  PKT_DOWNLOAD_RESPONSE,
  PKT_UPLOAD,
  PKT_UPLOAD_END,
  PKT_INVALID_TYPE,
  PKT_DNS_QUERY,
  PKT_DNS_QUERY_RESPONSE,
  PKT_DNS_REGISTRATION,
  PKT_DNS_RESPONSE
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

int packet_recv(struct Net_port *port, struct Packet *p);

void packet_send(struct Net_port *port, struct Packet *p);

struct Packet *createPacket(int src, int dst, int type, int length,
                            char *payload);

struct Packet *createEmptyPacket();

void packet_delete(struct Packet *p);

char *get_packet_type_literal(int pktType);

void printPacket(struct Packet *p);
