// net.c

/*
 * Here is where pipes and sockets are created.
 * Note that they are "nonblocking".  This means that
 * whenever a read/write (or send/recv) call is made,
 * the called function will do its best to fulfill
 * the request and quickly return to the caller.
 *
 * Note that if a pipe or socket is "blocking" then
 * when a call to read/write (or send/recv) will be blocked
 * until the read/write is completely fulfilled.
 */

#include "net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#define _GNU_SOURCE
#include <fcntl.h>

#include "color.h"
#include "host.h"
#include "main.h"
#include "manager.h"
#include "packet.h"
#include "socket.h"

#define PIPE_WRITE 1
#define PIPE_READ 0

///////////////////////////////////////////////////////////////////////////////
////////////////////// PRIVATE GLOBAL VARIABLES FOR NET.C /////////////////////
bool g_initialized = false; /* Network initialized? */
/* The network is initialized only once */

/*
 * net_node_list[] and net_node_num have link information from
 * the network configuration file.
 * g_node_list is a linked list version of net_node_list[]
 */
static struct Net_node *net_node_list;
static int net_node_num;
static struct Net_node *g_node_list = NULL;

/*
 * net_link_list[] and net_link_num have link information from
 * the network configuration file
 */
static struct Net_link *net_link_list;
static int net_link_num;

/*
 * Global private variables about ports of network node links
 * and ports of links to the manager
 */
static struct Net_port *g_port_list = NULL;
static struct Man_port_at_man *g_man_man_port_list = NULL;
static struct Man_port_at_host *g_man_host_port_list = NULL;

////////////////////// PRIVATE GLOBAL VARIABLES FOR NET.C /////////////////////
///////////////////////////////////////////////////////////////////////////////

/*
 * Get the list of nodes
 */
struct Net_node *net_get_node_list();

/*
Function to retrieve a linked list of network ports that belong to a specified
node. Takes in an ID of the node of interest as an argument. Returns a pointer
to the head of the resulting linked list.
*/
struct Net_port *net_get_port_list(int id_of_interest) {
  struct Net_port **curr;
  struct Net_port *result;
  struct Net_port *temp;
  result = NULL;
  curr = &g_port_list;
  while (*curr != NULL) {
    if ((*curr)->link_node_id == id_of_interest) {
      temp = *curr;
      *curr = (*curr)->next;
      temp->next = result;
      result = temp;
    } else {
      curr = &((*curr)->next);
    }
  }
  return result;
}

/* Return the linked list of nodes */
struct Net_node *net_get_node_list() { return g_node_list; }

/* Return linked list of ports used by the manager to connect to hosts */
struct Man_port_at_man *net_get_man_ports_at_man_list() {
  return (g_man_man_port_list);
}

/* Return the port used by host to link with other nodes */
struct Man_port_at_host *net_get_host_port(int host_id) {
  struct Man_port_at_host *p;
  for (p = g_man_host_port_list; p != NULL && p->host_id != host_id;
       p = p->next)
    ;
  return (p);
}

/* Close all host ports not used by manager */
void net_close_man_ports_at_hosts() {
  struct Man_port_at_host *p_h;
  p_h = g_man_host_port_list;
  while (p_h != NULL) {
    close(p_h->send_fd);
    close(p_h->recv_fd);
    p_h = p_h->next;
  }
}

/* Close all host ports used by manager except to host_id */
void net_close_man_ports_at_hosts_except(int host_id) {
  struct Man_port_at_host *p_h;
  p_h = g_man_host_port_list;
  while (p_h != NULL) {
    if (p_h->host_id != host_id) {
      close(p_h->send_fd);
      close(p_h->recv_fd);
    }
    p_h = p_h->next;
  }
}

/* Free all host ports to manager */
void net_free_man_ports_at_hosts() {
  struct Man_port_at_host *p_h;
  struct Man_port_at_host *t_h;
  p_h = g_man_host_port_list;
  while (p_h != NULL) {
    t_h = p_h;
    p_h = p_h->next;
    free(t_h);
  }
}

