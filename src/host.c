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

// The number of payload space available after including a response ticket,
// delimiter, and terminator
int MAX_RESPONSE_LEN = PACKET_PAYLOAD_MAX - 2 - TICKETLEN;

// Perform and return a deep copy of jobToCopy
struct Job *copyJob(struct Job *jobToCopy) {
  struct Packet *p = createEmptyPacket();
  memcpy(p, jobToCopy->packet, sizeof(struct Packet));
  struct Job *j = createEmptyJob();
  memcpy(j, jobToCopy, sizeof(struct Job));
  j->packet = p;
  return j;
}

// int dirAndFnameValid(int host_id, char hostDirectory[MAX_FILENAME_LENGTH],
//                      char fname[MAX_RESPONSE_LEN],
//                      char responseMsg[MAX_RESPONSE_LEN]) {
//   // Check to see if hostDirectory is valid
//   if (!isValidDirectory(hostDirectory)) {
//     sprintf(responseMsg, "Host%d does not have a valid directory set\n",
//             host_id);
//     return -1;  // Host Directory not set
//   }

//   // Check to see if fullPath points to a valid file
//   // Concatenate hostDirectory and filePath with a slash in between
//   char fullPath[2 * MAX_FILENAME_LENGTH] = {0};
//   snprintf(fullPath, (2 * MAX_FILENAME_LENGTH), "%s/%s", hostDirectory,
//   fname); if (!fileExists(fullPath)) {
//     sprintf(responseMsg, "This file does not exist\n");
//     return -2;
//   }

//   return 0;
// }

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

void handleRequestPkt(int host_id, struct Packet *recPkt,
                      struct Job_queue *host_q) {
  packet_type ptype;
  switch (recPkt->type) {
    case PKT_PING_REQ:
      ptype = PKT_PING_RESPONSE;
      break;
    case PKT_UPLOAD_REQ:
      ptype = PKT_UPLOAD_RESPONSE;
      break;
    case PKT_DOWNLOAD_REQ:
      ptype = PKT_DOWNLOAD_RESPONSE;
      break;
    default:
      return;
  }
  struct Packet *resPkt = createPacket(host_id, recPkt->src, ptype);
  resPkt->length = recPkt->length;
  memcpy(resPkt->payload, recPkt->payload, PACKET_PAYLOAD_MAX);
  // Place response packet inside response job and enqueue
  struct Job *resJob = createJob(JOB_SEND_RESPONSE, resPkt);
  job_enqueue(host_id, host_q, resJob);
}

