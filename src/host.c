/*
  host.c
*/

#include "host.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "color.h"
#include "filebuf.h"
#include "job.h"
#include "manager.h"
#include "net.h"
#include "packet.h"
#include "request.h"
#include "semaphore.h"

int sendPacketTo(struct Net_port **arr, int arrSize, struct Packet *p) {
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
}

////////////////////////////////////////////////
////////////////// HOST MAIN ///////////////////
void host_main(int host_id) {
  ////// Initialize state of host //////
  char hostDirectory[MAX_FILENAME_LENGTH];
  char man_msg[MAX_MSG_LENGTH];
  // char man_reply_msg[MAX_MSG_LENGTH];
  char man_cmd;

  // Initialize file buffer
  struct File_buf f_buf_upload;
  struct File_buf f_buf_download;
  file_buf_init(&f_buf_upload);
  file_buf_init(&f_buf_download);

  /* Initialize pipes, Get link port to the manager */
  struct Man_port_at_host *man_port;  // Port to the manager
  man_port = net_get_host_port(host_id);

  // Initialize node_port_array
  struct Net_port *node_port_list;
  node_port_list = net_get_port_list(host_id);
  /*  Count the number of network link ports */
  struct Net_port **node_port_array;  // Array of pointers to node ports
  int node_port_array_size = 0;       // Number of node ports
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
  struct Job_queue host_q;
  job_queue_init(&host_q);

  /* Initialize request list */
  struct Request *requestList = NULL;

  while (1) {
    ////////////////////////////////////////////////////////////////////////////
    ////////////////////////////// COMMAND HANDLER /////////////////////////////

    int n = get_man_command(man_port, man_msg, &man_cmd);
    /* Execute command */
    if (n > 0) {
      sem_wait(&console_print_access);

      printf("man_msg: %s\n", man_msg);

      switch (man_cmd) {
        case 's':
          // Display host state
          reply_display_host_state(man_port, hostDirectory,
                                   isValidDirectory(hostDirectory), host_id);
          break;
        case 'm':
          // Change Active Host's hostDirectory
          size_t len = strnlen(man_msg, MAX_FILENAME_LENGTH - 1);
          if (isValidDirectory(man_msg)) {
            memcpy(hostDirectory, man_msg, len);
            hostDirectory[len] = '\0';  // add null character
            colorPrint(BOLD_GREEN, "Host%d's main directory set to %s\n",
                       host_id, man_msg);
          } else {
            colorPrint(BOLD_RED, "%s is not a valid directory\n", man_msg);
          }
          break;
        case 'p':
          ////// Have active Host ping another host //////
          // Get destination from man_msg
          int dst;
          sscanf(man_msg, "%d", &dst);
          // Generate a ping request packet
          struct Packet *pingReqPkt = createPacket(host_id, dst, PKT_PING_REQ);
          // Generate a job and put ping request packet inside
          struct Job *pingReqJob = createJob(JOB_SEND_REQUEST, pingReqPkt);
          job_enqueue(host_id, &host_q, pingReqJob);
          break;
        case 'u':
          // Upload a file from active host to another host
          break;
        case 'd':
          // Download a file from another host to active host
          break;
        default:;
      }
      // Release console_print_access semaphore
      sem_signal(&console_print_access);
    }

    ////////////////////////////////////////////////////////////////////////////
    ////////////////////////////// COMMAND HANDLER /////////////////////////////
    // -------------------------------------------------------------------------
    ////////////////////////////////////////////////////////////////////////////
    ////////////////////////////// PACKET HANDLER //////////////////////////////

    for (int portNum = 0; portNum < node_port_array_size; portNum++) {
      // Receive packets for all ports in node_port_array
      struct Packet *received_packet = createEmptyPacket();
      n = packet_recv(node_port_array[portNum], received_packet);

      // if portNum has received a packet, translate the packet into a job
      if ((n > 0) && ((int)received_packet->dst == host_id)) {
#ifdef DEBUG
        colorPrint(YELLOW,
                   "DEBUG: id:%d host_main packet_handler Host %d received "
                   "packet of type "
                   "%s\n",
                   host_id, (int)received_packet->dst,
                   get_packet_type_literal(received_packet->type));
        printPacket(received_packet);
#endif
        struct Job *job_from_pkt = createEmptyJob();
        job_from_pkt->packet = received_packet;

        switch (received_packet->type) {
          case PKT_PING_REQ:
            struct Packet *resPkt = received_packet;
            resPkt->dst = received_packet->src;
            resPkt->src = host_id;
            resPkt->type = PKT_PING_RESPONSE;
            struct Job *resJob = createJob(JOB_SEND_RESPONSE, resPkt);
            job_enqueue(host_id, &host_q, resJob);
            break;

          case PKT_PING_RESPONSE:
            struct Request *r = findRequestByStringTicket(
                requestList, received_packet->payload);
            if (r != NULL && r->state) {
              r->state = COMPLETE;
            }
            free(received_packet);
            break;

          default:
        }
      } else {
      }
    }

    ////////////////////////////// PACKET HANDLER //////////////////////////////
    ////////////////////////////////////////////////////////////////////////////
    // -------------------------------------------------------------------------
    ////////////////////////////////////////////////////////////////////////////
    //////////////////////////////// JOB HANDLER ///////////////////////////////

    if (job_queue_length(&host_q) > 0) {
      /* Get a new job from the job queue */
      struct Job *job_from_queue = job_dequeue(host_id, &host_q);

      //////////// EXECUTE FETCHED JOB ////////////
      switch (job_from_queue->type) {
        case JOB_SEND_REQUEST:
          // Generate a request
          struct Request *req = createRequest(PING_REQ, TIMETOLIVE);
          // Add request to requestList
          addToReqList(&requestList, req);

          // Include request ticket inside request packet
          int reqTicket = req->ticket;
          sprintf(job_from_queue->packet->payload, "%d", reqTicket);
          job_from_queue->packet->length =
              strlen(job_from_queue->packet->payload);
          // Enqueue a JOB_SEND_PKT to send to destination
          struct Job *sendJob = createJob(JOB_SEND_PKT, job_from_queue->packet);
          job_enqueue(host_id, &host_q, sendJob);
          // Enqueue a JOB_WAIT_FOR_RESPONSE with the Request
          struct Job *waitJob = createJob(JOB_WAIT_FOR_RESPONSE, NULL);
          waitJob->request = req;
          job_enqueue(host_id, &host_q, waitJob);
          break;

        case JOB_SEND_RESPONSE:
          struct Job *res = createJob(JOB_SEND_PKT, job_from_queue->packet);
          job_enqueue(host_id, &host_q, res);
          break;

        case JOB_SEND_PKT:
          sendPacketTo(node_port_array, node_port_array_size,
                       job_from_queue->packet);
          free(job_from_queue->packet);
          break;

        case JOB_WAIT_FOR_RESPONSE:
          struct Request *r = job_from_queue->request;
          printf("State: %s\n", r->state == 0 ? "PENDING" : "COMPLETE");
          if (job_from_queue->request->state == COMPLETE) {
            printf("Complete\n");
            const char *repMsg = "\x1b[32;1mPing acknowledged!\x1b[0m";
            write(man_port->send_fd, repMsg, strlen(repMsg));
            deleteFromReqList(requestList, job_from_queue->request->ticket);
          } else {
            if (job_from_queue->request->timeToLive > 0) {
              job_from_queue->request->timeToLive--;
              struct Job *waitJob = createJob(JOB_WAIT_FOR_RESPONSE, NULL);
              waitJob->request = job_from_queue->request;
              job_enqueue(host_id, &host_q, waitJob);
            } else {
              printf("Timed out\n");

              const char *repMsg = "\x1b[31;1mPing timed out!\x1b[0m";
              write(man_port->send_fd, repMsg, strlen(repMsg));
              deleteFromReqList(requestList, job_from_queue->request->ticket);
            }
          }
          break;

        default:
#ifdef DEBUG
          colorPrint(YELLOW,
                     "DEBUG: id:%d host_main: job_handler defaulted with "
                     "job type: "
                     "%s\n",
                     host_id, get_job_type_literal(job_from_queue->type));
#endif
      }
      if (job_from_queue != NULL) {
        free(job_from_queue);
        job_from_queue = NULL;
      }
    }

    //////////////////////////////// JOB HANDLER ///////////////////////////////
    ////////////////////////////////////////////////////////////////////////////

    /* The host goes to sleep for 10 ms */
    usleep(LOOP_SLEEP_TIME_MS);

  } /* End of while loop */
}
