/*
    switch.c
*/

#include "switch.h"

#include <stdbool.h>

struct packet *in_packet; /* Incoming packet */
struct packet *new_packet;

/*
This function searches a routing table for a valid ID.
The function takes in two parameters: a pointer to a struct tableEntry and an
integer ID. It iterates through the maximum number of routes and checks if the
ID matches the current entry's ID and if it is valid. If it finds a match, it
returns the index of that entry in the routing table. Otherwise, it returns -1
indicating that no valid entry was found.
*/
int searchRoutingTableForValidID(struct tableEntry *rt, int id) {
  for (int i = 0; i < MAX_NUM_ROUTES; i++) {
    if (rt[i].id == id && rt[i].isValid == true) {
      return i;
    }
  }
  return -1;
}

/*
This function takes a job struct, a table entry, an array of net_port pointers
and the size of the array as parameters. It then iterates through the port_array
and sends the job's packet to each port in the array, except for the port that
corresponds to the job's in_port_index.
*/
void broadcastToAllButSender(struct job_struct *job, struct tableEntry *rt,
                             struct net_port **port_array,
                             int port_array_size) {
  for (int i = 0; i < port_array_size; i++) {
    if (i != job->in_port_index) {
      packet_send(port_array[i], job->packet);
    }
  }
}

void switch_main(int switch_id) {
  ////////////////// Initializing //////////////////
  struct net_port *node_port_list;
  struct net_port **node_port_array;
  int node_port_array_size;
  struct net_port *p;
  struct job_struct *new_job;
  struct job_struct *new_job2;
  ////// Initialize Router Table //////

  struct job_queue switch_q;

  ////// Initialize Switch Job Queue //////
  /*
  Routing table index == port
  Each element within routingTable has both an isValid and dest property
  */
  struct tableEntry *routingTable =
      (struct tableEntry *)malloc(sizeof(struct tableEntry) * MAX_NUM_ROUTES);
  for (int i = 0; i < MAX_NUM_ROUTES; i++) {
    routingTable[i].isValid = false;
    routingTable[i].id = -1;
  }
  ////// Initialize Router Table //////

  /*
   * Create an array node_port_array[ ] to store the network link ports
   * at the switch.  The number of ports is node_port_array_size
   */
  node_port_list = net_get_port_list(switch_id);

  /*  Count the number of network link ports */
  node_port_array_size = 0;
  for (p = node_port_list; p != NULL; p = p->next) {
    node_port_array_size++;
  }
  /* Create memory space for the array */
  node_port_array = (struct net_port **)malloc(node_port_array_size *
                                               sizeof(struct net_port *));

  /* Load ports into the array */
  p = node_port_list;
  for (int k = 0; k < node_port_array_size; k++) {
    node_port_array[k] = p;
    p = p->next;
  }

  /* Initialize the job queue */
  job_queue_init(&switch_q);

  while (1) {
    /////// Receive In-Coming packet and translate it to job //////
    for (int k = 0; k < node_port_array_size; k++) {
      struct packet *in_packet = (struct packet *)malloc(sizeof(struct packet));
      int n = packet_recv(node_port_array[k], in_packet);
      if (n > 0) {
#ifdef DEBUG
        printf(
            "\033[0;33m"  // yellow text
            "DEBUG: id:%d switch_main: Switch received packet on port:%d "
            "src:%d dst:%d\n"
            "\033[0m",  // regular text
            switch_id, k, in_packet->src, in_packet->dst);
#endif
        new_job = (struct job_struct *)malloc(sizeof(struct job_struct));
        new_job->in_port_index = k;
        new_job->packet = in_packet;

        int srcPortNum = -1;
        int dstPortNum = -1;

        /*
        if the routingTable does not have a valid matching entry for incoming
        packet source, add it to routingTable by setting isValid, and
        associating its id
        */
        if (routingTable[k].id != in_packet->src || !routingTable[k].isValid) {
          routingTable[k].isValid = true;
          routingTable[k].id = in_packet->src;
        }

        int dstIndex =
            searchRoutingTableForValidID(routingTable, in_packet->dst);
        if (dstIndex < 0) {
          /*
       if routingTable does not have a valid matching entry for incoming
       packet destination enque an UNKNOWN_PORT_BROADCAST job to broadcast the
       current packet to all ports except the current port
       */
          new_job->type = UNKNOWN_PORT_BROADCAST;
          job_enqueue(switch_id, &switch_q, new_job);
        } else {
          // Enqueue a FORWARD_PACKET_TO_PORT job forward the current in_packet
          // to the associated port
          new_job->type = FORWARD_PACKET_TO_PORT;
          new_job->out_port_index = dstIndex;
          job_enqueue(switch_id, &switch_q, new_job);
        }
      } else {
        free(in_packet);
      }
    }

    //////////// FETCH JOB FROM QUEUE ////////////
    if (job_queue_length(&switch_q) > 0) {
      /* Get a new job from the job queue */
      new_job = job_dequeue(switch_id, &switch_q);

      //////////// EXECUTE FETCHED JOB ////////////
      switch (new_job->type) {
        case UNKNOWN_PORT_BROADCAST:
          broadcastToAllButSender(new_job, routingTable, node_port_array,
                                  node_port_array_size);
          break;
        case FORWARD_PACKET_TO_PORT:
          packet_send(node_port_array[new_job->out_port_index],
                      new_job->packet);
          break;
      }
    }

    /* The host goes to sleep for 10 ms */
    usleep(TENMILLISEC);
  } /* End of while loop */
}