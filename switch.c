/*
    switch.c
*/

#include "switch.h"

struct packet *in_packet; /* Incoming packet */
struct packet *new_packet;

void switch_main(int switch_id) {
  ////////////////////////////////////
  ////// Network Initialization //////

  // Create an array node_port[] to store the network link ports
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

  ////// Network Initialization //////
  ////////////////////////////////////

  ////// Job Queue Setup //////
  /////////////////////////////

  struct job_queue switch_q;
  struct job_struct *new_job;

  /* Initialize the job queue */
  job_queue_init(&switch_q);

  /////////////////////////////
  ////// Job Queue Setup //////

  /////////////////////////////////////////////
  ////////////// Work Loop ////////////////////
  while (1) {
    /*
     * Get packets from incoming links and translate to jobs
     * Put jobs in job queue
     */

    for (int k = 0; k < number_of_node_ports; k++) { /* Scan all ports */
      in_packet = (struct packet *)malloc(sizeof(struct packet));
      int n = packet_recv(node_port[k], in_packet);
      if ((n > 0) && ((int)in_packet->dst == switch_id)) {
        new_job = (struct job_struct *)malloc(sizeof(struct job_struct));
        new_job->in_port_index = k;
        new_job->packet = in_packet;

      } else {
        free(in_packet);
      }
    }

    /*
     * Execute one job in the job queue
     */

    if (job_queue_length(&switch_q) > 0) {
      /* Get a new job from the job queue */
      new_job = job_dequeue(&switch_q);

      /* Send packet on all ports */
      switch (new_job->type) {
        /* Send packets on all ports */
        case JOB_SEND_PKT_ALL_PORTS:
          for (int k = 0; k < number_of_node_ports; k++) {
            packet_send(node_port[k], new_job->packet);
          }
          free(new_job->packet);
          free(new_job);
          break;

          /* The next three jobs deal with the pinging process */

          /* The host goes to sleep for 10 ms */
          usleep(TENMILLISEC);

      } /* End of while loop */

      ////////////// Work Loop ////////////////////
      /////////////////////////////////////////////

      return;
    }
  }
}