/* Close all manager ports */
void net_close_man_ports_at_man() {
  struct Man_port_at_man *p_m;
  p_m = g_man_man_port_list;
  while (p_m != NULL) {
    close(p_m->send_fd);
    close(p_m->recv_fd);
    p_m = p_m->next;
  }
}

/* Free all manager ports */
void net_free_man_ports_at_man() {
  struct Man_port_at_man *p_m;
  struct Man_port_at_man *t_m;
  p_m = g_man_man_port_list;
  while (p_m != NULL) {
    t_m = p_m;
    p_m = p_m->next;
    free(t_m);
  }
}

/* Initialize network ports and links */
int net_init(char *confFile) {
  if (g_initialized == true) { /* Check if the network is already initialized */
    colorPrint(BOLD_RED, "Network already loaded\n");
    return (0);
    /* Load network configuration file */
  } else if (load_net_data_file(confFile) == -1) {
    // Error occurred when loading network configuration file
    return (-1);
  }
  create_node_list();
  create_port_list();
  create_man_ports(&g_man_man_port_list, &g_man_host_port_list);

  return 0;
}  // End of net_init()

/*
 *  Create pipes to connect the manager to host nodes.
 *  (Note that the manager is not connected to switch nodes.)
 *  p_man is a linked list of ports at the manager.
 *  p_host is a linked list of ports at the hosts.
 *  Note that the pipes are nonblocking.
 */
void create_man_ports(struct Man_port_at_man **p_man,
                      struct Man_port_at_host **p_host) {
  struct Net_node *p;
  int fd0[2];
  int fd1[2];
  struct Man_port_at_man *p_m;
  struct Man_port_at_host *p_h;
  int host;
  for (p = g_node_list; p != NULL; p = p->next) {
    if (p->type == HOST) {
      p_m = (struct Man_port_at_man *)malloc(sizeof(struct Man_port_at_man));
      p_m->host_id = p->id;
      p_h = (struct Man_port_at_host *)malloc(sizeof(struct Man_port_at_host));
      p_h->host_id = p->id;
      pipe(fd0); /* Create a pipe */
                 /* Make the pipe nonblocking at both ends */
      fcntl(fd0[PIPE_WRITE], F_SETFL,
            fcntl(fd0[PIPE_WRITE], F_GETFL) | O_NONBLOCK);
      fcntl(fd0[PIPE_READ], F_SETFL,
            fcntl(fd0[PIPE_READ], F_GETFL) | O_NONBLOCK);
      p_m->send_fd = fd0[PIPE_WRITE];
      p_h->recv_fd = fd0[PIPE_READ];
      pipe(fd1); /* Create a pipe */
                 /* Make the pipe nonblocking at both ends */
      fcntl(fd1[PIPE_WRITE], F_SETFL,
            fcntl(fd1[PIPE_WRITE], F_GETFL) | O_NONBLOCK);
      fcntl(fd1[PIPE_READ], F_SETFL,
            fcntl(fd1[PIPE_READ], F_GETFL) | O_NONBLOCK);
      p_h->send_fd = fd1[PIPE_WRITE];
      p_m->recv_fd = fd1[PIPE_READ];
      p_m->next = *p_man;
      *p_man = p_m;
      p_h->next = *p_host;
      *p_host = p_h;
    }
  }
}

/*
This code creates a linked list of nodes. It begins by setting the global node
list g_node_list to NULL. It then iterates through the net_node_list array,
creating a new node for each element in the array and adding it to the linked
list. Each node contains an ID and type taken from the element in the
net_node_list array. The newly created node is then set as the head of the
linked list.
*/
void create_node_list() {
  struct Net_node *p;
  int i;
  g_node_list = NULL;
  for (i = 0; i < net_node_num; i++) {
    p = (struct Net_node *)malloc(sizeof(struct Net_node));
    p->id = i;
    p->type = net_node_list[i].type;
    p->next = g_node_list;
    g_node_list = p;
  }
}

