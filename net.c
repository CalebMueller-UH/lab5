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

#include "host.h"
#include "main.h"
#include "man.h"
#include "packet.h"

#define MAX_FILE_NAME_LENGTH 100
#define MAX_DOMAIN_NAME_LENGTH 100
#define PIPE_READ 0
#define PIPE_WRITE 1

enum bool { FALSE, TRUE };

/*
 * Struct used to store a link. It is used when the
 * network configuration file is loaded.
 */
struct net_link {
  enum NetLinkType type;
  int pipe_node0;
  int pipe_node1;
  int socket_node0;
  char socket_domain_name0[MAX_DOMAIN_NAME_LENGTH];
  int socket_port_num0;
  char socket_domain_name1[MAX_DOMAIN_NAME_LENGTH];
  int socket_port_num1;
};

// struct net_link {
//   enum NetLinkType type;
//   int pipe_node0;
//   int pipe_node1;
// };

/*
 * The following are private global variables to this file net.c
 */
static enum bool g_initialized = FALSE; /* Network initialized? */
/* The network is initialized only once */

/*
 * net_node_list[] and net_node_num have link information from
 * the network configuration file.
 * g_node_list is a linked list version of net_node_list[]
 */
static struct net_node *net_node_list;
static int net_node_num;
static struct net_node *g_node_list = NULL;

/*
 * net_link_list[] and net_link_num have link information from
 * the network configuration file
 */
static struct net_link *net_link_list;
static int net_link_num;

/*
 * Global private variables about ports of network node links
 * and ports of links to the manager
 */
static struct net_port *g_port_list = NULL;

static struct man_port_at_man *g_man_man_port_list = NULL;
static struct man_port_at_host *g_man_host_port_list = NULL;

/*
 * Loads network configuration file and creates data structures
 * for nodes and links.  The results are accessible through
 * the private global variables
 */
int load_net_data_file();

/*
 * Creates a data structure for the nodes
 */
void create_node_list();

/*
 * Creates links, using pipes
 * Then creates a port list for these links.
 */
void create_port_list();

/*
 * Creates ports at the manager and ports at the hosts so that
 * the manager can communicate with the hosts.  The list of
 * ports at the manager side is p_m.  The list of ports
 * at the host side is p_h.
 */
void create_man_ports(struct man_port_at_man **p_m,
                      struct man_port_at_host **p_h);

void net_close_man_ports_at_hosts();
void net_close_man_ports_at_hosts_except(int host_id);
void net_free_man_ports_at_hosts();
void net_close_man_ports_at_man();
void net_free_man_ports_at_man();

/*
 * Get the list of ports for host host_id
 */
struct net_port *net_get_port_list(int host_id);

/*
 * Get the list of nodes
 */
struct net_node *net_get_node_list();

/*
Function to retrieve a linked list of network ports that belong to a specified
node. Takes in an ID of the node of interest as an argument. Returns a pointer
to the head of the resulting linked list.
*/
struct net_port *net_get_port_list(int id_of_interest) {
  struct net_port **curr;
  struct net_port *result;
  struct net_port *temp;
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
struct net_node *net_get_node_list() { return g_node_list; }

/* Return linked list of ports used by the manager to connect to hosts */
struct man_port_at_man *net_get_man_ports_at_man_list() {
  return (g_man_man_port_list);
}

/* Return the port used by host to link with other nodes */
struct man_port_at_host *net_get_host_port(int host_id) {
  struct man_port_at_host *p;

  for (p = g_man_host_port_list; p != NULL && p->host_id != host_id;
       p = p->next)
    ;

