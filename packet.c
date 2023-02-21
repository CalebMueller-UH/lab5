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
    if ((connect(port->send_fd, (struct sockaddr *)&port->remoteaddr,
                 sizeof(port->remoteaddr))) < 0) {
      fprintf(stderr,
              "\033[35mError: packet_send: could not connect send_fd to "
              "remotesockaddr\033[0m\n");
    } else {
      bytesSent = send(port->send_fd, msg, p->length + 4, 0);
    }
  }
  if (bytesSent < 0) {
    fprintf(stderr,
            "\033[35mError: packet_send: failed to send packet\033[0m\n");
  } else {
#ifdef DEBUG
    printf(
        "\033[35mPACKETSEND, src=%d dst=%d type=%d len=%d p-src=%d "
        "p-dst=%d\033[0m\n",
        (int)msg[0], (int)msg[1], (int)msg[2], (int)msg[3], (int)p->src,
        (int)p->dst);
#endif
  }
}

int packet_recv(struct net_port *port, struct packet *p) {
  char msg[PAYLOAD_MAX + 4];
  int bytesRead;

  // Parse Packet

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
          "\033[35mPACKETRECV, src=%d dst=%d type=%d len=%d p-src=%d "
          "p-dst=%d\033[0m\n",
          (int)msg[0], (int)msg[1], (int)msg[2], (int)msg[3], (int)p->src,
          (int)p->dst);
#endif
    }
  }
  return (bytesRead);
}
