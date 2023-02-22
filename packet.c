/*
    packet.c
*/

#include "packet.h"

void packet_send(struct net_port *port, struct packet *p) {
  char msg[PAYLOAD_MAX + 4];
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
  printf(
      "\033[35mPACKETSEND, src=%d dst=%d type=%d len=%d p-src=%d "
      "p-dst=%d\033[0m\n",
      (int)msg[0], (int)msg[1], (int)msg[2], (int)msg[3], (int)p->src,
      (int)p->dst);
#endif
}

int packet_recv(struct net_port *port, struct packet *p) {
  char msg[PAYLOAD_MAX + 4];
  int bytesRead = 0;

  if (port->type == PIPE) {
    // Parse Packet
    bytesRead = read(port->recv_fd, msg, PAYLOAD_MAX + 4);
  } else if (port->type == SOCKET) {
    bytesRead =
        sock_recv(port->send_fd, msg, PAYLOAD_MAX + 4, port->remoteDomain);
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

    printf(
        "\033[35mPACKETRECV, src=%d dst=%d type=%d len=%d p-src=%d "
        "p-dst=%d\033[0m\n",
        (int)msg[0], (int)msg[1], (int)msg[2], (int)msg[3], (int)p->src,
        (int)p->dst);
#endif
  }
  return (bytesRead);
}