  return (p);
}

/* Close all host ports not used by manager */
void net_close_man_ports_at_hosts() {
  struct man_port_at_host *p_h;

  p_h = g_man_host_port_list;

  while (p_h != NULL) {
    close(p_h->send_fd);
    close(p_h->recv_fd);
    p_h = p_h->next;
  }
}

/* Close all host ports used by manager except to host_id */
void net_close_man_ports_at_hosts_except(int host_id) {
  struct man_port_at_host *p_h;

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
  struct man_port_at_host *p_h;
  struct man_port_at_host *t_h;

  p_h = g_man_host_port_list;

  while (p_h != NULL) {
    t_h = p_h;
    p_h = p_h->next;
    free(t_h);
  }
}

/* Close all manager ports */
void net_close_man_ports_at_man() {
  struct man_port_at_man *p_m;

  p_m = g_man_man_port_list;

  while (p_m != NULL) {
    close(p_m->send_fd);
    close(p_m->recv_fd);
    p_m = p_m->next;
  }
}

/* Free all manager ports */
void net_free_man_ports_at_man() {
  struct man_port_at_man *p_m;
  struct man_port_at_man *t_m;

  p_m = g_man_man_port_list;

  while (p_m != NULL) {
    t_m = p_m;
    p_m = p_m->next;
    free(t_m);
  }
}

/* Initialize network ports and links */
int net_init() {
  if (g_initialized == TRUE) { /* Check if the network is already initialized */
    printf("Network already loaded\n");
    return (0);
  } else if (load_net_data_file() == -1) { /* Load network configuration file */
    // Error occurred when loading network configuration file
    return (-1);
  }
  /*
   * Create a linked list of node information at g_node_list
   */
  create_node_list();

  /*
   * Create pipes and sockets to realize network links
   * and store the ports of the links at g_port_list
   */
  create_port_list();

  /*
   * Create pipes to connect the manager to hosts
   * and store the ports at the host at g_man_host_port_list
   * as a linked list
   * and store the ports at the manager at g_man_man_port_list
   * as a linked list
   */
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
void create_man_ports(struct man_port_at_man **p_man,
                      struct man_port_at_host **p_host) {
  struct net_node *p;
  int fd0[2];
  int fd1[2];
  struct man_port_at_man *p_m;
  struct man_port_at_host *p_h;
  int host;

  for (p = g_node_list; p != NULL; p = p->next) {
    if (p->type == HOST) {
      p_m = (struct man_port_at_man *)malloc(sizeof(struct man_port_at_man));
      p_m->host_id = p->id;

      p_h = (struct man_port_at_host *)malloc(sizeof(struct man_port_at_host));
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

/* Create a linked list of nodes at g_node_list */
void create_node_list() {
  struct net_node *p;
  int i;

  g_node_list = NULL;
  for (i = 0; i < net_node_num; i++) {
    p = (struct net_node *)malloc(sizeof(struct net_node));
    p->id = i;
    p->type = net_node_list[i].type;
    p->next = g_node_list;
    g_node_list = p;
  }
}

void create_port_list() {
  struct net_port *p0;
  struct net_port *p1;
  int node0, node1;
  int fd01[2];
  int fd10[2];
  int sockfd;
  struct sockaddr_in servaddr;
  int i;

  g_port_list = NULL;
  for (i = 0; i < net_link_num; i++) {
    ////////////////////////////////////
    if (net_link_list[i].type == PIPE) {
      node0 = net_link_list[i].pipe_node0;
      node1 = net_link_list[i].pipe_node1;

      p0 = (struct net_port *)malloc(sizeof(struct net_port));
      p0->type = net_link_list[i].type;
      p0->link_node_id = node0;

      p1 = (struct net_port *)malloc(sizeof(struct net_port));
      p1->type = net_link_list[i].type;
      p1->link_node_id = node1;

      pipe(fd01); /* Create a pipe */
                  /* Make the pipe nonblocking at both ends */
      fcntl(fd01[PIPE_WRITE], F_SETFL,
            fcntl(fd01[PIPE_WRITE], F_GETFL) | O_NONBLOCK);
      fcntl(fd01[PIPE_READ], F_SETFL,
            fcntl(fd01[PIPE_READ], F_GETFL) | O_NONBLOCK);
      p0->pipe_send_fd = fd01[PIPE_WRITE];
      p1->pipe_recv_fd = fd01[PIPE_READ];

      pipe(fd10); /* Create a pipe */
                  /* Make the pipe nonblocking at both ends */
      fcntl(fd10[PIPE_WRITE], F_SETFL,
            fcntl(fd10[PIPE_WRITE], F_GETFL) | O_NONBLOCK);
      fcntl(fd10[PIPE_READ], F_SETFL,
            fcntl(fd10[PIPE_READ], F_GETFL) | O_NONBLOCK);
      p1->pipe_send_fd = fd10[PIPE_WRITE];
      p0->pipe_recv_fd = fd10[PIPE_READ];

      p0->next = p1; /* Insert ports in linked lisst */
      p1->next = g_port_list;
      g_port_list = p0;
    } else if (net_link_list[i].type == SOCKET) {
      ////////////////////////////////////
      node0 = net_link_list[i].socket_node0;
      char *domain_name0 = net_link_list[i].socket_domain_name0;
      int port_num0 = net_link_list[i].socket_port_num0;
      char *domain_name1 = net_link_list[i].socket_domain_name1;
      int port_num1 = net_link_list[i].socket_port_num1;

      // Create a socket
      sockfd = socket(AF_INET, SOCK_STREAM, 0);

      // Set up the server address structure
      bzero(&servaddr, sizeof(servaddr));
      servaddr.sin_family = AF_INET;
      servaddr.sin_port = htons(port_num1);
      inet_pton(AF_INET, domain_name1, &servaddr.sin_addr);

      // Connect to the server
      if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) !=
          0) {
        perror("create_port_list: Socket connect failed");
        exit(1);
      }

      // Create the net_ports for the two ends of the socket link
      p0 = (struct net_port *)malloc(sizeof(struct net_port));
      p0->type = net_link_list[i].type;
      p0->link_node_id = node0;
      p0->sock_send_fd = sockfd;
      p0->next = NULL;

      p1 = (struct net_port *)malloc(sizeof(struct net_port));
      p1->type = net_link_list[i].type;
      p1->link_node_id = -1;  // not used for socket ports
      p1->sock_listen_fd =
          sockfd;  // use the same socket file descriptor for both ends
      p1->next = NULL;

      // Insert ports in linked list
      p0->next = g_port_list;
      g_port_list = p0;
      p1->next = g_port_list;
      g_port_list = p1;
    }
  }
}  // End of create_port_list()

// void create_port_list() {
//   struct net_port *p0;
//   struct net_port *p1;
//   int node0, node1;
//   int fd01[2];
//   int fd10[2];
//   int i;

//   g_port_list = NULL;
//   for (i = 0; i < net_link_num; i++) {
//     if (net_link_list[i].type == PIPE) {
//       node0 = net_link_list[i].pipe_node0;
//       node1 = net_link_list[i].pipe_node1;

//       p0 = (struct net_port *)malloc(sizeof(struct net_port));
//       p0->type = net_link_list[i].type;
//       p0->link_node_id = node0;

//       p1 = (struct net_port *)malloc(sizeof(struct net_port));
//       p1->type = net_link_list[i].type;
//       p1->link_node_id = node1;

//       pipe(fd01); /* Create a pipe */
//                   /* Make the pipe nonblocking at both ends */
//       fcntl(fd01[PIPE_WRITE], F_SETFL,
//             fcntl(fd01[PIPE_WRITE], F_GETFL) | O_NONBLOCK);
//       fcntl(fd01[PIPE_READ], F_SETFL,
//             fcntl(fd01[PIPE_READ], F_GETFL) | O_NONBLOCK);
//       p0->pipe_send_fd = fd01[PIPE_WRITE];
//       p1->pipe_recv_fd = fd01[PIPE_READ];

//       pipe(fd10); /* Create a pipe */
//                   /* Make the pipe nonblocking at both ends */
//       fcntl(fd10[PIPE_WRITE], F_SETFL,
//             fcntl(fd10[PIPE_WRITE], F_GETFL) | O_NONBLOCK);
//       fcntl(fd10[PIPE_READ], F_SETFL,
//             fcntl(fd10[PIPE_READ], F_GETFL) | O_NONBLOCK);
//       p1->pipe_send_fd = fd10[PIPE_WRITE];
//       p0->pipe_recv_fd = fd10[PIPE_READ];

//       p0->next = p1; /* Insert ports in linked lisst */
//       p1->next = g_port_list;
//       g_port_list = p0;
//     } else if (net_link_list[i].type == SOCKET) {
//     }
//   }
// }  // End of create_port_list()

/*
 * Loads network configuration file and creates data structures
 * for nodes and links.
 */
int load_net_data_file() {
  FILE *fp;
  char fname[MAX_FILE_NAME_LENGTH];

  /* Open network configuration file */
  printf("Enter network data file: ");
  fgets(fname, sizeof(fname), stdin);
  fname[strcspn(fname, "\n")] = '\0';  // strip the newline character
  fp = fopen(fname, "r");
  if (fp == NULL) {
    printf("net.c: File did not open\n");
    return (-1);
  }

  int i;
  int node_num;
  char node_type;
  int node_id;

  /*
   * Read node information from the file and
   * fill in data structure for nodes.
   * The data structure is an array net_node_list[ ]
   * and the size of the array is net_node_num.

   * Note that net_node_list[] and net_node_num are
   * private global variables.
   */
  fscanf(fp, "%d", &node_num);
  printf("Number of Nodes = %d: \n", node_num);
  net_node_num = node_num;

  if (node_num < 1) {
    printf("net.c: No nodes\n");
    fclose(fp);
    return (-1);
  } else {
    net_node_list =
        (struct net_node *)malloc(sizeof(struct net_node) * node_num);
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
        printf(" net.c: Unidentified Node Type\n");
      }

      if (i != node_id) {
        printf(" net.c: Incorrect node id\n");
        fclose(fp);
        return (-1);
      }
    }
  }
  /*
   * Read link information from the file and
   * fill in data structure for links.
   * The data structure is an array net_link_list[ ]
   * and the size of the array is net_link_num.
   * Note that net_link_list[] and net_link_num are
   * private global variables.
   */

  int link_num;
  char link_type;
  int node0, node1;

  fscanf(fp, " %d ", &link_num);
  printf("Number of Links = %d\n", link_num);
  net_link_num = link_num;

  if (link_num < 1) {
    printf("net.c: No links\n");
    fclose(fp);
    return (-1);
  } else {
    net_link_list =
        (struct net_link *)malloc(sizeof(struct net_link) * link_num);
    for (i = 0; i < link_num; i++) {
      fscanf(fp, " %c ", &link_type);
      if (link_type == 'P') {
        fscanf(fp, " %d %d ", &node0, &node1);
        net_link_list[i].type = PIPE;
        net_link_list[i].pipe_node0 = node0;
        net_link_list[i].pipe_node1 = node1;
      } else if (link_type == 'S') {
        char domain_name0[MAX_DOMAIN_NAME_LENGTH];
        int port_num0;
        char domain_name1[MAX_DOMAIN_NAME_LENGTH];
        int port_num1;
        fscanf(fp, " %d %s %d %s %d ", &node0, domain_name0, &port_num0,
               domain_name1, &port_num1);
        net_link_list[i].type = SOCKET;
        net_link_list[i].socket_node0 = node0;
        strcpy(net_link_list[i].socket_domain_name0, domain_name0);
        net_link_list[i].socket_port_num0 = port_num0;
        strcpy(net_link_list[i].socket_domain_name1, domain_name1);
        net_link_list[i].socket_port_num1 = port_num1;
      } else {
        printf(" net.c: Unidentified link type\n");
      }
    }
  }

  /* Display the nodes and links of the network */
  printf("Nodes:\n");
  for (i = 0; i < net_node_num; i++) {
    if (net_node_list[i].type == HOST) {
      printf(" Node %d HOST\n", net_node_list[i].id);
    } else if (net_node_list[i].type == SWITCH) {
      printf(" SWITCH\n");
    } else {
      printf(" Unknown Type\n");
    }
  }
  printf("Links:\n");
  for (i = 0; i < net_link_num; i++) {
    if (net_link_list[i].type == PIPE) {
      printf(" Link (%d, %d) PIPE\n", net_link_list[i].pipe_node0,
             net_link_list[i].pipe_node1);
    } else if (net_link_list[i].type == SOCKET) {
      printf(" Link (%d, %s:%d, %s:%d) SOCKET\n", net_link_list[i].socket_node0,
             net_link_list[i].socket_domain_name0,
             net_link_list[i].socket_port_num0,
             net_link_list[i].socket_domain_name1,
             net_link_list[i].socket_port_num1);
    }
  }

  fclose(fp);
  return (0);
}
