// net.h

#pragma once

#include "constants.h"

#define MAX_FILENAME_LENGTH 100
#define PIPE_READ 0
#define PIPE_WRITE 1

enum NetNodeType { /* Types of network nodes */
                   HOST,
                   SWITCH
};

enum NetLinkType { /* Types of linkls */
                   PIPE,
                   SOCKET
};

struct Net_node { /* Network node, e.g., host or switch */
  enum NetNodeType type;
  int id;
  struct Net_node *next;
};

/*
 * struct  used to store a link. It is used when the
 * network configuration file is loaded.
 */
struct Net_link {
  enum NetLinkType type;
  int node0;
  int node1;
  char socket_local_domain[MAX_DOMAIN_NAME_LENGTH];
  int socket_local_port;
  char socket_remote_domain[MAX_DOMAIN_NAME_LENGTH];
  int socket_remote_port;
};

struct Net_port {
  enum NetLinkType type;
  int link_node_id;
  int send_fd;
  int recv_fd;
  char localDomain[MAX_DOMAIN_NAME_LENGTH];
  char remoteDomain[MAX_DOMAIN_NAME_LENGTH];
  int remotePort;
  struct Net_port *next;
};

struct Man_port_at_man *net_get_man_ports_at_man_list();

struct Man_port_at_host *net_get_host_port(int host_id);

struct Net_node *net_get_node_list();

struct Net_port *net_get_port_list(int host_id);

int net_init();

/*
 * Creates ports at the manager and ports at the hosts so that
 * the manager can communicate with the hosts.  The list of
 * ports at the manager side is p_m.  The list of ports
 * at the host side is p_h.
 */
void create_man_ports(struct Man_port_at_man **p_m,
                      struct Man_port_at_host **p_h);

void net_close_man_ports_at_hosts();

void net_close_man_ports_at_hosts_except(int host_id);

void net_free_man_ports_at_hosts();

void net_close_man_ports_at_man();

void net_free_man_ports_at_man();

/*
Function to retrieve a linked list of network ports that belong to a specified
node. Takes in an ID of the node of interest as an argument. Returns a pointer
to the head of the resulting linked list.
*/
struct Net_port *net_get_port_list(int host_id);

/*
 * Loads network configuration file and creates data struct File_bufures
 * for nodes and links.  The results are accessible through
 * the private global variables
 */
int load_net_data_file();

/* Creates a data structure for the nodes */
void create_node_list();

/* Creates links, using pipes, then creates a port list for these links */
void create_port_list();
