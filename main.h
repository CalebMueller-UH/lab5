// main.h

#pragma once

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "socket.h"
#include "host.h"
#include "man.h"
#include "net.h"
#include "packet.h"
#include "switch.h"

#define BCAST_ADDR 100
#define STRING_MAX 100
#define NAME_LENGTH 100
#define MAX_DOMAIN_NAME_LENGTH 100


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
  char localDomain[MAX_DOMAIN_NAME_LENGTH];
  char remoteDomain[MAX_DOMAIN_NAME_LENGTH];
  int remotePort;
  struct net_port *next;
};
