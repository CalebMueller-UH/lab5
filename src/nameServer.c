/*
    nameServer.h
*/

#include "nameServer.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "color.h"
#include "constants.h"
#include "host.h"
#include "job.h"
#include "net.h"
#include "packet.h"

// Used for register_name_to_table when ID is unfound
#define UNKNOWN -1

int sendPacketTo2(struct Net_port **arr, int arrSize, struct Packet *p) {
  // Find which Net_port entry in net_port_array has desired destination
  int destIndex = -1;
  for (int i = 0; i < arrSize; i++) {
    if (arr[i]->link_node_id == p->dst) {
      destIndex = i;
    }
  }
  // If node_port_array had the destination id, send to that node
  if (destIndex >= 0) {
    packet_send(arr[destIndex], p);
  } else {
    // Else, broadcast packet to all connected hosts
    for (int i = 0; i < arrSize; i++) {
      packet_send(arr[i], p);
    }
  }
}  // End of sendPacketTo2

void init_nametable(char **nametable) {
  for (int i = 0; i <= MAX_NUM_NAMES; i++) {
    nametable[i] = malloc(PACKET_PAYLOAD_MAX + 1);
    nametable[i][0] = '\0';
  }
}

////// Function that puts register names onto the nametable //////
void register_name_to_table(struct Packet *pkt, char **nametable,
                            struct Net_port **arr, int arrSize) {
  colorPrint(BOLD_RED, "register_name_to_table\n");
  printPacket(pkt);
  int index =
      (int)pkt->src;  // Convert the source character to an integer index
  int length = pkt->length;
  if (index < 0 || index >= PACKET_PAYLOAD_MAX || length <= 0) {
    return;  // Invalid input, do nothing
  }

  strncpy(nametable[pkt->src], pkt->payload, pkt->length);
  colorPrint(BOLD_RED, "%s was registered to index %d\n", nametable[pkt->src],
             pkt->src);

  const char *remsg = "Domain Name Registered!";
  const int remsgLen = strlen(remsg) + 1;

  pkt->dst = pkt->src;
  pkt->src = STATIC_DNS_ID;
  pkt->type = PKT_DNS_RESPONSE;
  pkt->length = remsgLen;
  strncpy(pkt->payload, remsg, PACKET_PAYLOAD_MAX);
  sendPacketTo2(arr, arrSize, pkt);
}

////// Function that searches the nametable for an ID //////

int retrieve_id_from_table(char *name, char **nametable) {
  for (int i = 0; i < MAX_NUM_NAMES; i++) {
    if (strncmp(name, nametable[i], PACKET_PAYLOAD_MAX) == 0) {
      return i;
    }
  }
  // name wasn't found, return error
  return -1;
}

void name_server_main(int name_id) {
  ////// Initialize state of switch ////// //entry point

  // Initialize node_port_array
  struct Net_port *node_port_list;
  struct Net_port **node_port_array;  // Array of pointers to node ports
  int node_port_array_size;           // Number of node ports
  node_port_list = net_get_port_list(name_id);

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
  struct JobQueue name_q;
  job_queue_init(&name_q);

  ////// Initialize Name Table //////
  char **nametable = malloc((MAX_NUM_NAMES + 1) * sizeof(char *));
  init_nametable(nametable);

  while (1) {
    ////////////////////////////////////////////////////////////////////////////
    ////////////////////////////// PACKET HANDLER //////////////////////////////

    for (int portNum = 0; portNum < node_port_array_size; portNum++) {
      struct Packet *received_packet =
          (struct Packet *)malloc(sizeof(struct Packet));
      int n = packet_recv(node_port_array[portNum], received_packet);
      if (n > 0) {
#ifdef DEBUG
        colorPrint(YELLOW,
                   "DEBUG: id:%d name_server_main: DNS Server received packet "
                   "on port:%d "
                   "src:%d dst:%d\n",
                   name_id, portNum, received_packet->src,
                   received_packet->dst);
        printPacket(received_packet);
#endif

        struct Job *nsJob = job_create_empty();
        nsJob->packet = received_packet;

        // switch statement that differeniates from registration, and query
        switch (received_packet->type) {
          case PKT_DNS_REGISTRATION:
            nsJob->type = JOB_DNS_REGISTER;
            break;
          case PKT_DNS_QUERY:
            nsJob->type = JOB_DNS_QUERY;
            break;
        }
        ////// Enqueues job //////
        nsJob->state = JOB_PENDING_STATE;
        job_enqueue(name_id, &name_q, nsJob);
      } else {
        // Nothing to receive on port, so discard malloc'd packet
        free(received_packet);
        received_packet = NULL;
      }
    }

    ////////////////////////////// PACKET HANDLER //////////////////////////////
    ////////////////////////////////////////////////////////////////////////////
    // -------------------------------------------------------------------------
    ////////////////////////////////////////////////////////////////////////////
    //////////////////////////////// JOB HANDLER ///////////////////////////////

    if (job_queue_length(&name_q) > 0) {
      /* Get a new job from the job queue */
      struct Job *job_from_queue = job_dequeue(name_id, &name_q);

      //////////// EXECUTE FETCHED JOB ////////////
      switch (job_from_queue->type) {
        case JOB_DNS_REGISTER:
          register_name_to_table(job_from_queue->packet, nametable,
                                 node_port_array, node_port_array_size);
          break;
        case JOB_DNS_QUERY:
          // Grab the domain name from the DNS Query
          char dname[PACKET_PAYLOAD_MAX];
          strncpy(dname, job_from_queue->packet->payload, PACKET_PAYLOAD_MAX);

          // Getting id number from nametable with matching name
          int id = retrieve_id_from_table(dname, nametable);

          // Construct a response packet with id result
          struct Packet *queryResponsePkt =
              createPacket(STATIC_DNS_ID, job_from_queue->packet->src,
                           PKT_DNS_QUERY_RESPONSE, 0, NULL);

          // Payload syntax: domainName:id
          snprintf(queryResponsePkt->payload, PACKET_PAYLOAD_MAX, "%s:%d",
                   dname, id);

          // Update length
          queryResponsePkt->length = strlen(queryResponsePkt->payload);

          sendPacketTo2(node_port_array, node_port_array_size,
                        queryResponsePkt);

          free(queryResponsePkt);
          break;
      }

      ////// Empties job queue after completion //////
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
  free(nametable);
}
