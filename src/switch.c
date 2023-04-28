/*
    switch.c
*/

#include "switch.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "color.h"
#include "constants.h"
#include "debug.h"
#include "job.h"
#include "net.h"
#include "packet.h"

#define MAX_NUM_ROUTES 100

#define MAX_ADDRESS 255

#define PERIODIC_LOCALROOTID_WAITTIME_SEC 1

// Used for searchRoutingTableForValidID when port is unknown
#define UNKNOWN -1
#define YES 1
#define NO 0

// Searches the routing table for a matching valid TableEntry matching id
// Returns routing table index of valid id, or -1 if unsuccessful
//  **Note that routing table index is the port
int searchRoutingTableForValidID(struct TableEntry **rt, int id, int port) {
#ifdef SWITCH_DEBUG
  char portMsg[100];
  if (port == UNKNOWN) {
    snprintf(portMsg, 100, "unknown port");
  } else {
    snprintf(portMsg, 100, "port%d", port);
  }
  colorPrint(BLUE, "Searching routing table for host%d on %s\n", id, portMsg);
#endif

  if (port == UNKNOWN) {
    // Port was not given...
    // Scan through the indices (ports) of the routing table
    for (int i = 0; i < MAX_NUM_ROUTES; i++) {
      // If there are entries for that port
      if (rt[i] != NULL) {
        struct TableEntry *t = rt[i];
        while (t != NULL) {
          if (t->id == id) {
#ifdef SWITCH_DEBUG
            colorPrint(BLUE, "\tFound host%d on port%d\n", id, i);
#endif
            return i;
          }
          t = t->next;
        }
      }
    }
  } else {
    // Port was specified...
    // Look in the routing table at that port for a matching id
    if (rt[port] != NULL) {
      struct TableEntry *t = rt[port];
      while (t != NULL) {
        if (t->id == id) {
          return port;
        }
        t = t->next;
      }
    }
  }

#ifdef SWITCH_DEBUG
  colorPrint(BLUE, "\tUnable to find host%d in routing table\n", id);
#endif

  return -1;
}

void addToRoutingTable(struct TableEntry **rt, int id, int port) {
#ifdef SWITCH_DEBUG
  colorPrint(BLUE, "\tAdding host%d to routing table on port%d\n", id, port);
#endif
  struct TableEntry *newEntry =
      malloc(sizeof(struct TableEntry));  // Memory allocation added here
  newEntry->id = id;
  newEntry->next = rt[port];
  rt[port] = newEntry;  // Assignment changed to address of new entry instead
                        // of its value
}

/*
This function takes a job struct , a table entry, an array of Net_port
pointers and the size of the array as parameters. It then iterates through the
port_array and sends the job's packet to each port in the array, except for the
port that corresponds to the job->packet->src
*/
void broadcastToAllButSender(struct Job *job, struct TableEntry **rt,
                             struct Net_port **port_array, int port_array_size,
                             int *localPortTree) {
  int senderPort = searchRoutingTableForValidID(rt, job->packet->src, UNKNOWN);

  if (senderPort < 0) {
    fprintf(
        stderr,
        "Error: broadcastToAllButSender unable to find value for senderPort\n");
    return;
  }

  // Iterate through all connected ports
  // broadcast packet to all except senderPort
  for (int i = 0; i < port_array_size; i++) {
    // Only send packet to nodes within the tree (localPortTree[portNum]=YES)
    if (i != senderPort && localPortTree[i] == YES) {
      packet_send(port_array[i], job->packet);
    }
  }
}

int createTreePayload(char *dst, int packetRootID, int packetRootDist,
                      char packetSenderType, char packetIsSenderChild) {
  // Calculate the total length of the payload string
  int payloadLength =
      snprintf(NULL, 0, "9999:%d:%d:%c:%c", packetRootID, packetRootDist,
               packetSenderType, packetIsSenderChild);

  // Create the payload string
  snprintf(dst, payloadLength + 1, "9999:%d:%d:%c:%c", packetRootID,
           packetRootDist, packetSenderType, packetIsSenderChild);

  // Return the length of the payload string
  return payloadLength;
}  // End of createTreePayload()

