/*
    packet.c
*/

#include "packet.h"

void packet_send(struct net_port *port, struct packet *p) {
  char msg[PAYLOAD_MAX + 7]; 
  int bytesSent = -1;
  int bytesToSend = 0;
  
  // Add Packet Header: 
  // Casts msg to a pointer to a short type.  
  // Sets the first 2 bytes of the msg buffer to the total_payload.
  // Sets the next 2 bytes of the msg buffer to the payloag_offset. 
  *((short*)(msg)) = (short)p->total_payload_length; 
  *((short*)(msg + 2)) = (short)p->payload_offset;
  msg[4] = (char)p->src;
  msg[5] = (char)p->dst;
  msg[6] = (char)p->type;
  
  // Copy payload data
  int payload_remaining = p->total_payload_length - p->payload_offset;
  bytesToSend = payload_remaining > MAX_PAYLOAD_LENGTH ? MAX_PAYLOAD_LENGTH : payload_remaining;
  memcpy(msg + 7, p->payload + p->payload_offset, bytesToSend);

  // Send Packet
  if (port->type == PIPE) {
    bytesSent = write(port->send_fd, msg, bytesToSend + 7);
  } else if (port->type == SOCKET) {
    bytesSent = sock_send(port->localDomain, port->remoteDomain,
                          port->remotePort, msg, bytesToSend + 7);
  }

#ifdef DEBUG
  printf(
      "\033[35mPACKETSEND, src=%d dst=%d type=%d len=%d p-src=%d "
      "p-dst=%d\033[0m\n",
      (int)msg[4], (int)msg[5], (int)msg[6], (int)bytesToSend, (int)p->src,
      (int)p->dst);
#endif

  // Update the payload offset 
  p->payload_offset += bytesToSend;
}

int packet_recv(struct net_port *port, struct packet *p) {
  char msg[MAX_PAYLOAD_LENGTH + 7]; 
  int bytesRead = 0;

  if (port->type == PIPE) {
    bytesRead = read(port->recv_fd, msg, MAX_PAYLOAD_LENGTH + 7);
  } else if (port->type == SOCKET) {
    bytesRead = sock_recv(port->send_fd, msg, MAX_PAYLOAD_LENGTH + 7, port->remoteDomain);
  }

  if (bytesRead > 0) {
#ifdef DEBUG
    p->src = (char)msg[4];
    p->dst = (char)msg[5];
    p->type = (char)msg[6];
    p->length = bytesRead - 7;
    memcpy(p->payload, msg + 7, p->length);

    printf(
        "\033[35mPACKETRECV, src=%d dst=%d type=%d len=%d p-src=%d "
        "p-dst=%d\033[0m\n",
        (int)msg[4], (int)msg[5], (int)msg[6], (int)bytesRead - 7, (int)p->src,
        (int)p->dst);
#endif
  }
  return (bytesRead);
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
