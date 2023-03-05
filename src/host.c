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
#include "semaphore.h"

// Helper Function Forward Declarations
void commandUploadHandler(int host_id, struct JobQueue *hostq,
                          char *hostDirectory, int dst, char *fname, int manFd);

void jobSendRequestHandler(int host_id, struct JobQueue *hostq,
                           struct Job *job_from_queue, struct Net_port **arr,
                           int arrSize);

void jobSendResponseHandler(int host_id, struct JobQueue *hostq,
                            struct Job *job_from_queue, struct Net_port **arr,
                            int arrSize);

void jobWaitForResponseHandler(int host_id, struct Job *job_from_queue,
                               struct JobQueue *hostq, char *hostDirectory,
                               int manFd);

void parsePacket(const char *inputStr, char *ticketStr, char *dataStr);

void pktIncomingRequestHandler(int host_id, struct JobQueue *hostq,
                               struct Packet *inPkt);

void pktIncomingResponseHandler(struct Packet *inPkt, struct JobQueue *hostq);

// Send a message back to manager
void sendMsgToManager(int fd, char msg[MAX_MSG_LENGTH]);

int sendPacketTo(struct Net_port **arr, int arrSize, struct Packet *p);

////////////////////////////////////////////////
////////////////// HOST MAIN ///////////////////
void host_main(int host_id) {
  ////// Initialize state of host //////
  char hostDirectory[MAX_FILENAME_LENGTH];
  char man_msg[MAX_MSG_LENGTH];
  // char man_reply_msg[MAX_MSG_LENGTH];

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
  struct JobQueue hostq;
  job_queue_init(&hostq);

  /* Initialize request list */
  struct Request *requestList = NULL;

  while (1) {
    ////////////////////////////////////////////////////////////////
    ////////////////////////////////
    //////////////// COMMAND HANDLER

    char man_cmd;
    int dst;

    int n = get_man_command(man_port, man_msg, &man_cmd);
    /* Execute command */
    if (n > 0) {
      sem_wait(&console_print_access);

      switch (man_cmd) {
        case 's': {
          // Display host state
          reply_display_host_state(man_port, hostDirectory,
                                   isValidDirectory(hostDirectory), host_id);
          break;
        }  //////////////// End of case 's'

        case 'm': {
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
        }  //////////////// End of case 'm'

        case 'p': {
          ////// Have active Host ping another host //////
          // Get destination from man_msg
          sscanf(man_msg, "%d", &dst);

          // Check to see if pinging self; issue warning
          if (dst == host_id) {
            colorPrint(BOLD_YELLOW, "Can not ping self\n");
            write(man_port->send_fd, "", sizeof(""));
            break;
          }

          // Create ping request packet
          struct Packet *preqPkt =
              createPacket(host_id, dst, PKT_PING_REQ, 0, NULL);

          // Create send request job
          struct Job *sendReqJob =
              job_create(NULL, TIMETOLIVE, NULL, JOB_SEND_REQUEST,
                         JOB_PENDING_STATE, preqPkt);
          // Enqueue job
          job_enqueue(host_id, &hostq, sendReqJob);
          break;
        }  //////////////// End of case 'p'

        case 'u': {
          // Upload a file from active host to another host
          // Get dst and fname from man_msg
          int dst;
          char fname[MAX_FILENAME_LENGTH] = {0};
          sscanf(man_msg, "%d %s", &dst, fname);
          int fnameLen = strnlen(fname, MAX_FILENAME_LENGTH);
          fname[fnameLen] = '\0';
          commandUploadHandler(host_id, &hostq, hostDirectory, dst, fname,
                               man_port->send_fd);
          break;
        }  //////////////// End of case 'u'

        case 'd': {
          // Download a file from another host to active host

          break;
        }  //////////////// End of case 'd'

        default:;
      }
    }

    //////////////// COMMAND HANDLER
    ////////////////////////////////
    ////////////////////////////////////////////////////////////////
    // -------------------------------------------------------------
    ////////////////////////////////////////////////////////////////
    ////////////////////////////////
    //////////////// PACKET HANDLER

    for (int portNum = 0; portNum < node_port_array_size; portNum++) {
      // Receive packets for all ports in node_port_array
      struct Packet *inPkt = createEmptyPacket();
      n = packet_recv(node_port_array[portNum], inPkt);
      // if portNum has received a packet, translate the packet into a job
      if ((n > 0) && ((int)inPkt->dst == host_id)) {
#ifdef DEBUG
        colorPrint(YELLOW,
                   "DEBUG: node_id:%d host_main packet_handler received "
                   "packet: \n\t",
                   host_id);
        printPacket(inPkt);
#endif
        switch (inPkt->type) {
          case PKT_PING_REQ:
          case PKT_UPLOAD_REQ:
          case PKT_DOWNLOAD_REQ:
            pktIncomingRequestHandler(host_id, &hostq, inPkt);
            break;

            ////////////////
          case PKT_PING_RESPONSE:
          case PKT_UPLOAD_RESPONSE:
          case PKT_DOWNLOAD_RESPONSE:
            pktIncomingResponseHandler(inPkt, &hostq);
            break;

          case PKT_UPLOAD:

            break;

          case PKT_UPLOAD_END:

            break;

          ////////////////
          default:
        }
      }

      //////////////// PACKET HANDLER
      ////////////////////////////////
      ////////////////////////////////////////////////////////////////
      // -------------------------------------------------------------
      ////////////////////////////////////////////////////////////////
      ////////////////////////////////
      //////////////// JOB HANDLER

      if (job_queue_length(&hostq) > 0) {
        /* Get a new job from the job queue */
        struct Job *job_from_queue = job_dequeue(host_id, &hostq);

        //////////////////// EXECUTE FETCHED JOB ////////////////////
        switch (job_from_queue->type) {
          ////////////////
          case JOB_SEND_REQUEST: {
            jobSendRequestHandler(host_id, &hostq, job_from_queue,
                                  node_port_array, node_port_array_size);
            break;
          }  //////////////// End of JOB_SEND_REQUEST

          case JOB_SEND_RESPONSE: {
            jobSendResponseHandler(host_id, &hostq, job_from_queue,
                                   node_port_array, node_port_array_size);
          }  //////////////// End of case JOB_SEND_RESPONSE

          case JOB_SEND_PKT: {
            break;
          }  //////////////// End of case JOB_SEND_PKT

          case JOB_WAIT_FOR_RESPONSE: {
            jobWaitForResponseHandler(host_id, job_from_queue, &hostq,
                                      hostDirectory, man_port->send_fd);
            break;
          }  //////////////// End of case JOB_WAIT_FOR_RESPONSE

          case JOB_UPLOAD: {
            break;
          }  //////////////// End of case JOB_UPLOAD

          case JOB_DOWNLOAD: {
            break;
          }  //////////////// End of case JOB_DOWNLOAD

          default:
#ifdef DEBUG
            colorPrint(YELLOW,
                       "DEBUG: id:%d host_main: job_handler defaulted with "
                       "job type: "
                       "%s\n",
                       host_id, get_job_type_literal(job_from_queue->type));
#endif
        }  // End of switch (job_from_queue->type)
      }    // End of if (job_queue_length(&hostq) > 0)

      /////////////////// JOB HANDLER
      ///////////////////////////////////
      ///////////////////////////////////////////////////////////////////////

      /* The host goes to sleep for 10 ms */
      usleep(LOOP_SLEEP_TIME_MS);
    }  // End of for (int portNum = 0; portNum < node_port_array_size;
       // portNum++)
  }    // End of while(1)
}  // End of host_main()

