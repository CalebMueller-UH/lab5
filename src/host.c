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

// Send a message back to manager
void sendMsgToManager(int fd, char msg[MAX_MSG_LENGTH]) {
  int msgLen = strnlen(msg, MAX_MSG_LENGTH);
  write(fd, msg, msgLen);
}

void parseString(const char *inputStr, char *ticketStr, char *dataStr) {
  // Find the delimiter in the input string
  char *delimPos = strchr(inputStr, ':');
  if (delimPos == NULL) {
    // Handle error - delimiter not found
    ticketStr = NULL;
    strncpy(dataStr, "BAD DATA", 9);
    dataStr[9] = '\0';  // Null-terminate the string
    return;
  }
  // Copy the key string to the output buffer (if ticketStr is not NULL)
  if (ticketStr != NULL) {
    size_t keyLen = delimPos - inputStr;
    strncpy(ticketStr, inputStr, keyLen);
    ticketStr[keyLen] = '\0';  // Null-terminate the string
  }
  // Copy the data string to the output buffer (if dataStr is not NULL)
  if (dataStr != NULL) {
    size_t dataLen = strlen(inputStr) - (delimPos - inputStr) - 1;
    strncpy(dataStr, delimPos + 1, dataLen);
    dataStr[dataLen] = '\0';  // Null-terminate the string
  }
  // Null-terminate the ticket string
  if (ticketStr != NULL) {
    size_t ticketLen = strlen(ticketStr);
    ticketStr[ticketLen] = '\0';
  }
  // Null-terminate the data string
  if (dataStr != NULL) {
    size_t dataLen = strlen(dataStr);
    dataStr[dataLen] = '\0';
  }
}  // End of parseString()

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

void handleIncomingRequestPacket(int host_id, struct Packet *recPkt,
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
      ptype = PKT_INVALID_TYPE;
      return;
  }
  struct Packet *resPkt = createPacket(host_id, recPkt->src, ptype);
  resPkt->length = recPkt->length;
  memcpy(resPkt->payload, recPkt->payload, PACKET_PAYLOAD_MAX);
  resPkt->src = host_id;
  resPkt->dst = recPkt->src;
  // Place response packet inside response job and enqueue
  struct Job *resJob = createJob(JOB_SEND_RESPONSE, resPkt);
  job_enqueue(host_id, host_q, resJob);
}  // End of handleIncomingRequestPacket()

void handleIncomingResponsePacket(int host_id, struct Packet *recPkt,
                                  struct Job_queue *host_q,
                                  struct Request **reqList) {
  char responseTicket[TICKETLEN];
  parseString(recPkt->payload, responseTicket, NULL);
  struct Request *r = findRequestByStringTicket(*reqList, responseTicket);
  if (r != NULL) {
    switch (recPkt->type) {
      case PKT_PING_RESPONSE:
        r->state = STATE_COMPLETE;
        break;
      case PKT_UPLOAD_RESPONSE:
        r->state = STATE_READY;
        break;
      case PKT_DOWNLOAD_RESPONSE:
        r->state = STATE_READY;
        break;
      default:
    }
  }
}  // End of handleIncomingResponsePacket()

