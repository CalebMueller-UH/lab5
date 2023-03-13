/*
    packet.c
*/

#include "packet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "color.h"
#include "host.h"
#include "net.h"
#include "socket.h"

/* Receives a network packet through a pipe or socket by reading a message
 * buffer and then parsing it into a packet. */
int packet_recv(struct Net_port *port, struct Packet *p) {
  char pkt[PACKET_PAYLOAD_MAX + 4];
  int bytesRead = 0;

  if (port->type == PIPE) {
    // Parse Packet
    bytesRead = read(port->recv_fd, pkt, PACKET_PAYLOAD_MAX + 4);
  } else if (port->type == SOCKET) {
    bytesRead = sock_recv(port->send_fd, pkt, PACKET_PAYLOAD_MAX + 4,
                          port->remoteDomain);
  }
  if (bytesRead > 0) {
#ifdef DEBUG
    // printPacket(p);
#endif
    p->src = (char)pkt[0];
    p->dst = (char)pkt[1];
    p->type = (char)pkt[2];
    p->length = (int)pkt[3];
    for (int i = 0; i < p->length; i++) {
      p->payload[i] = pkt[i + 4];
    }
  }
  return (bytesRead);
}

/* Sends a network packet through a pipe or socket by parsing the packet into a
 * message buffer and then sending it. */
void packet_send(struct Net_port *port, struct Packet *p) {
  char pkt[PACKET_PAYLOAD_MAX + 4];
  int bytesSent = -1;

  // Parse Packet
  pkt[0] = (char)p->src;
  pkt[1] = (char)p->dst;
  pkt[2] = (char)p->type;
  pkt[3] = (char)p->length;
  for (int i = 0; i < p->length; i++) {
    pkt[i + 4] = p->payload[i];
  }

  if (port->type == PIPE) {
    bytesSent = write(port->send_fd, pkt, p->length + 4);

  } else if (port->type == SOCKET) {
    bytesSent = sock_send(port->localDomain, port->remoteDomain,
                          port->remotePort, pkt, p->length + 4);
  }

#ifdef DEBUG
  // printPacket(p);
#endif
}

/* createPacket:
 * creates a new packet with the given parameters and payload, or with a
 * specified length if no payload is provided */
struct Packet *createPacket(int src, int dst, int type, int length,
                            char *payload) {
  struct Packet *p = createEmptyPacket();
  p->src = src;
  p->dst = dst;
  p->type = type;
  if (payload != NULL) {
    strncpy(p->payload, payload, PACKET_PAYLOAD_MAX);
    p->length = strlen(payload);
  }
  return p;
}

/* Allocates memory for a new packet and initializes it to zeros. */
struct Packet *createEmptyPacket() {
  struct Packet *p = (struct Packet *)malloc(sizeof(struct Packet));
  memset(&p->dst, 0, sizeof(p->dst));
  memset(&p->src, 0, sizeof(p->src));
  memset(&p->type, 0, sizeof(p->type));
  memset(&p->length, 0, sizeof(p->length));
  memset(&p->payload, 0, sizeof(p->payload));
  return p;
}

void packet_delete(struct Packet *p) {
  if (p == NULL) {
    fprintf(stderr, "packet_delete called on NULL packet\n");
  } else {
    free(p);
    p = NULL;
  }
}

/* Returns a string representation of the packet type. */
char *get_packet_type_literal(int pktType) {
  switch (pktType) {
    case PKT_PING_REQ:
      return "PKT_PING_REQ";
    case PKT_PING_RESPONSE:
      return "PKT_PING_RESPONSE";
    case PKT_UPLOAD_REQ:
      return "PKT_UPLOAD_REQ";
    case PKT_UPLOAD_RESPONSE:
      return "PKT_UPLOAD_RESPONSE";
    case PKT_DOWNLOAD_REQ:
      return "PKT_DOWNLOAD_REQ";
    case PKT_DOWNLOAD_RESPONSE:
      return "PKT_DOWNLOAD_RESPONSE";
    case PKT_UPLOAD:
      return "PKT_UPLOAD";
    case PKT_UPLOAD_END:
      return "PKT_UPLOAD_END";
  }
}

/* Prints the contents of a packet with its source, destination, type,
 * length, and payload. */
void printPacket(struct Packet *p) {
  colorPrint(ORANGE, "src:%d dst:%d type: %s len:%d payload:%s\n", p->src,
             p->dst, get_packet_type_literal(p->type), p->length, p->payload);
}
