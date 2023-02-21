// main.h

#pragma once

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "host.h"
#include "man.h"
#include "net.h"
#include "switch.h"

#define BCAST_ADDR 100
#define PAYLOAD_MAX 100
#define STRING_MAX 100
#define NAME_LENGTH 100

enum NetNodeType { /* Types of network nodes */
                   HOST,
                   SWITCH
};

enum NetLinkType { /* Types of linkls */
                   PIPE,
                   SOCKET
};

struct net_node { /* Network node, e.g., host or switch */
  enum NetNodeType type;
  int id;
  struct net_node *next;
};

struct net_port {
  enum NetLinkType type;
  int link_node_id;
  int send_fd;
  int recv_fd;
  struct net_port *next;
};

struct packet { /* struct for a packet */
  char src;
  char dst;
  char type;
  int length;
  char payload[PAYLOAD_MAX];
};

/* Types of packets */

#define PKT_PING_REQ 0
#define PKT_PING_REPLY 1
#define PKT_FILE_UPLOAD_START 2
#define PKT_FILE_UPLOAD_END 3
