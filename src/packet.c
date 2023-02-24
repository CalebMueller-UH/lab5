/*
    packet.c
*/

#include "packet.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "color.h"
#include "host.h"
#include "main.h"
#include "net.h"
#include "socket.h"

void packet_send(struct net_port *port, struct packet *p) {
  char msg[PKT_PAYLOAD_MAX + 4];
  int bytesSent = -1;

  // Parse Packet
  msg[0] = (char)p->src;
  msg[1] = (char)p->dst;
  msg[2] = (char)p->type;
  msg[3] = (char)p->length;
  for (int i = 0; i < p->length; i++) {
    msg[i + 4] = p->payload[i];
  }

  if (port->type == PIPE) {
    bytesSent = write(port->send_fd, msg, p->length + 4);

  } else if (port->type == SOCKET) {
    bytesSent = sock_send(port->localDomain, port->remoteDomain,
                          port->remotePort, msg, p->length + 4);
  }

#ifdef DEBUG
  printPacket(p);
#endif
}

int packet_recv(struct net_port *port, struct packet *p) {
  char msg[PKT_PAYLOAD_MAX + 4];
  int bytesRead = 0;

  if (port->type == PIPE) {
    // Parse Packet
    bytesRead = read(port->recv_fd, msg, PKT_PAYLOAD_MAX + 4);
  } else if (port->type == SOCKET) {
    bytesRead =
        sock_recv(port->send_fd, msg, PKT_PAYLOAD_MAX + 4, port->remoteDomain);
  }
  if (bytesRead > 0) {
#ifdef DEBUG
    p->src = (char)msg[0];
    p->dst = (char)msg[1];
    p->type = (char)msg[2];
    p->length = (int)msg[3];
    for (int i = 0; i < p->length; i++) {
      p->payload[i] = msg[i + 4];
    }

    printPacket(p);
#endif
  }
  return (bytesRead);
}

struct packet *createBlankPacket() {
  struct packet *p = (struct packet *)malloc(sizeof(struct packet));
  memset(&p->dst, 0, sizeof(p->dst));
  memset(&p->src, 0, sizeof(p->src));
  memset(&p->type, 0, sizeof(p->type));
  memset(&p->length, 0, sizeof(p->length));
  memset(&p->payload, 0, sizeof(p->payload));
  return p;
}

char *get_packet_type_literal(int pktType) {
  switch (pktType) {
    case PKT_PING_REQ:
      return "PKT_PING_REQ ";
    case PKT_PING_REPLY:
      return "PKT_PING_REPLY ";
    case PKT_FILE_UPLOAD_START:
      return "PKT_FILE_UPLOAD_START ";
    case PKT_FILE_UPLOAD_CONTINUE:
      return "PKT_FILE_UPLOAD_CONTINUE ";
    case PKT_FILE_UPLOAD_END:
      return "PKT_FILE_UPLOAD_END ";
    case PKT_FILE_DOWNLOAD_REQUEST:
      return "PKT_FILE_DOWNLOAD_REQUEST ";
    case PKT_REQUEST_RESPONSE:
      return "PKT_REQUEST_RESPONSE";
    default:
      return "Unknown Packet Type";
  }
}

void printPacket(struct packet *p) {
  colorPrint(
      ORANGE, "Packet contents: src:%d dst:%d type:%s len:%d payload:%s\n",
      p->src, p->dst, get_packet_type_literal(p->type), p->length, p->payload);
}