void create_port_list() {
  int fd01[2];
  int fd10[2];
  g_port_list = NULL;
  for (int i = 0; i < net_link_num; i++) {
    struct Net_port *p0 = (struct Net_port *)malloc(sizeof(struct Net_port));
    struct Net_port *p1 = (struct Net_port *)malloc(sizeof(struct Net_port));
    int node0 = net_link_list[i].node0;
    int node1 = net_link_list[i].node1;
    p0->link_node_id = node0;
    p1->link_node_id = node1;
    if (net_link_list[i].type == PIPE) {
      ////////////////////// PIPE ///////////////////////////
      strncpy(p0->remoteDomain, "", MAX_DOMAIN_NAME_LENGTH);
      p0->remotePort = -1;
      strncpy(p1->remoteDomain, "", MAX_DOMAIN_NAME_LENGTH);
      p1->remotePort = -1;
      p0->type = net_link_list[i].type;
      p1->type = net_link_list[i].type;
      pipe(fd01); /* Create a pipe */
                  /* Make the pipe nonblocking at both ends */
      fcntl(fd01[PIPE_WRITE], F_SETFL,
            fcntl(fd01[PIPE_WRITE], F_GETFL) | O_NONBLOCK);
      fcntl(fd01[PIPE_READ], F_SETFL,
            fcntl(fd01[PIPE_READ], F_GETFL) | O_NONBLOCK);
      p0->send_fd = fd01[PIPE_WRITE];
      p1->recv_fd = fd01[PIPE_READ];
      pipe(fd10); /* Create a pipe */
                  /* Make the pipe nonblocking at both ends */
      fcntl(fd10[PIPE_WRITE], F_SETFL,
            fcntl(fd10[PIPE_WRITE], F_GETFL) | O_NONBLOCK);
      fcntl(fd10[PIPE_READ], F_SETFL,
            fcntl(fd10[PIPE_READ], F_GETFL) | O_NONBLOCK);
      p1->send_fd = fd10[PIPE_WRITE];
      p0->recv_fd = fd10[PIPE_READ];
      /* Insert ports in linked list */
      p0->next = p1;
      p1->next = g_port_list;
      g_port_list = p0;
    } else if (net_link_list[i].type == SOCKET) {
      ////////////////////// SOCKET ///////////////////////////
      free(p1);
      p0->type = net_link_list[i].type;
      p0->send_fd = sock_server_init(net_link_list[i].socket_local_domain,
                                     net_link_list[i].socket_local_port);
      p0->recv_fd = 0;
      strncpy(p0->localDomain, net_link_list[i].socket_local_domain,
              MAX_DOMAIN_NAME_LENGTH);
      strncpy(p0->remoteDomain, net_link_list[i].socket_remote_domain,
              MAX_DOMAIN_NAME_LENGTH);
      p0->remotePort = net_link_list[i].socket_remote_port;
      /* Insert port into linked list */
      p0->next = g_port_list;
      g_port_list = p0;
    }
  }
}  // End of create_port_list()

