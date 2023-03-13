/*
    switch.c
*/

#include "switch.h"

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "color.h"
#include "constants.h"
#include "job.h"
#include "main.h"
#include "net.h"
#include "packet.h"
#include "semaphore.h"

#define MAX_NUM_ROUTES 100

// Searches the routing table for a matching valid TableEntry matching id
// Returns routing table index of valid id, or -1 if unsuccessful
//  Note that routing table index is = to port
int searchRoutingTableForValidID(struct TableEntry *rt, int id) {
  for (int i = 0; i < MAX_NUM_ROUTES; i++) {
    if (rt[i].id == id && rt[i].isValid == true) {
      return i;
    }
  }
  return -1;
}

/*
This function takes a job struct , a table entry, an array of Net_port
pointers and the size of the array as parameters. It then iterates through the
port_array and sends the job's packet to each port in the array, except for the
port that corresponds to the job->packet->src
*/
void broadcastToAllButSender(struct Job *job, struct TableEntry *rt,
                             struct Net_port **port_array,
                             int port_array_size) {
  for (int i = 0; i < port_array_size; i++) {
    if (i != job->packet->src) {
      packet_send(port_array[i], job->packet);
    }
  }
}

void switch_main(int switch_id) {
  ////// Initialize state of switch //////

  // Initialize node_port_array
  struct Net_port *node_port_list;
  struct Net_port **node_port_array;  // Array of pointers to node ports
  int node_port_array_size;           // Number of node ports
  node_port_list = net_get_port_list(switch_id);
  /*  Count the number of network link ports */
  node_port_array_size = 0;
  for (struct Net_port *p = node_port_list; p != NULL; p = p->next) {
    node_port_array_size++;
  }
  /* Create memory space for the array */
  node_port_array = (struct Net_port **)malloc(node_port_array_size *
                                               sizeof(struct Net_port *));
  /* Load ports into the array */
  {
    struct Net_port *p = node_port_list;
    for (int portNum = 0; portNum < node_port_array_size; portNum++) {
      node_port_array[portNum] = p;
      p = p->next;
    }
  }

  /* Initialize the job queue */
  struct JobQueue switch_q;
  job_queue_init(&switch_q);

  ////// Initialize Router Table //////
  struct TableEntry *routingTable =
      (struct TableEntry *)malloc(sizeof(struct TableEntry) * MAX_NUM_ROUTES);
  for (int i = 0; i < MAX_NUM_ROUTES; i++) {
    routingTable[i].isValid = false;
    routingTable[i].id = -1;
  }

  while (1) {
    ////////////////////////////////////////////////////////////////////////////
    ////////////////////////////// PACKET HANDLER //////////////////////////////

    for (int portNum = 0; portNum < node_port_array_size; portNum++) {
      struct Packet *received_packet =
          (struct Packet *)malloc(sizeof(struct Packet));
      int n = packet_recv(node_port_array[portNum], received_packet);
      if (n > 0) {
#ifdef DEBUG
        colorPrint(
            YELLOW,
            "DEBUG: id:%d switch_main: Switch received packet on port:%d "
            "src:%d dst:%d\n",
            switch_id, portNum, received_packet->src, received_packet->dst);
#endif
        struct Job *swJob = job_create_empty();
        swJob->packet = received_packet;

        int dstIndex =
            searchRoutingTableForValidID(routingTable, received_packet->dst);
        if (dstIndex < 0) {
          // destination of received packet is not in routing table
          swJob->type = JOB_BROADCAST_PKT;
          job_enqueue(switch_id, &switch_q, swJob);
        } else {
          // destination of received packet has been found in routing table
          swJob->type = JOB_FORWARD_PKT;
          job_enqueue(switch_id, &switch_q, swJob);
        }
      } else {
        free(received_packet);
        received_packet = NULL;
      }
    }

    ////////////////////////////// PACKET HANDLER //////////////////////////////
    ////////////////////////////////////////////////////////////////////////////
    // -------------------------------------------------------------------------
    ////////////////////////////////////////////////////////////////////////////
    //////////////////////////////// JOB HANDLER ///////////////////////////////

    if (job_queue_length(&switch_q) > 0) {
      /* Get a new job from the job queue */
      struct Job *job_from_queue = job_dequeue(switch_id, &switch_q);

      //////////// EXECUTE FETCHED JOB ////////////
      switch (job_from_queue->type) {
        case JOB_BROADCAST_PKT:
          broadcastToAllButSender(job_from_queue, routingTable, node_port_array,
                                  node_port_array_size);
          break;
        case JOB_FORWARD_PKT:
          int dstPort = searchRoutingTableForValidID(
              routingTable, job_from_queue->packet->dst);
          packet_send(node_port_array[dstPort], job_from_queue->packet);
          break;
      }

      free(job_from_queue->packet);
      job_from_queue->packet = NULL;
      free(job_from_queue);
      job_from_queue = NULL;
    }

    //////////////////////////////// JOB HANDLER ///////////////////////////////
    ////////////////////////////////////////////////////////////////////////////

    /* The host goes to sleep for 10 ms */
    usleep(LOOP_SLEEP_TIME_US);
  } /* End of while loop */

  /* Free dynamically allocated memory */
  for (int i = 0; i < node_port_array_size; i++) {
    free(node_port_array[i]);
    node_port_array[i] = NULL;
  }
  free(node_port_array);
  node_port_array = NULL;
  free(routingTable);
  routingTable = NULL;
}