//////////////////////////////////////////////////
//////////////////////////////////////////////////
//////////////////////////////////////////////////
//////////////////////////////////////////////////
//////////////////////////////////////////////////
//////////////////////////////////////////////////
//////////////// HELPER FUNCTIONS ////////////////

void commandUploadHandler(int host_id, struct JobQueue *hostq,
                          char *hostDirectory, int dst, char *fname,
                          int manFd) {
  char *responseMsg = malloc(sizeof(char) * MAX_MSG_LENGTH);

  if (!isValidDirectory(hostDirectory)) {
    snprintf(responseMsg, MAX_MSG_LENGTH,
             "Host %d does not have a valid directory set", host_id);
  } else if (dst == host_id) {
    snprintf(responseMsg, MAX_MSG_LENGTH, "Cannot upload to self");
  } else {
    char fullPath[2 * MAX_FILENAME_LENGTH] = {0};
    snprintf(fullPath, sizeof(fullPath), "%s/%s", hostDirectory, fname);

    if (!fileExists(fullPath)) {
      snprintf(responseMsg, MAX_MSG_LENGTH, "This file does not exist");
    } else {
      // Directory is set, and file exists

      // Create an upload request packet
      struct Packet *upReqPkt =
          createPacket(host_id, dst, PKT_UPLOAD_REQ, 0, NULL);

      // Create a send request job
      struct Job *sendReqJob =
          job_create(NULL, TIMETOLIVE, NULL, JOB_SEND_REQUEST,
                     JOB_PENDING_STATE, upReqPkt);

      // Enque job
      job_enqueue(host_id, hostq, sendReqJob);
    }
  }

  sendMsgToManager(manFd, responseMsg);
  free(responseMsg);
}

