/*
    packet.c
*/

#include "packet.h"

void packet_send(struct net_port *port, struct packet *p) {
  char msg[PAYLOAD_MAX + 4];
  int i;

  if (port->type == PIPE) {
    msg[0] = (char)p->src;
    msg[1] = (char)p->dst;
    msg[2] = (char)p->type;
    msg[3] = (char)p->length;
    for (i = 0; i < p->length; i++) {
      msg[i + 4] = p->payload[i];
    }
    write(port->send_fd, msg, p->length + 4);
#ifdef DEBUG
    printf(
        "\x1b[35mPACKETSEND, src=%d dst=%d type=%d len=%d p-src=%d "
        "p-dst=%d\x1b[0m\n",
        (int)msg[0], (int)msg[1], (int)msg[2], (int)msg[3], (int)p->src,
        (int)p->dst);
#endif
  }

  return;
}

int packet_recv(struct net_port *port, struct packet *p) {
  char msg[PAYLOAD_MAX + 4];
  int bytesRead;

  if (port->type == PIPE) {
    bytesRead = read(port->recv_fd, msg, PAYLOAD_MAX + 4);
    if (bytesRead > 0) {
      p->src = (char)msg[0];
      p->dst = (char)msg[1];
      p->type = (char)msg[2];
      p->length = (int)msg[3];
      for (int i = 0; i < p->length; i++) {
        p->payload[i] = msg[i + 4];
      }
#ifdef DEBUG
      printf(
          "\x1b[35mPACKETRECV, src=%d dst=%d type=%d len=%d p-src=%d "
          "p-dst=%d\x1b[0m\n",
          (int)msg[0], (int)msg[1], (int)msg[2], (int)msg[3], (int)p->src,
          (int)p->dst);
#endif
    }
  }
  return (bytesRead);
}