void parseString(const char *inputStr, char *keyStr, char *dataStr) {
  // Find the delimiter in the input string
  char *delimPos = strchr(inputStr, ':');
  if (delimPos == NULL) {
    // Handle error - delimiter not found
    return;
  }

  // Copy the key string to the output buffer (if keyStr is not NULL)
  if (keyStr != NULL) {
    size_t keyLen = delimPos - inputStr;
    strncpy(keyStr, inputStr, keyLen);
    keyStr[keyLen] = '\0';  // Null-terminate the string
  }

  // Copy the data string to the output buffer (if dataStr is not NULL)
  if (dataStr != NULL) {
    size_t dataLen = strlen(inputStr) - (delimPos - inputStr) - 1;
    strncpy(dataStr, delimPos + 1, dataLen);
    dataStr[dataLen] = '\0';  // Null-terminate the string
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
  int dst;

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

      switch (man_cmd) {
        case 's':
          // Display host state
          reply_display_host_state(man_port, hostDirectory,
                                   isValidDirectory(hostDirectory), host_id);
          break;
          ////////////////

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
          ////////////////

        case 'p':
          ////// Have active Host ping another host //////
          // Get destination from man_msg
          sscanf(man_msg, "%d", &dst);
          // Generate a ping request packet
          struct Packet *pingReqPkt = createPacket(host_id, dst, PKT_PING_REQ);
          // Generate a job and put ping request packet inside
          struct Job *pingReqJob = createJob(JOB_SEND_REQUEST, pingReqPkt);
          job_enqueue(host_id, &host_q, pingReqJob);
          break;
          ////////////////

        case 'u':
          // Upload a file from active host to another host
          // Check to see if hostDirectory is valid
          if (!isValidDirectory(hostDirectory)) {
            colorPrint(BOLD_RED, "Host%d does not have a valid directory set\n",
                       host_id);
            break;
          }
          char fname[MAX_FILENAME_LENGTH] = {0};
          int dst;
          sscanf(man_msg, "%d %s", &dst, fname);
          int fnameLen = strnlen(fname, MAX_FILENAME_LENGTH);
          fname[fnameLen] = '\0';
          printf("fname: %s,  fnameLen: %d\n", fname, fnameLen);
          // Concatenate hostDirectory and filePath with a slash in between
          char fullPath[2 * MAX_FILENAME_LENGTH] = {0};
          snprintf(fullPath, (2 * MAX_FILENAME_LENGTH), "%s/%s", hostDirectory,
                   fname);
          // Check to see if fullPath points to a valid file
          if (!fileExists(fullPath)) {
            colorPrint(BOLD_RED, "This file does not exist\n", host_id);
            break;
          }
          // User input is valid, enqueue upload request to verify destination
          // Create Packet for upload request
          struct Packet *uplPkt = createPacket(host_id, dst, PKT_UPLOAD_REQ);
          // Put filename inside request packet payload, and update length
          strncpy(uplPkt->payload, fname, MAX_FILENAME_LENGTH);
          uplPkt->length = fnameLen;
          // Create and enqueue upload request job
          struct Job *uplJob = createJob(JOB_SEND_REQUEST, uplPkt);
          job_enqueue(host_id, &host_q, uplJob);
          break;
          ////////////////

        case 'd':
          // Download a file from another host to active host
          break;
          ////////////////

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
        switch (received_packet->type) {
          case PKT_PING_REQ:
          case PKT_UPLOAD_REQ:
          case PKT_DOWNLOAD_REQ:
            handleRequestPkt(host_id, received_packet, &host_q);
            break;

            ////////////////
          case PKT_PING_RESPONSE:
            char rTicket[TICKETLEN];
            parseString(received_packet->payload, rTicket, NULL);
            struct Request *r = findRequestByStringTicket(requestList, rTicket);
            if (r != NULL) {
              r->state = COMPLETE;
            }
            break;

          ////////////////
          default:
        }
        // ATTENTION! received_packet is freed every cycle
        // DO NOT pass received_packet pointer references to enqueued jobs!
        free(received_packet);
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
        ////////////////
        case JOB_SEND_REQUEST:
          // Determine request type from packet type
          requestType rtype = INVALID;
          switch (job_from_queue->packet->type) {
            case PKT_PING_REQ:
              rtype = PING_REQ;
              break;
            case PKT_UPLOAD_REQ:
              rtype = UPLOAD_REQ;
              break;
            case PKT_DOWNLOAD_REQ:
              rtype = DOWNLOAD_REQ;
              break;
            default:
              rtype = INVALID;
              break;
          }
          // Generate a request and add it to requestList
          struct Request *req = createRequest(rtype, TIMETOLIVE);
          addToReqList(&requestList, req);

          // Create a packet for JOB_SEND_PKT
          struct Packet *reqPkt = createEmptyPacket();
          memcpy(reqPkt, job_from_queue->packet, sizeof(struct Packet));

          // Include Request->ticket value and filename inside packet payload
          int reqTicket = req->ticket;
          // buffer to hold the formatted string
          char buf[PACKET_PAYLOAD_MAX];
          // copy the formatted string back to packet->payload
          snprintf(buf, PACKET_PAYLOAD_MAX, "%d:%.*s", reqTicket,
                   (int)(PACKET_PAYLOAD_MAX - sizeof(int) - 1),
                   reqPkt->payload);
          strcpy(reqPkt->payload, buf);
          // update packet length
          reqPkt->length = strlen(reqPkt->payload);

          // Copy request packet to include it in the JOB_WAITING_FOR_RESPONSE
          struct Packet *waitPkt = createEmptyPacket();
          memcpy(waitPkt, reqPkt, sizeof(struct Packet));

          // Enqueue a JOB_SEND_PKT to send to destination
          struct Job *sendJob = createJob(JOB_SEND_PKT, reqPkt);
          job_enqueue(host_id, &host_q, sendJob);

          // Enqueue a JOB_WAIT_FOR_RESPONSE
          struct Job *waitJob = createJob(JOB_WAIT_FOR_RESPONSE, waitPkt);
          job_enqueue(host_id, &host_q, waitJob);
          break;
          ////////////////

        case JOB_SEND_RESPONSE:
          struct Packet *responsePkt = createEmptyPacket();
          memcpy(responsePkt, job_from_queue->packet, sizeof(struct Packet));

          switch (job_from_queue->packet->type) {
            case PKT_PING_RESPONSE:
              break;

            case PKT_UPLOAD_RESPONSE: {
              // Parse packet payload for ticket and fname
              char ticket[TICKETLEN];
              char fname[MAX_RESPONSE_LEN];
              parseString(job_from_queue->packet->payload, ticket, fname);

              char responseMsg[MAX_RESPONSE_LEN];

              // Check to see if hostDirectory is valid
              if (!isValidDirectory(hostDirectory)) {
                snprintf(responseMsg, PACKET_PAYLOAD_MAX,
                         "%s:Host%d does not have a valid directory set\n",
                         ticket, host_id);

              } else {
                // Directory is set and valid
                // Create fullpath from hostDirectory and fname
                int fnameLen = strnlen(fname, MAX_FILENAME_LENGTH);
                fname[fnameLen] = '\0';
                char fullPath[2 * MAX_FILENAME_LENGTH];
                snprintf(fullPath, (2 * MAX_FILENAME_LENGTH), "%s/%s",
                         hostDirectory, fname);

                // Check to see if fullPath points to a valid file
                if (fileExists(fullPath)) {
                  snprintf(responseMsg, PACKET_PAYLOAD_MAX,
                           "%s:This file already exists\n", ticket);
                } else {
                  // directory is set and valid, and file does not already exist
                  snprintf(responseMsg, PACKET_PAYLOAD_MAX, "%s:Ready", ticket);
                }
              }

              // Update responsePkt payload and length
              strncpy(responsePkt->payload, responseMsg, PACKET_PAYLOAD_MAX);
              int responseMsgLen = strnlen(responseMsg, PACKET_PAYLOAD_MAX);
              responsePkt->length = responseMsgLen;

              // Create and enqueue job
              struct Job *res = createJob(JOB_SEND_PKT, responsePkt);
              job_enqueue(host_id, &host_q, res);
              break;

              break;
            }

            case PKT_DOWNLOAD_RESPONSE:
              break;
          }

          ////////////////

        case JOB_SEND_PKT:
          sendPacketTo(node_port_array, node_port_array_size,
                       job_from_queue->packet);
          break;
          ////////////////

        case JOB_WAIT_FOR_RESPONSE:
          char rTicket[TICKETLEN];
          parseString(job_from_queue->packet->payload, rTicket, NULL);
          struct Request *r = findRequestByStringTicket(requestList, rTicket);
          if (r != NULL) {
            switch (r->type) {
              case PING_REQ:
                if (r->state == COMPLETE) {
                  const char *repMsg = "\x1b[32;1mPing acknowledged!\x1b[0m";
                  write(man_port->send_fd, repMsg, strlen(repMsg));
                  deleteFromReqList(requestList, r->ticket);
                } else if (r->timeToLive > 0) {
                  r->timeToLive--;
                  struct Packet *p = createEmptyPacket();
                  memcpy(p, job_from_queue->packet, sizeof(struct Packet));
                  struct Job *j = createJob(JOB_WAIT_FOR_RESPONSE, p);
                  job_enqueue(host_id, &host_q, j);
                } else {
                  const char *repMsg = "\x1b[31;1mPing timed out!\x1b[0m";
                  write(man_port->send_fd, repMsg, strlen(repMsg));
                  deleteFromReqList(requestList, r->ticket);
                }
                break;

              case UPLOAD_REQ:
                break;

              case DOWNLOAD_REQ:
                break;
            }
          } else {
            printf("Request could not be found in list\n");
          }
          break;
          ////////////////

        default:
#ifdef DEBUG
          colorPrint(YELLOW,
                     "DEBUG: id:%d host_main: job_handler defaulted with "
                     "job type: "
                     "%s\n",
                     host_id, get_job_type_literal(job_from_queue->type));
#endif
      }
      // ATTENTION!!
      // job_from_queue->packet and job_from_queue are freed every cycle
      // DO NOT pass pointer references of these!
      if (job_from_queue->packet != NULL) {
        free(job_from_queue->packet);
        job_from_queue->packet = NULL;
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