void jobSendRequestHandler(int host_id, struct Job *job_from_queue,
                           struct Job_queue *host_q, char *hostDirectory,
                           struct Request **requestList) {
  // input validation for function parameters

  if (!job_from_queue || !host_q || !requestList) {
    fprintf(stderr,
            "ERROR: host%d jobSendRequestHandler invoked with invalid function "
            "parameter: ",
            host_id);
    if (!job_from_queue) {
      fprintf(stderr, "job_from_queue\n");
    }
    if (!host_q) {
      fprintf(stderr, "host_q\n");
    }
    if (!requestList) {
      fprintf(stderr, "requestList\n");
    }
    return;
  }

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

  // create a request, and add it to request list
  struct Request *requestX = createRequest(rtype, TIMETOLIVE);
  if (!requestX) {
    fprintf(stderr,
            "ERROR: host%d jobSendRequestHandler could not create requestX\n",
            host_id);
    return;
  }
  addToReqList(requestList, requestX);

  struct Packet *reqPkt = createEmptyPacket();
  if (!reqPkt) {
    fprintf(stderr,
            "ERROR: host%d jobSendRequestHandler could not create reqPkt\n",
            host_id);
    return;  // or handle the error appropriately
  }
  memcpy(reqPkt, job_from_queue->packet, sizeof(struct Packet));

  char payloadBuffer[PACKET_PAYLOAD_MAX];
  snprintf(payloadBuffer, sizeof(payloadBuffer), "%d:%.*s", requestX->ticket,
           (int)(sizeof(payloadBuffer) - sizeof(int) - 1), reqPkt->payload);
  int payload_len = strlen(payloadBuffer);
  if (payload_len >= PACKET_PAYLOAD_MAX) {
    payload_len = PACKET_PAYLOAD_MAX - 1;
  }
  memcpy(reqPkt->payload, payloadBuffer, payload_len);
  reqPkt->payload[payload_len] = '\0';
  reqPkt->length = payload_len;

  struct Job *sendJob = createJob(JOB_SEND_PKT, reqPkt);
  if (!sendJob) {
    fprintf(stderr,
            "ERROR: host%d jobSendRequestHandler could not create sendJob\n",
            host_id);
    return;
  }
  job_enqueue(host_id, host_q, sendJob);

  struct Packet *waitPkt = createEmptyPacket();
  if (!waitPkt) {
    fprintf(stderr,
            "ERROR: host%d jobSendRequestHandler could not create waitPkt"
            "parameter\n",
            host_id);
    return;
  }
  memcpy(waitPkt, reqPkt, sizeof(struct Packet));

  struct Job *waitJob = createJob(JOB_WAIT_FOR_RESPONSE, waitPkt);
  if (!waitJob) {
    fprintf(stderr,
            "ERROR: host%d jobSendRequestHandler could not create waitJob"
            "parameter\n",
            host_id);
    return;
  }
  waitJob->request = requestX;
  job_enqueue(host_id, host_q, waitJob);
}  // End of jobSendRequestHandler

void jobSendResponseHandler(int host_id, struct Job *job_from_queue,
                            struct Job_queue *host_q, char *hostDirectory,
                            struct Request *reqList) {
  switch (job_from_queue->packet->type) {
    case PKT_PING_RESPONSE: {
      job_from_queue->type = JOB_SEND_PKT;
      job_enqueue(host_id, host_q, job_from_queue);
      break;
    }

    case PKT_UPLOAD_RESPONSE: {
      // Parse packet payload for ticket and fname
      char ticket[TICKETLEN];
      char fname[MAX_RESPONSE_LEN];
      memset(ticket, 0, TICKETLEN);
      memset(fname, 0, MAX_RESPONSE_LEN);

      parseString(job_from_queue->packet->payload, ticket, fname);

      char responseMsg[MAX_RESPONSE_LEN];
      int responseMsgLen = 0;

      // Check to see if hostDirectory is valid
      if (!isValidDirectory(hostDirectory)) {
        responseMsgLen = snprintf(
            responseMsg, MAX_RESPONSE_LEN,
            "%s:Host%d does not have a valid directory set", ticket, host_id);
      } else {
        // Host has directory set

        // Build the full file path from directory and fname
        char *fullPath = (char *)malloc(sizeof(char) * 2 * MAX_FILENAME_LENGTH);

        int fullPathLen = snprintf(fullPath, 2 * MAX_FILENAME_LENGTH, "%s/%s",
                                   hostDirectory, fname);

        // Check to see if fullPath already points to an existing file
        if (fileExists(fullPath)) {
          responseMsgLen =
              snprintf(responseMsg, MAX_RESPONSE_LEN,
                       "%s:File already exists on host%d", ticket, host_id);
        } else {
          responseMsgLen =
              snprintf(responseMsg, MAX_RESPONSE_LEN, "%s:Ready", ticket);
        }
        free(fullPath);
        fullPath = NULL;
      }

      // Add null terminator to responseMsg buffer
      responseMsg[MAX_RESPONSE_LEN - 1] = '\0';

      struct Packet *responsePkt = job_from_queue->packet;
      // Update responsePkt payload and length
      if (responseMsgLen > PACKET_PAYLOAD_MAX) {
        // handle error
      } else {
        strncpy(responsePkt->payload, responseMsg, responseMsgLen);
        responsePkt->payload[responseMsgLen] = '\0';  // add null terminator
        responsePkt->length = responseMsgLen;
      }

      // Create and enqueue job
      struct Job *res = createJob(JOB_SEND_PKT, responsePkt);
      job_enqueue(host_id, host_q, res);
      break;
    }

    case PKT_DOWNLOAD_RESPONSE:
      break;
  }
}  // End of jobSendResponseHandler()