void jobSendRequestHandler(int host_id, struct JobQueue *hostq,
                           struct Job *job_from_queue, struct Net_port **arr,
                           int arrSize) {
  if (!job_from_queue || !hostq) {
    fprintf(stderr,
            "ERROR: host%d jobSendRequestHandler invoked with invalid function "
            "parameter: %s %s\n",
            host_id, !job_from_queue ? "job_from_queue " : "",
            !hostq ? "hostq " : "");
    return;
  }

  sendPacketTo(arr, arrSize, job_from_queue->packet);

  job_from_queue->type = JOB_WAIT_FOR_RESPONSE;
  job_enqueue(host_id, hostq, job_from_queue);
}
// End of jobSendRequestHandler

// void sendPingResponse(int host_id, struct JobQueue *hostq,
//                       struct Job *job_from_queue, struct Net_port **arr,
//                       int arrSize) {}

void jobSendResponseHandler(int host_id, struct JobQueue *hostq,
                            struct Job *job_from_queue, struct Net_port **arr,
                            int arrSize) {
  switch (job_from_queue->packet->type) {
    case PKT_PING_RESPONSE:
      sendPacketTo(arr, arrSize, job_from_queue->packet);
      break;
    case PKT_UPLOAD_RESPONSE:
      break;
    case PKT_DOWNLOAD_RESPONSE:
      break;
  }
}  // End of jobSendResponseHandler()

void jobWaitForResponseHandler(int host_id, struct Job *job,
                               struct JobQueue *hostq, char *hostDirectory,
                               int manFd) {
  // printf("jobWaitForResponseHandler called\n");
  // printJob(job);

  char *responseMsg = (char *)malloc(sizeof(char) * MAX_MSG_LENGTH);

  if (job->timeToLive <= 0) {
    // job timeToLive has expired
    switch (job->packet->type) {
      case PKT_PING_REQ:
        // snprintf(responseMsg, MAX_MSG_LENGTH, "Ping request timed out!");
        colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED,
                      "Ping request timed out!");
        break;
      case PKT_UPLOAD_REQ:
        snprintf(responseMsg, MAX_MSG_LENGTH, "Upload request timed out!");
        break;
      case PKT_DOWNLOAD_REQ:
        snprintf(responseMsg, MAX_MSG_LENGTH, "Download request timed out!");
        break;
    }
    sendMsgToManager(manFd, responseMsg);
    job_delete(job);

  } else {
    // timeToLive > 0

    // Decrement
    job->timeToLive--;

    if (job->state == JOB_PENDING_STATE) {
      // re-enque job while ttl > 0
      job_enqueue(host_id, hostq, job);
    } else {
      switch (job->packet->type) {
        case PKT_PING_REQ:
          if (job->state == JOB_COMPLETE_STATE) {
            snprintf(responseMsg, MAX_MSG_LENGTH,
                     "\x1b[32;1mPing request acknowledged!\x1b[0m");
          }
          break;

        case PKT_UPLOAD_REQ:
          if (job->state == JOB_READY_STATE) {
          } else if (job->state == JOB_ERROR_STATE) {
            snprintf(responseMsg, MAX_MSG_LENGTH, "%s", job->errorMsg);

          } else if (job->state == JOB_COMPLETE_STATE) {
            snprintf(responseMsg, MAX_MSG_LENGTH, "Upload Complete");
          }
          break;

        case PKT_DOWNLOAD_REQ:
          if (job->state == JOB_READY_STATE) {
          } else if (job->state == JOB_ERROR_STATE) {
            snprintf(responseMsg, MAX_MSG_LENGTH, "%s", job->errorMsg);

          } else if (job->state == JOB_COMPLETE_STATE) {
            printf("Download Complete\n");
          }
          // destroyJob(job_from_queue);
          break;
      }
      sendMsgToManager(manFd, responseMsg);
      job_delete(job);
    }
  }
  free(responseMsg);
}  // End of jobWaitForResponseHandler()

