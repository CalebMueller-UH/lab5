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

// Used for searchRoutingTableForValidID when port is unknown
#define UNKNOWN -1
#define YES 1
#define NO 0

struct SwitchNodeContext {
  int _id;
  struct Net_port **node_port_array;
  int node_port_array_size;
  struct JobQueue **jobq;
  struct TableEntry **routingtable;
  int localRootID;
  int localRootDist;
  int localParentID;
  int *localPortTree;
};

void addToRoutingTable(struct SwitchNodeContext *sw, int id, int port);
void broadcastToAllButSender(struct SwitchNodeContext *sw, struct Job *job);
int createTreePayload(char *dst, int packetRootID, int packetRootDist,
                      char packetSenderType, char packetIsSenderChild);
void handleControlPacket(struct SwitchNodeContext *sw, const int receivePort,
                         struct Packet *pkt);
struct SwitchNodeContext *initSwitchNodeContext(int switch_id);
int searchRoutingTableForValidID(struct SwitchNodeContext *sw, int id,
                                 int port);
int setLocalPortTreeState(struct SwitchNodeContext *sw, int portToSet,
                          int stateToSet);

void switch_main(int switch_id) {
  // Initialize Switch State
  struct SwitchNodeContext *sw = initSwitchNodeContext(switch_id);

  while (1) {
    ////////////////////////////////////////////////////////////////////////////
    ////////////////////////////// PACKET HANDLER //////////////////////////////

    // Periodically broadcast STP Control Packets
    periodicControlPacketSender(
        sw->_id, sw->node_port_array, sw->node_port_array_size, sw->localRootID,
        sw->localRootDist, sw->localParentID, sw->localPortTree, 'S');

    for (int portNum = 0; portNum < sw->node_port_array_size; portNum++) {
      struct Packet *inPkt = (struct Packet *)malloc(sizeof(struct Packet));
      int n = packet_recv(sw->node_port_array[portNum], inPkt);

      if (n > 0) {
        // Packet was received on port

        switch (inPkt->type) {
          case PKT_CONTROL:
            handleControlPacket(sw, portNum, inPkt);
            packet_delete(inPkt);
            break;

          default:
#ifdef SWITCH_DEBUG_PACKET_RECEIPT
            colorPrint(BLUE,
                       "Switch%d received packet on port:%d src:%d dst:%d\n",
                       sw->_id, portNum, inPkt->src, inPkt->dst);
#endif
            // Ensure that sender of received packet is in the routing table
            if (searchRoutingTableForValidID(sw, inPkt->src, portNum) ==
                UNKNOWN) {
              // Sender was not found in routing table
              addToRoutingTable(sw, inPkt->src, portNum);
            }

            // Create a job to enqueue with work
            struct Job *swJob = job_create_empty();
            swJob->packet = inPkt;

            // Search for destination in routing table
            int dstIndex =
                searchRoutingTableForValidID(sw, inPkt->dst, UNKNOWN);
            if (dstIndex < 0) {
              // destination of received packet is not in routing table...
              // enqueue job to broadcast packet to all connected hosts
              swJob->type = JOB_BROADCAST_PKT;
              job_enqueue(sw->_id, *sw->jobq, swJob);

            } else {
              // destination of received packet has been found in routing
              // table... enqueue job to forward packet to the associated port
              swJob->type = JOB_FORWARD_PKT;
              job_enqueue(sw->_id, *sw->jobq, swJob);
            }
        }

      } else {
        // No packet was received on port, so discard packet
        packet_delete(inPkt);
      }
    }

    ////////////////////////////// PACKET HANDLER //////////////////////////////
    ////////////////////////////////////////////////////////////////////////////
    // -------------------------------------------------------------------------
    ////////////////////////////////////////////////////////////////////////////
    //////////////////////////////// JOB HANDLER ///////////////////////////////

    if (job_queue_length(*sw->jobq) > 0) {
      /* Get a new job from the job queue */
      struct Job *job_from_queue = job_dequeue(sw->_id, *sw->jobq);

      //////////// EXECUTE FETCHED JOB ////////////
      switch (job_from_queue->type) {
        case JOB_BROADCAST_PKT:
          broadcastToAllButSender(sw, job_from_queue);
          break;

        case JOB_FORWARD_PKT:
          int dstPort = searchRoutingTableForValidID(
              sw, job_from_queue->packet->dst, UNKNOWN);
          packet_send(sw->node_port_array[dstPort], job_from_queue->packet);
          break;

        default:
          fprintf(stderr,
                  "Switch%d's Job Handler encountered an unknown job type\n",
                  sw->_id);
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

}  // End of switch_main()

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////// HELPER FUNCTIONS ////////////////////////////////

void addToRoutingTable(struct SwitchNodeContext *sw, int id, int port) {
#ifdef SWITCH_DEBUG
  colorPrint(BLUE, "\tSwitch%d: Adding node_id%d to routing table on port%d\n",
             sw->_id, id, port);
#endif
  struct TableEntry *newEntry =
      malloc(sizeof(struct TableEntry));  // Memory allocation added here
  newEntry->id = id;
  newEntry->next = sw->routingtable[port];
  sw->routingtable[port] = newEntry;  // Assignment changed to address of new
                                      // entry instead of its value
}  // End of addToRoutingTable()

/*
This function takes a job struct , a table entry, an array of Net_port
pointers and the size of the array as parameters. It then iterates through the
port_array and sends the job's packet to each port in the array, except for the
port that corresponds to the job->packet->src
*/
void broadcastToAllButSender(struct SwitchNodeContext *sw, struct Job *job) {
  int senderPort = searchRoutingTableForValidID(sw, job->packet->src, UNKNOWN);
  if (senderPort < 0) {
    fprintf(
        stderr,
        "Error: broadcastToAllButSender unable to find value for senderPort\n");
    return;
  }

  // Iterate through all connected ports
  // broadcast packet to all except senderPort
  for (int i = 0; i < sw->node_port_array_size; i++) {
    if (i != senderPort && sw->localPortTree[i] == YES) {
      packet_send(sw->node_port_array[i], job->packet);
      printf("switch%d sending to port%d\n", sw->_id, i);
    }
  }
}  // End of broadcastToAllButSender

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

void handleControlPacket(struct SwitchNodeContext *sw, const int receivePort,
                         struct Packet *pkt) {
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

#ifdef SWITCH_DEBUG_CONTROL_MSG
  colorPrint(
      BLUE,
      "\tSwitch%d packetRootID:%d packetRootDist:%d packetSenderType:%c, "
      "packetIsSenderChild:%c\n",
      sw->_id, packetRootID, packetRootDist, *packetSenderType,
      *packetIsSenderChild);
#endif

  // Update localRootID, localRootDist, and localParent
  if (*packetSenderType == 'S') {
    if (packetRootID < sw->localRootID) {
#ifdef SWITCH_DEBUG
      colorPrint(RED, "\t\tSwitch%d's localRootID updated from %d to %d\n",
                 sw->_id, sw->localRootID, packetRootID);
#endif
      sw->localRootID = packetRootID;
      sw->localParentID = receivePort;
      sw->localRootDist = packetRootDist + 1;
    } else if (packetRootID == sw->localRootID) {
      if (sw->localRootDist > packetRootDist + 1 ||
          (sw->localRootDist == packetRootDist + 1 &&
           receivePort < sw->localParentID)) {
        sw->localParentID = receivePort;
        sw->localRootDist = packetRootDist + 1;
      }
    }
  }

  // Update status of receivePort whether it's the tree or not
  if (*packetSenderType == 'H' || *packetSenderType == 'D') {
    if (sw->localPortTree[receivePort] != YES) {
      setLocalPortTreeState(sw, receivePort, YES);
    }

  } else if (*packetSenderType == 'S') {
    if (*packetIsSenderChild == 'Y') {
      if (sw->localPortTree[receivePort] != YES) {
        setLocalPortTreeState(sw, receivePort, YES);
      }
    } else {
      if (sw->localPortTree[receivePort] != NO) {
        // Tie-breaker: Prioritize lower node IDs when distances are equal
        if (packetRootDist == sw->localRootDist - 1 && pkt->src < sw->_id) {
          setLocalPortTreeState(sw, receivePort, YES);
        } else {
          setLocalPortTreeState(sw, receivePort, NO);
        }
      }
    }
  }

  else {
    if (sw->localPortTree[receivePort] != NO) {
      setLocalPortTreeState(sw, receivePort, NO);
    }
  }
}  // End of handleControlPacket()

struct SwitchNodeContext *initSwitchNodeContext(int switch_id) {
  ////// Initialize state of switch //////
  struct SwitchNodeContext *sw =
      (struct SwitchNodeContext *)malloc(sizeof(struct SwitchNodeContext));

  // Set switch id
  sw->_id = switch_id;

  // Initialize node_port_array
  struct Net_port *node_port_list;
  node_port_list = net_get_port_list(switch_id);

  /*  Count the number of network link ports */
  sw->node_port_array_size = 0;
  for (struct Net_port *p = node_port_list; p != NULL; p = p->next) {
    sw->node_port_array_size++;
  }

  /* Create memory space for the array */
  sw->node_port_array = (struct Net_port **)malloc(sw->node_port_array_size *
                                                   sizeof(struct Net_port *));

  /* Load ports into the array */
  {
    struct Net_port *p = node_port_list;
    for (int portNum = 0; portNum < sw->node_port_array_size; portNum++) {
      sw->node_port_array[portNum] = p;
      p = p->next;
    }
  }

  /* Initialize the job queue */
  sw->jobq = (struct JobQueue **)malloc(sizeof(struct JobQueue *));
  *sw->jobq = (struct JobQueue *)malloc(sizeof(struct JobQueue));
  job_queue_init(*sw->jobq);

  ////// Initialize Router Table //////
  sw->routingtable = (struct TableEntry **)malloc(sizeof(struct TableEntry *) *
                                                  MAX_NUM_ROUTES);

  for (int i = 0; i < MAX_NUM_ROUTES; i++) {
    sw->routingtable[i] = NULL;
  }

  ////// Initialize spanning tree variables //////
  sw->localRootID = switch_id;
  sw->localRootDist = 0;
  sw->localParentID = -1;
  sw->localPortTree = malloc(sizeof(int) * (MAX_ADDRESS + 1));
  for (int i = 0; i <= MAX_ADDRESS; i++) {
    sw->localPortTree[i] = NO;
  }

  return sw;
}  // End of initSwitchNodeContext()

void periodicControlPacketSender(int id, struct Net_port **node_port_array,
                                 int node_port_array_size, int localRootID,
                                 int localRootDist, int localParentID,
                                 int *localPortTree, const char nodeType) {
  static time_t timeLast = 0;
  time_t timeNow = time(NULL);

  if (timeNow - timeLast > PERIODIC_CTRL_MSG_WAITTIME_SEC) {
    // For each connected port
    for (int port = 0; port < node_port_array_size; port++) {
      // Create a STP packet payload
      char ctrlPayload[PACKET_PAYLOAD_MAX];
      int ctrlPayloadLen =
          createTreePayload(ctrlPayload, localRootID, localRootDist, nodeType,
                            (localParentID == port) ? 'Y' : 'N');

      // Create a STP control packet
      struct Packet *ctrlPkt =
          createPacket(id, 255, PKT_CONTROL, ctrlPayloadLen, ctrlPayload);
      packet_send(node_port_array[port], ctrlPkt);
    }

    timeLast = timeNow;
  }
}  // End of periodicControlPacketSender()

// Searches the routing table for a matching valid TableEntry matching id
// Returns routing table index of valid id, or -1 if unsuccessful
//  **Note that routing table index is the port
int searchRoutingTableForValidID(struct SwitchNodeContext *sw, int id,
                                 int port) {
#ifdef SWITCH_DEBUG_ROUTINGTABLE
  char portMsg[100];
  if (port == UNKNOWN) {
    snprintf(portMsg, 100, "unknown port");
  } else {
    snprintf(portMsg, 100, "port%d", port);
  }
  colorPrint(BLUE, "Switch%d: Searching routing table for host%d on %s\n",
             sw->_id, id, portMsg);
#endif

  if (port == UNKNOWN) {
    // Port was not given...
    // Scan through the indices (ports) of the routing table
    for (int i = 0; i < MAX_NUM_ROUTES; i++) {
      // If there are entries for that port
      if (sw->routingtable[i] != NULL) {
        struct TableEntry *t = sw->routingtable[i];
        while (t != NULL) {
          if (t->id == id) {
#ifdef SWITCH_DEBUG_ROUTINGTABLE
            colorPrint(BLUE, "\tSwitch%d Found host%d on port%d\n", sw->_id, id,
                       i);
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
    if (sw->routingtable[port] != NULL) {
      struct TableEntry *t = sw->routingtable[port];
      while (t != NULL) {
        if (t->id == id) {
          return port;
        }
        t = t->next;
      }
    }
  }

#ifdef SWITCH_DEBUG_ROUTINGTABLE
  colorPrint(BLUE, "\tUnable to find host%d in routing table\n", id);
#endif

  return -1;
}  // End of searchRoutingTableForValidID()

int setLocalPortTreeState(struct SwitchNodeContext *sw, int portToSet,
                          int stateToSet) {
  color c;
  char stateLiteral[4];

  if (stateToSet == NO) {
    c = RED;
    strncpy(stateLiteral, "NO", sizeof(3));
  } else if (stateToSet == YES) {
    c = GREEN;
    strncpy(stateLiteral, "YES", sizeof(4));
  } else {
    fprintf(stderr,
            "Switch%d attempted to setLocalPortTreeState with illegal "
            "stateToSet value\n",
            sw->_id);
    return -1;
  }

  sw->localPortTree[portToSet] = stateToSet;

#ifdef SWITCH_DEBUG_CONTROL_UPDATE
  colorPrint(c, "\tSwitch%d updating localPortTree[%d]=%s\n", sw->_id,
             portToSet, stateLiteral);
#endif
}
