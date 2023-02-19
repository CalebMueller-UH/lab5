/*
    switch.c
*/

#include "switch.h"

#include "main.h"

struct packet *in_packet; /* Incoming packet */
struct packet *new_packet;

struct job_queue switch_q;

void switch_main(int switch_id) {
  ///////////// Initialize State //////////////

  ///////////////////Create an array node_port[ ]
  ///////////////////to store the network link ports
  struct net_port *node_port_list;
  node_port_list = net_get_port_list(switch_id);

  /*  Count the number of network link ports */
  int number_of_node_ports = 0;
  for (struct net_port *p = node_port_list; p != NULL; p = p->next) {
    number_of_node_ports++;
  }

  // Create memory space for array of pointers to node ports
  struct net_port **node_port = (struct net_port **)malloc(
      number_of_node_ports * sizeof(struct net_port *));

  /* Load ports into the array */
  struct net_port *p;
  p = node_port_list;
  for (int k = 0; k < number_of_node_ports; k++) {
    node_port[k] = p;
    p = p->next;
  }

  /* Initialize the job queue */
  // job_q_init(&job_q);

  /* Configure Connection */

  return;
}