void periodicTreePacketSender(struct Net_port **arr, const int arrSize,
                              const int switch_id, const int localRootID,
                              const int localRootDist,
                              const int localParentID) {
  static time_t timeLast = 0;
  time_t timeNow = time(NULL);
  if (timeNow - timeLast > PERIODIC_LOCALROOTID_WAITTIME_SEC) {
    // #ifdef SWITCH_DEBUG
    //     colorPrint(BLUE, "Sending Compare Packet\n");
    // #endif

    // Broadcast packet to all connected hosts
    for (int i = 0; i < arrSize; i++) {
      // Create a string to write to the comparrison packet payload
      char compPayload[PACKET_PAYLOAD_MAX];
      int compPayloadLen =
          createTreePayload(compPayload, localRootID, localRootDist, 'S',
                            (localParentID == i) ? 'Y' : 'N');

      // Create a localRootID comparrison packet
      struct Packet *compPkt = createPacket(switch_id, 255, PKT_TREE_PKT,
                                            compPayloadLen, compPayload);
      packet_send(arr[i], compPkt);
    }
    timeLast = timeNow;
  }
}  // End of periodicTreePacketSender()

void handleTreePacket(const int receivePort, struct Packet *pkt,
                      const int switch_id, int *localRootID, int *localRootDist,
                      int *localParentID, int *localPortTree) {
  char *payload = pkt->payload;

  /* Parsing Packet for various fields */
  // Find the start of the localRootID field
  char *packetRootID_str = payload + JIDLEN + 1;
  // Convert the localRootID field string to an integer
  int packetRootID = atoi(packetRootID_str);

  // Find the start of the localRootDist field
  char *packetRootDist_str = strchr(packetRootID_str, ':') + 1;
  // Convert the localRootDist field string to an integer
  int packetRootDist = atoi(packetRootDist_str);

  // Find the start of the senderType field
  char *packetSenderType = strchr(packetRootDist_str, ':') + 1;

  // Find the start of the isSenderChild field
  char *packetIsSenderChild = strchr(packetSenderType, ':') + 1;

#ifdef SWITCH_DEBUG
  colorPrint(BLUE, "Switch%d received Tree Packet:\n", switch_id);
  colorPrint(BLUE,
             "\t packetRootID:%d packetRootDist:%d packetSenderType:%c, "
             "packetIsSenderChild:%c\n",
             packetRootID, packetRootDist, *packetSenderType,
             *packetIsSenderChild);
#endif

  // Update localRootID, localRootDist, and localParent
  if (*packetSenderType == 'S') {
    if (packetRootID < *localRootID) {
#ifdef SWITCH_DEBUG
      colorPrint(BLUE, "\t\tSwitch%d's localRootID updated from %d to %d\n",
                 switch_id, *localRootID, packetRootID);
#endif
      *localRootID = packetRootID;
      *localParentID = receivePort;
      *localRootDist = packetRootDist + 1;
    } else if (packetRootID == *localRootID) {
#ifdef SWITCH_DEBUG
      colorPrint(BLUE,
                 "\t\tSwitch%d's localRootID is equal to the received "
                 "packetRootID (%d)\n",
                 switch_id, packetRootID);
#endif
      if (*localRootDist > packetRootDist + 1) {
        *localParentID = receivePort;
        *localRootDist = packetRootDist + 1;
      }
    }
  }

  // Update status of receivePort whether it's the tree or not
  if (*packetSenderType == 'H') {
    localPortTree[receivePort] = YES;
  } else if (*packetSenderType == 'S') {
    if (*localParentID == receivePort) {
      localPortTree[receivePort] = YES;
    } else if (*packetIsSenderChild == 'Y') {
      localPortTree[receivePort] = YES;
    } else {
      if (localPortTree[receivePort] != NO) {
#ifdef SWITCH_DEBUG
        colorPrint(BOLD_RED, "Switch%d updating localPortTree[%d]=NO (msg1)\n",
                   switch_id, receivePort);
#endif
        localPortTree[receivePort] = NO;
      }
    }

  } else {
    if (localPortTree[receivePort] != NO) {
#ifdef SWITCH_DEBUG
      colorPrint(BOLD_RED, "Switch%d updating localPortTree[%d]=NO (msg2)\n",
                 switch_id, receivePort);
#endif
      localPortTree[receivePort] = NO;
    }
  }

  // Assign the localRootDist and isSenderChild variables
  *localRootDist = packetRootDist;
  *localParentID = (*packetIsSenderChild == 'Y') ? switch_id : packetRootID;
}  // End of handleTreePacket()

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
  struct TableEntry **routingTable = (struct TableEntry **)malloc(
      sizeof(struct TableEntry *) * MAX_NUM_ROUTES);
  for (int i = 0; i < MAX_NUM_ROUTES; i++) {
    routingTable[i] = NULL;
  }

  ////// Initialize spanning tree variables //////
  int localRootID = switch_id;
  int localRootDist = 0;
  int localParentID = -1;
  int localPortTree[MAX_ADDRESS];
  for (int i = 0; i <= MAX_ADDRESS; i++) {
    localPortTree[i] = YES;
  }

  while (1) {
    ////////////////////////////////////////////////////////////////////////////
    ////////////////////////////// PACKET HANDLER //////////////////////////////

    // Periodically broadcast localRootID Comparrison Packets
    periodicTreePacketSender(node_port_array, node_port_array_size, switch_id,
                             localRootID, localRootDist, localParentID);

    for (int portNum = 0; portNum < node_port_array_size; portNum++) {
      struct Packet *received_packet =
          (struct Packet *)malloc(sizeof(struct Packet));
      int n = packet_recv(node_port_array[portNum], received_packet);
      if (n > 0) {
#ifdef SWITCH_DEBUG
        colorPrint(YELLOW,
                   "SWITCH_DEBUG: id:%d switch_main: Switch received packet on "
                   "port:%d "
                   "src:%d dst:%d\n",
                   switch_id, portNum, received_packet->src,
                   received_packet->dst);
#endif

        struct Job *swJob = job_create_empty();
        swJob->packet = received_packet;

        if (received_packet->type == PKT_TREE_PKT) {
          // compare the received localRootID of the received packet against
          // the local value, and update if the received value is lower in
          // value
          handleTreePacket(portNum, received_packet, switch_id, &localRootID,
                           &localRootDist, &localParentID, localPortTree);
        } else {
          // Ensure that sender of received packet is in the routing table
          if (searchRoutingTableForValidID(routingTable, received_packet->src,
                                           portNum) == UNKNOWN) {
            // Sender was not found in routing table
            addToRoutingTable(routingTable, received_packet->src, portNum);
          }

          // Search for destination in routing table
          int dstIndex = searchRoutingTableForValidID(
              routingTable, received_packet->dst, UNKNOWN);
          if (dstIndex < 0) {
            // destination of received packet is not in routing table...
            // enqueue job to broadcast packet to all connected hosts
            swJob->type = JOB_BROADCAST_PKT;
            job_enqueue(switch_id, &switch_q, swJob);

          } else {
            // destination of received packet has been found in routing
            // table... enqueue job to forward packet to the associated port
            swJob->type = JOB_FORWARD_PKT;
            job_enqueue(switch_id, &switch_q, swJob);
          }
        }

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

    if (job_queue_length(&switch_q) > 0) {
      /* Get a new job from the job queue */
      struct Job *job_from_queue = job_dequeue(switch_id, &switch_q);

      //////////// EXECUTE FETCHED JOB ////////////
      switch (job_from_queue->type) {
        case JOB_BROADCAST_PKT:
          broadcastToAllButSender(job_from_queue, routingTable, node_port_array,
                                  node_port_array_size, localPortTree);
          break;
        case JOB_FORWARD_PKT:
          int dstPort = searchRoutingTableForValidID(
              routingTable, job_from_queue->packet->dst, UNKNOWN);
          if (localPortTree[dstPort] == YES) {
            packet_send(node_port_array[dstPort], job_from_queue->packet);
          }
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