/* parsePacket:
 * parse a packet payload inputStr into its ticket and data
 * Note:: Use dynamically allocated buffers for ticket and data */
void parsePacket(const char *inputStr, char *ticketStr, char *dataStr) {
  const char delim = ':';

  int inputLen = strnlen(inputStr, PACKET_PAYLOAD_MAX);
  if (inputLen == 0) {
    printf("ERROR: parsePacket was passed an empty inputStr\n");
    return;
  }
  if (inputLen > PACKET_PAYLOAD_MAX) {
    printf(
        "ERROR: parsePacket was passed an inputStr larger than "
        "PACKET_PAYLOAD_MAX\n");
    return;
  }

  // Find position of delimiter
  int delimPos = 0;
  for (int i = 0; i < inputLen; i++) {
    if (inputStr[i] == delim) {
      delimPos = i;
      break;
    }
  }

  if (delimPos == 0) {
    printf("ERROR: parsePacket: delimiter not found in inputStr\n");
    return;
  }

  // Copy ticket from inputStr into ticketStr
  for (int i = 0; i < delimPos; i++) {
    ticketStr[i] = inputStr[i];
  }
  ticketStr[delimPos] = '\0';

  // Copy data from inputStr into dataStr
  for (int i = delimPos + 1; i < inputLen; i++) {
    dataStr[i - delimPos - 1] = inputStr[i];
  }
  dataStr[inputLen - delimPos - 1] = '\0';
}  // End of parsePacket()

void pktIncomingRequestHandler(int host_id, struct JobQueue *hostq,
                               struct Packet *inPkt) {
  packet_type response_type;
  switch (inPkt->type) {
    case PKT_PING_REQ:
      response_type = PKT_PING_RESPONSE;
      break;
    case PKT_UPLOAD_REQ:
      response_type = PKT_UPLOAD_RESPONSE;
      break;
    case PKT_DOWNLOAD_REQ:
      response_type = PKT_DOWNLOAD_RESPONSE;
      break;
    default:
      response_type = PKT_INVALID_TYPE;
      return;
  }

  // Readdress incoming packet to use as
  inPkt->type = response_type;
  inPkt->dst = inPkt->src;
  inPkt->src = host_id;

  // Grab jid from request packet payload
  char *id = (char *)malloc(sizeof(char) * JIDLEN);
  char *msg = (char *)malloc(sizeof(char) * MAX_RESPONSE_LEN);
  parsePacket(inPkt->payload, id, msg);

  struct Job *sendRespJob = job_create(id, TIMETOLIVE, NULL, JOB_SEND_RESPONSE,
                                       JOB_PENDING_STATE, inPkt);
  job_enqueue(host_id, hostq, sendRespJob);

  free(id);
  free(msg);
}  // End of pktIncomingRequestHandler()

void pktIncomingResponseHandler(struct Packet *inPkt, struct JobQueue *hostq) {
  // Grab jid from request packet payload
  char *id = (char *)malloc(sizeof(char) * JIDLEN);
  char *msg = (char *)malloc(sizeof(char) * MAX_RESPONSE_LEN);
  parsePacket(inPkt->payload, id, msg);

  // Find jid in job queue
  struct Job *waitJob = job_queue_find_id(hostq, id);

  switch (inPkt->type) {
    case PKT_PING_RESPONSE:
      // Ping request acknowledged
      waitJob->state = JOB_COMPLETE_STATE;
      break;
    case PKT_UPLOAD_RESPONSE:
    case PKT_DOWNLOAD_RESPONSE:
      if (strncmp(msg, "Ready", sizeof("Ready")) == 0) {
        waitJob->state = JOB_READY_STATE;
      } else {
        waitJob->state = JOB_ERROR_STATE;
      }
      break;
  }

  // Clean up packet and dynamic vars
  free(inPkt);
  inPkt = NULL;
  free(id);
  free(msg);
}  // End of pktPingResponseHandler()

// Send a message back to manager
void sendMsgToManager(int fd, char msg[MAX_MSG_LENGTH]) {
  int msgLen = strnlen(msg, MAX_MSG_LENGTH);
  write(fd, msg, msgLen);
}

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
}  // End of sendPacketTo
