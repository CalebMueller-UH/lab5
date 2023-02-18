/*
    switch.c
*/

#include "switch.h"

#include "main.h"

struct packet *in_packet; /* Incoming packet */
struct packet *new_packet;

struct job_queue switch_q;

void switch_main(int switch_id) {
  /* Initialize State */

  /*
   * Create an array node_port[ ] to store the network link ports
   * at the switch.  The number of ports is node_port_num
   */
  struct net_port *node_port_list;
  node_port_list = net_get_port_list(switch_id);

  /*  Count the number of network link ports */
  int node_port_num;  // Number of node ports
  node_port_num = 0;
  for (struct net_port *p = node_port_list; p != NULL; p = p->next) {
    node_port_num++;
  }

  /* Create memory space for the array */
  struct net_port **node_port;  // Array of pointers to node ports
  node_port =
      (struct net_port **)malloc(node_port_num * sizeof(struct net_port *));

  /* Load ports into the array */
  struct net_port *p;
  p = node_port_list;
  for (int k = 0; k < node_port_num; k++) {
    node_port[k] = p;
    p = p->next;
  }

  /* Initialize the job queue */
  // job_q_init(&job_q);

  /* Configure Connection */

  return;
}