/*
- This function loads network configuration data from a file and stores it in
two arrays, net_node_list and net_link_list.
- If no file name is given as an argument, it prompts the user to enter one.
- The function reads the number of nodes and links from the file, and allocates
memory for arrays to store this data.
- It then reads and stores each node and link's properties, including node type,
ID, link type, nodes it connects, and socket information.
- The function displays all nodes and links stored in their respective arrays
before closing the file.
- It returns 0 if successful, or -1 otherwise.
*/
int load_net_data_file(char *confFile) {
  FILE *fp;
  char fname[MAX_FILENAME_LENGTH];
  if (confFile == NULL) {
    /* Open network configuration file */
    colorPrint(PURPLE, "Enter network data file: ");
    fgets(fname, sizeof(fname), stdin);
    fname[strcspn(fname, "\n")] = '\0';  // strip the newline character
  } else {
    strncpy(fname, confFile, MAX_FILENAME_LENGTH);
  }
  fp = fopen(fname, "r");
  if (fp == NULL) {
    colorPrint(RED, "net.c: File did not open\n");
    return (-1);
  }
  int i;
  int node_num;
  char node_type;
  int node_id;
  fscanf(fp, "%d", &node_num);
  colorPrint(PURPLE, "Number of Nodes = %d: \n", node_num);
  net_node_num = node_num;
  if (node_num < 1) {
    colorPrint(RED, "net.c: No nodes\n");
    fclose(fp);
    return (-1);
  } else {
    net_node_list =
        (struct Net_node *)malloc(sizeof(struct Net_node) * node_num);
    for (i = 0; i < node_num; i++) {
      fscanf(fp, " %c ", &node_type);
      if (node_type == 'H') {
        fscanf(fp, " %d ", &node_id);
        net_node_list[i].type = HOST;
        net_node_list[i].id = node_id;
      } else if (node_type == 'S') {
        fscanf(fp, " %d ", &node_id);
        net_node_list[i].type = SWITCH;
        net_node_list[i].id = node_id;
      } else {
        colorPrint(RED, " net.c: Unidentified Node Type\n");
      }
      if (i != node_id) {
        colorPrint(PURPLE, " net.c: Incorrect node id\n");
        fclose(fp);
        return (-1);
      }
    }
  }
  int link_num;
  char link_type;
  int node0, node1;
  fscanf(fp, " %d ", &link_num);
  colorPrint(PURPLE, "Number of Links = %d\n", link_num);
  net_link_num = link_num;
  if (link_num < 1) {
    colorPrint(RED, "net.c: No links\n");
    fclose(fp);
    return (-1);
  } else {
    net_link_list =
        (struct Net_link *)malloc(sizeof(struct Net_link) * link_num);
    for (i = 0; i < link_num; i++) {
      fscanf(fp, " %c ", &link_type);
      if (link_type == 'P') {
        net_link_list[i].type = PIPE;
        fscanf(fp, " %d %d ", &node0, &node1);
        net_link_list[i].node0 = node0;
        net_link_list[i].node1 = node1;
        // Set unused fields to empty
        strncpy(net_link_list[i].socket_local_domain, "",
                MAX_DOMAIN_NAME_LENGTH);
        net_link_list[i].socket_local_port = 0;
        strncpy(net_link_list[i].socket_remote_domain, "",
                MAX_DOMAIN_NAME_LENGTH);
        net_link_list[i].socket_remote_port = 0;
      } else if (link_type == 'S') {
        net_link_list[i].type = SOCKET;
        fscanf(fp, "%d %s %d %s %d", &net_link_list[i].node0,
               net_link_list[i].socket_local_domain,
               &net_link_list[i].socket_local_port,
               net_link_list[i].socket_remote_domain,
               &net_link_list[i].socket_remote_port);
        net_link_list[i].node1 = -1;
      } else {
        colorPrint(PURPLE, " net.c: Unidentified link type\n");
      }
    }
  }
  /* Display the nodes and links of the network */
  colorPrint(PURPLE, "Nodes:\n");
  for (i = 0; i < net_node_num; i++) {
    if (net_node_list[i].type == HOST) {
      colorPrint(PURPLE, " Node %d HOST\n", net_node_list[i].id);
    } else if (net_node_list[i].type == SWITCH) {
      colorPrint(PURPLE, " Node %d SWITCH\n", net_node_list[i].id);
    } else {
      colorPrint(RED, " Unknown Type\n");
    }
  }
  colorPrint(PURPLE, "Links:\n");
  for (i = 0; i < net_link_num; i++) {
    if (net_link_list[i].type == PIPE) {
      colorPrint(PURPLE, " Link (%d, %d) PIPE\n", net_link_list[i].node0,
                 net_link_list[i].node1);
    } else if (net_link_list[i].type == SOCKET) {
      colorPrint(PURPLE, " Link (%d, %s:%d, %s:%d) SOCKET\n",
                 net_link_list[i].node0, net_link_list[i].socket_local_domain,
                 net_link_list[i].socket_local_port,
                 net_link_list[i].socket_remote_domain,
                 net_link_list[i].socket_remote_port);
    }
  }
  fclose(fp);
  return (0);
}