void destroyJob(struct Job *jobToDestroy) {
  if (jobToDestroy) {
    if (jobToDestroy->packet) {
      free(jobToDestroy->packet);
      jobToDestroy->packet = NULL;
    }
    // if (jobToDestroy->request) {
    //   free(jobToDestroy->request);
    //   jobToDestroy->request = NULL;
    // }
    free(jobToDestroy);
    jobToDestroy = NULL;
  }
}

void jobUploadHandler(int host_id, int dst, char ticket[TICKETLEN],
                      char directory[MAX_FILENAME_LENGTH],
                      char fname[MAX_RESPONSE_LEN], struct Job_queue *host_q) {
  char *fullPath =
      (char *)malloc(sizeof(char) * (MAX_FILENAME_LENGTH + MAX_RESPONSE_LEN));
  sprintf(fullPath, "%s/%s", directory, fname);

  // Step 1: Open the file you want to send in binary mode.
  FILE *fp = fopen(fullPath, "rb");
  if (fp == NULL) {
    printf("Error opening file.\n");
    free(fullPath);
    return;
  }

  // Step 2: Determine the size of the file in bytes.
  fseek(fp, 0L, SEEK_END);
  long fileSize = ftell(fp);
  fseek(fp, 0L, SEEK_SET);

  // Step 3: Allocate a buffer of a certain size that you will use to read
  // chunks of the file into.
  int bufferSize = MAX_RESPONSE_LEN;
  char *buffer = (char *)malloc(sizeof(char) * bufferSize);

  // Step 4: Read the first chunk of data from the file into the buffer.
  int bytesRead = fread(buffer, sizeof(char), bufferSize, fp);

  // Step 5: Send the first chunk of data to the receiving end.
  struct Packet *firstPkt = createPacket(host_id, dst, PKT_UPLOAD);
  int pktLen =
      snprintf(firstPkt->payload, PACKET_PAYLOAD_MAX, "%s:%s", ticket, buffer);
  firstPkt->length = pktLen;
  struct Job *firstJob = createJob(JOB_SEND_PKT, firstPkt);
  job_enqueue(host_id, host_q, firstJob);

  // Step 6: Repeat steps 4 and 5 until you have read and sent the entire file.
  while (bytesRead == bufferSize) {
    bytesRead = fread(buffer, sizeof(char), bufferSize, fp);
    struct Packet *nextPkt = createPacket(host_id, dst, PKT_UPLOAD);
    int nextPktLen =
        snprintf(nextPkt->payload, PACKET_PAYLOAD_MAX, "%s:%s", ticket, buffer);
    nextPkt->length = nextPktLen;
    struct Job *nextJob = createJob(JOB_SEND_PKT, nextPkt);
    job_enqueue(host_id, host_q, nextJob);
  }

  // Step 7: Close the file.
  fclose(fp);

  // Step 8: Notify the receiving end that the file has been fully sent.
  // send(ticket, "EOF", 3, 0);

  // Free the memory used by the buffer and full path string.
  free(buffer);
  free(fullPath);
}  // End of jobUploadHandler()

// void jobDownloadHandler(int host_id, int dst, struct Packet *pkt) {
//   char *payload = pkt->payload;
//   char *ticketStr = strtok(payload, ":");
//   char *buffer = strtok(NULL, ":");
//   int ticket = atoi(ticketStr);

//   // Find the file name from the first packet of the upload
//   char *uploadPayload = pkt_get_payload(firstPkt);
//   char *filename = strtok(uploadPayload, "/");
//   while (filename != NULL) {
//     filename = strtok(NULL, "/");
//   }

//   // Step 1: Open the file for writing in binary mode.
//   FILE *fp = fopen(filename, "wb");
//   if (fp == NULL) {
//     printf("Error opening file.\n");
//     return;
//   }

//   // Step 2: Write the buffer data to the file.
//   fwrite(buffer, sizeof(char), pkt->length - strlen(ticketStr) - 1, fp);

