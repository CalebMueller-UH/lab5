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

#ifdef DEBUG
#define NAMESERVERDEBUG
#endif

// Used for registerNameToTable when ID can't be found
#define UNKNOWN -1

struct NameServerContext {
  int _id;
  struct JobQueue **jobq;
  struct Net_port **node_port_array;
  int node_port_array_size;
  struct Net_port *node_port_list;
  char **nametable;
};

struct NameServerContext *initNameServerContext(int name_id);
int registerNameToTable(struct NameServerContext *nsc, struct Packet *pkt);
int retrieveIdFromTable(struct NameServerContext *nsc, struct Packet *pkt);
int sendPacketTo2(struct NameServerContext *nsc, struct Packet *p);

////////////////////////////////
/////// NAME SERVER MAIN ///////
void name_server_main(int name_id) {
  ////// Initialize Name Server //////
  struct NameServerContext *nsc = initNameServerContext(name_id);

  while (1) {
    ////////////////////////////////////////////////////////////////////////////
    ////////////////////////////// PACKET HANDLER //////////////////////////////

    for (int portNum = 0; portNum < nsc->node_port_array_size; portNum++) {
      struct Packet *received_packet =
          (struct Packet *)malloc(sizeof(struct Packet));
      int n = packet_recv(nsc->node_port_array[portNum], received_packet);
      if (n > 0) {
#ifdef NAMESERVERDEBUG
        colorPrint(
            GREY,
            "\nDEBUG: id:%d name_server_main: DNS Server received packet "
            "on port:%d "
            "src:%d dst:%d\n",
            nsc->_id, portNum, received_packet->src, received_packet->dst);
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
          default:
            // fprintf(stderr, "DNS Server received a packet of unknown
            // type\n");
        }
        ////// Enqueues job //////
        nsJob->state = JOB_PENDING_STATE;
        job_enqueue(name_id, *nsc->jobq, nsJob);
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

    if (job_queue_length(*nsc->jobq) > 0) {
      /* Get a new job from the job queue */
      struct Job *job_from_queue = job_dequeue(name_id, *nsc->jobq);

      // Allow shorthand alias for Job Handler scope
      struct Packet *pkt = job_from_queue->packet;

      //////////// EXECUTE FETCHED JOB ////////////
      switch (job_from_queue->type) {
        case JOB_DNS_REGISTER: {
          int regSuccess = registerNameToTable(nsc, pkt);

          // Repurpose the job_from_queue->packet for the response //
          char prefix[JIDLEN + 2] = {0};
          for (int i = 0; i < JIDLEN + 1; i++) {
            // grab Job ID and demarcator for response
            prefix[i] = pkt->payload[i];
          }
          char remsg[PACKET_PAYLOAD_MAX];
          snprintf(remsg, PACKET_PAYLOAD_MAX, "%s%s", prefix,
                   (regSuccess < 0) ? "FAILED" : "OK");
          pkt->dst = pkt->src;
          pkt->src = STATIC_DNS_ID;
          pkt->type = PKT_DNS_REGISTRATION_RESPONSE;
          pkt->length = strnlen(remsg, PACKET_PAYLOAD_MAX);
          strncpy(job_from_queue->packet->payload, remsg, PACKET_PAYLOAD_MAX);

          // Repurpose the job_from_queue job for the response
          job_from_queue->type = JOB_SEND_PKT;
          job_enqueue(nsc->_id, *nsc->jobq, job_from_queue);
          break;
        }
        case JOB_DNS_QUERY: {
          int resolvedId = retrieveIdFromTable(nsc, pkt);

          // Repurpose the job_from_queue->packet for the response //
          char prefix[JIDLEN + 2] = {0};
          for (int i = 0; i < JIDLEN + 1; i++) {
            // grab Job ID and demarcator for response
            prefix[i] = pkt->payload[i];
          }
          char remsg[PACKET_PAYLOAD_MAX] = {0};
          snprintf(remsg, PACKET_PAYLOAD_MAX, "%s%d", prefix, resolvedId);
          pkt->dst = pkt->src;
          pkt->src = STATIC_DNS_ID;
          pkt->type = PKT_DNS_QUERY_RESPONSE;
          pkt->length = strnlen(remsg, PACKET_PAYLOAD_MAX);
          strncpy(job_from_queue->packet->payload, remsg, PACKET_PAYLOAD_MAX);

          // Repurpose the job_from_queue job for the response
          job_from_queue->type = JOB_SEND_PKT;
          job_enqueue(nsc->_id, *nsc->jobq, job_from_queue);

          break;
        }

        case JOB_SEND_PKT: {
          sendPacketTo2(nsc, job_from_queue->packet);
          job_delete(nsc->_id, job_from_queue);
        }
      }
    }

    //////////////////////////////// JOB HANDLER ///////////////////////////////
    ////////////////////////////////////////////////////////////////////////////

    /* The host goes to sleep for 10 ms */
    usleep(LOOP_SLEEP_TIME_US);
  } /* End of while loop */
}  // End of name_server_main()

//////////////////////////////////////////////////////////////////////////
/////////////////////// HELPER FUNCTIONS /////////////////////////////////

struct NameServerContext *initNameServerContext(int name_id) {
  struct NameServerContext *name_context =
      (struct NameServerContext *)malloc(sizeof(struct NameServerContext));

  name_context->_id = name_id;

  name_context->node_port_list = net_get_port_list(name_id);

  name_context->node_port_array_size = 0;
  for (struct Net_port *p = name_context->node_port_list; p != NULL;
       p = p->next) {
    name_context->node_port_array_size++;
  }

  name_context->node_port_array = (struct Net_port **)malloc(
      name_context->node_port_array_size * sizeof(struct Net_port *));
  {
    struct Net_port *p = name_context->node_port_list;
    for (int portNum = 0; portNum < name_context->node_port_array_size;
         portNum++) {
      name_context->node_port_array[portNum] = p;
      p = p->next;
    }
  }

  // Allocate memory for the JobQueue struct
  name_context->jobq = (struct JobQueue **)malloc(sizeof(struct JobQueue *));
  *name_context->jobq = (struct JobQueue *)malloc(sizeof(struct JobQueue));

  // Initialize the JobQueue struct
  job_queue_init(*name_context->jobq);

  name_context->nametable = malloc((MAX_NUM_NAMES + 1) * sizeof(char *));
  init_nametable(name_context->nametable);

  return name_context;
}  // End of initNameServerContext()

void init_nametable(char **nametable) {
  for (int i = 0; i <= MAX_NUM_NAMES; i++) {
    nametable[i] = malloc(PACKET_PAYLOAD_MAX + 1);
    nametable[i][0] = '\0';
  }
}  // End of init_nametable()

int registerNameToTable(struct NameServerContext *nsc, struct Packet *pkt) {
  int index = pkt->src;
  int length = pkt->length;

  if (index < 0 || index >= PACKET_PAYLOAD_MAX || length <= 0) {
    // Invalid input
    fprintf(stderr,
            "nameServer registerNameToTable encountered invalid index\n");
    return -1;
  }

  // grab domain name from payload
  char dname[MAX_NAME_LEN];
  int dnameLen = 0;
  for (dnameLen = 0; dnameLen < MAX_NAME_LEN; dnameLen++) {
    // Don't copy prependid Job ID or ':' demarcator
    if (pkt->payload[JIDLEN + dnameLen] == '\0') {
      break;
    }
    dname[dnameLen] = pkt->payload[dnameLen + JIDLEN + 1];
  }

  dname[dnameLen] = '\0';

  // Update nametable to [src]::domainName
  strncpy(nsc->nametable[pkt->src], dname, dnameLen);

#ifdef NAMESERVERDEBUG
  colorPrint(BOLD_GREY, "\t%s was registered to host%d\n",
             nsc->nametable[pkt->src], pkt->src);
#endif
  return 0;
}  // End of registerNameToTable()

int retrieveIdFromTable(struct NameServerContext *nsc, struct Packet *pkt) {
  int nameLen = strnlen(pkt->payload, MAX_NAME_LEN);
  // grab dname from pkt->payload
  char dname[MAX_NAME_LEN] = {0};
  for (int i = 0; i < nameLen; i++) {
    if (pkt->payload[i + JIDLEN] == '\0') {
      break;
    }
    dname[i] = pkt->payload[i + JIDLEN + 1];
  }

  for (int i = 0; i < MAX_NUM_NAMES; i++) {
    if (strncmp(dname, nsc->nametable[i], PACKET_PAYLOAD_MAX) == 0) {
      printf("found a match! %d\n", i);
      return i;
    }
  }
  // name wasn't found; return error
  return -1;
}  // End of retrieveIdFromTable()

int sendPacketTo2(struct NameServerContext *nsc, struct Packet *p) {
  // Find which Net_port entry in net_port_array has desired destination
  int destIndex = -1;
  for (int i = 0; i < nsc->node_port_array_size; i++) {
    if (nsc->node_port_array[i]->link_node_id == p->dst) {
      destIndex = i;
    }
  }
  // If node_port_array had the destination id, send to that node
  if (destIndex >= 0) {
    packet_send(nsc->node_port_array[destIndex], p);
  } else {
    // Else, broadcast packet to all connected hosts
    for (int i = 0; i < nsc->node_port_array_size; i++) {
      packet_send(nsc->node_port_array[i], p);
    }
  }
}  // End of sendPacketTo2