//   // Step 3: If this is not the last packet, create a new job to receive the
//   // next packet.
//   if (strcmp(buffer, "EOF") != 0) {
//     struct Job *nextJob = createJob(JOB_RECV_PKT, ticket);
//     pushJob(nextJob);
//   }

//   // Step 4: Close the file.
//   fclose(fp);
// }  // End of jobDownloadHandler()

void jobWaitForResponseHandler(int host_id, struct Job *job_from_queue,
                               struct Job_queue *host_q, char *hostDirectory,
                               struct Request **reqList, int manFd) {
  // Grab the request ticket from the job_from_queue->packet->payload
  char ticket[TICKETLEN];
  char msg[MAX_RESPONSE_LEN];
  parseString(job_from_queue->packet->payload, ticket, msg);

  // Find the request r matching that request ticket in payload
  struct Request *r = findRequestByStringTicket(*reqList, ticket);

  // If the request r can be found in the reqList
  if (r != NULL) {
    char responseMsg[MAX_MSG_LENGTH];

    // Check the time to live of the request
    if (r->timeToLive <= 0) {
      // job request timeToLive has expired

      switch (r->type) {
        case PING_REQ:
          snprintf(responseMsg, MAX_MSG_LENGTH,
                   "\x1b[31;1mPing timed out!\x1b[0m");
          sendMsgToManager(manFd, responseMsg);
          break;
        case UPLOAD_REQ:
          break;
        case DOWNLOAD_REQ:
          break;
      }

      // Delete from requestList
      deleteFromReqList(*reqList, r->ticket);
      // And discard job & packet
      destroyJob(job_from_queue);

    } else {
      // job request timeToLive still alive
      // Decrement
      r->timeToLive--;

      switch (r->type) {
        case PING_REQ:
          if (r->state == STATE_COMPLETE) {
            // sendMsgToManager(manFd, "\x1b[32;1mPing acknowledged!\x1b[0m");
            snprintf(responseMsg, MAX_MSG_LENGTH,
                     "\x1b[32;1mPing acknowledged!\x1b[0m");
            sendMsgToManager(manFd, responseMsg);
            deleteFromReqList(*reqList, r->ticket);
            destroyJob(job_from_queue);

          } else if (r->state == STATE_PENDING) {
            job_enqueue(host_id, host_q, job_from_queue);
          }
          break;

        case UPLOAD_REQ:
          printf("host%d: jobWaitForResponseHandler msg: %s\n", host_id, msg);
          jobUploadHandler(host_id, job_from_queue->packet->dst, ticket,
                           hostDirectory, msg, host_q);
          break;

        case DOWNLOAD_REQ:
          break;
      }
    }

  } else {
    printf("Request could not be found in list\n");
  }
}  // End of jobWaitForResponseHandler()

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
    ////////////////////////////////////////////////////////////////
    ////////////////////////////////
    //////////////// COMMAND HANDLER

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
        }  //////////////// End of case 's'

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

          // Generate a ping request packet
          // Create a job containing that packet
          // And enqueue the job
          job_enqueue(host_id, &host_q,
                      createJob(JOB_SEND_REQUEST,
                                createPacket(host_id, dst, PKT_PING_REQ)));
          break;
        }  //////////////// End of case 'p'

        case 'u': {
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

          // Check to see if uploading to self; issue warning
          if (dst == host_id) {
            colorPrint(BOLD_YELLOW, "Can not upload to self\n");
            write(man_port->send_fd, "", sizeof(""));
            break;
          }

          // Concatenate hostDirectory and filePath with a slash in between
          char fullPath[2 * MAX_FILENAME_LENGTH] = {0};
          snprintf(fullPath, (2 * MAX_FILENAME_LENGTH), "%s/%s", hostDirectory,
                   fname);
          // Check to see if fullPath points to a valid file
          if (!fileExists(fullPath)) {
            colorPrint(BOLD_RED, "This file does not exist\n", host_id);
            break;
          }
          // User input is valid: enqueue upload request to verify destination
          // Generate a upload request packet
          struct Packet *requestPacket =
              createPacket(host_id, dst, PKT_UPLOAD_REQ);
          requestPacket->length = fnameLen;
          memcpy(requestPacket->payload, fname, fnameLen);
          // Create a job containing that packet
          struct Job *requestJob = createJob(JOB_SEND_REQUEST, requestPacket);
          // And enqueue the job
          job_enqueue(host_id, &host_q, requestJob);
          break;
        }  //////////////// End of case 'u'

        case 'd': {
          // Download a file from another host to active host

          char fname[MAX_FILENAME_LENGTH] = {0};
          int dst;
          sscanf(man_msg, "%d %s", &dst, fname);
          int fnameLen = strnlen(fname, MAX_FILENAME_LENGTH);
          fname[fnameLen] = '\0';

          // Check to see if uploading to self; issue warning
          if (dst == host_id) {
            colorPrint(BOLD_YELLOW, "Can not upload to self\n");
            write(man_port->send_fd, "", sizeof(""));
            break;
          }

          // Generate a upload request packet
          struct Packet *requestPacket =
              createPacket(host_id, dst, PKT_UPLOAD_REQ);
          requestPacket->length = fnameLen;
          memcpy(requestPacket->payload, fname, fnameLen);
          // Create a job containing that packet
          struct Job *requestJob = createJob(JOB_SEND_REQUEST, requestPacket);
          // And enqueue the job
          job_enqueue(host_id, &host_q, requestJob);
          break;
        }  //////////////// End of case 'd'

        default:;
      }
      // Release console_print_access semaphore
      sem_signal(&console_print_access);
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
      struct Packet *received_packet = createEmptyPacket();
      n = packet_recv(node_port_array[portNum], received_packet);
      // if portNum has received a packet, translate the packet into a job
      if ((n > 0) && ((int)received_packet->dst == host_id)) {
#ifdef DEBUG
        colorPrint(YELLOW,
                   "DEBUG: node_id:%d host_main packet_handler received "
                   "packet: \n\t",
                   host_id);
        printPacket(received_packet);
#endif
        switch (received_packet->type) {
          case PKT_PING_REQ:
          case PKT_UPLOAD_REQ:
          case PKT_DOWNLOAD_REQ:
            handleIncomingRequestPacket(host_id, received_packet, &host_q);
            break;

            ////////////////
          case PKT_PING_RESPONSE:
          case PKT_UPLOAD_RESPONSE:
          case PKT_DOWNLOAD_RESPONSE:
            handleIncomingResponsePacket(host_id, received_packet, &host_q,
                                         &requestList);
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

      if (job_queue_length(&host_q) > 0) {
        /* Get a new job from the job queue */
        struct Job *job_from_queue = job_dequeue(host_id, &host_q);

        //////////////////// EXECUTE FETCHED JOB ////////////////////
        switch (job_from_queue->type) {
          ////////////////
          case JOB_SEND_REQUEST: {
            jobSendRequestHandler(host_id, job_from_queue, &host_q,
                                  hostDirectory, &requestList);
            break;
          }  //////////////// End of JOB_SEND_REQUEST

          case JOB_SEND_RESPONSE: {
            jobSendResponseHandler(host_id, job_from_queue, &host_q,
                                   hostDirectory, NULL);
            break;
          }  //////////////// End of case JOB_SEND_RESPONSE

          case JOB_SEND_PKT: {
            sendPacketTo(node_port_array, node_port_array_size,
                         job_from_queue->packet);
            break;
          }  ////////////////

          case JOB_WAIT_FOR_RESPONSE: {
            jobWaitForResponseHandler(host_id, job_from_queue, &host_q,
                                      hostDirectory, &requestList,
                                      man_port->send_fd);
            break;
          }  //////////////// End of case JOB_WAIT_FOR_RESPONSE

          case JOB_UPLOAD: {
            // jobUploadHandler();
            break;
          }  //////////////// End of case JOB_UPLOAD

          default:
#ifdef DEBUG
            colorPrint(YELLOW,
                       "DEBUG: id:%d host_main: job_handler defaulted with "
                       "job type: "
                       "%s\n",
                       host_id, get_job_type_literal(job_from_queue->type));
#endif
        }  // End of switch (job_from_queue->type)
      }    // End of if (job_queue_length(&host_q) > 0)

      /////////////////// JOB HANDLER
      ///////////////////////////////////
      ///////////////////////////////////////////////////////////////////////

      /* The host goes to sleep for 10 ms */
      usleep(LOOP_SLEEP_TIME_MS);
    }  // End of for (int portNum = 0; portNum < node_port_array_size;
       // portNum++)
  }    // End of while(1)
}  // End of host_main()
