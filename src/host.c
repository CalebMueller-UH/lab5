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

// Send a message back to manager
void sendMsgToManager(int fd, char msg[MAX_MSG_LENGTH]) {
  int msgLen = strnlen(msg, MAX_MSG_LENGTH);
  write(fd, msg, msgLen);
}

// Parse a packet payload inputStr into its ticket and data
// Note:: Use dynamically allocated buffers for ticket and data
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
  char *ticket = (char *)malloc(sizeof(char) * TICKETLEN);
  char *msg = (char *)malloc(sizeof(char) * PACKET_PAYLOAD_MAX - TICKETLEN - 1);
  parsePacket(recPkt->payload, ticket, msg);

  struct Request *r = findRequestByStringTicket(*reqList, ticket);
  if (r != NULL) {
    switch (recPkt->type) {
      case PKT_PING_RESPONSE:
        r->state = STATE_COMPLETE;
        break;
      case PKT_UPLOAD_RESPONSE:
        if (strncmp(msg, "Ready", sizeof("Ready")) == 0) {
          r->state = STATE_READY;
        } else {
          r->state = STATE_ERROR;
          strncpy(r->errorMsg, msg, MAX_RESPONSE_LEN);
        }
        break;
      case PKT_DOWNLOAD_RESPONSE:
        r->state = STATE_READY;
        break;
      default:
    }
  }
  // Clean up memory allocation
  free(ticket);
  free(msg);
}  // End of handleIncomingResponsePacket()

void jobSendRequestHandler(int host_id, struct Job *job_from_queue,
                           struct Job_queue *host_q, char *hostDirectory,
                           struct Request **requestList) {
  if (!job_from_queue || !host_q || !requestList) {
    fprintf(stderr,
            "ERROR: host%d jobSendRequestHandler invoked with invalid function "
            "parameter: %s%s%s\n",
            host_id, !job_from_queue ? "job_from_queue " : "",
            !host_q ? "host_q " : "", !requestList ? "requestList" : "");
    return;
  }

  requestType rtype;
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
      rtype = INVALID_REQ;
      break;
  }

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
    return;
  }
  memcpy(reqPkt, job_from_queue->packet, sizeof(struct Packet));

  char payloadBuffer[PACKET_PAYLOAD_MAX];
  int payload_len = snprintf(
      payloadBuffer, sizeof(payloadBuffer), "%d:%.*s", requestX->ticket,
      (int)(sizeof(payloadBuffer) - sizeof(int) - 1), reqPkt->payload);
  if (payload_len >= PACKET_PAYLOAD_MAX) {
    payload_len = PACKET_PAYLOAD_MAX - 1;
  }
  memcpy(reqPkt->payload, payloadBuffer, payload_len + 1);
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
            "ERROR: host%d jobSendRequestHandler could not create waitPkt "
            "parameter\n",
            host_id);
    return;
  }
  memcpy(waitPkt, reqPkt, sizeof(struct Packet));

  struct Job *waitJob = createJob(JOB_WAIT_FOR_RESPONSE, waitPkt);
  if (!waitJob) {
    fprintf(stderr,
            "ERROR: host%d jobSendRequestHandler could not create waitJob "
            "parameter\n",
            host_id);
    return;
  }
  waitJob->request = requestX;
  job_enqueue(host_id, host_q, waitJob);
}
// End of jobSendRequestHandler

void jobSendResponseHandler(int host_id, struct Job *job_from_queue,
                            struct Job_queue *host_q, char *hostDirectory,
                            struct Request **reqList) {
  switch (job_from_queue->packet->type) {
    case PKT_PING_RESPONSE: {
      job_from_queue->type = JOB_SEND_PKT;
      job_enqueue(host_id, host_q, job_from_queue);
      break;
    }

    case PKT_UPLOAD_RESPONSE: {
      // Extract ticket and filename from packet payload
      char *ticket = (char *)malloc(sizeof(char) * TICKETLEN);
      char *filename =
          (char *)malloc(sizeof(char) * PACKET_PAYLOAD_MAX - TICKETLEN - 1);
      parsePacket(job_from_queue->packet->payload, ticket, filename);

      // Create response message buffer
      char responseMsg[MAX_RESPONSE_LEN];
      int responseMsgLen = 0;

      // Check if host directory is valid
      if (!isValidDirectory(hostDirectory)) {
        // Generate response message for invalid directory
        responseMsgLen = snprintf(
            responseMsg, MAX_RESPONSE_LEN,
            "%s:Host%d does not have a valid directory set", ticket, host_id);
      } else {
        // Build full file path from directory and filename
        char *fullPath = (char *)malloc(sizeof(char) * 2 * MAX_FILENAME_LENGTH);
        int fullPathLen = snprintf(fullPath, 2 * MAX_FILENAME_LENGTH, "%s/%s",
                                   hostDirectory, filename);

        // Check if file already exists
        if (fileExists(fullPath)) {
          // Generate response message for existing file
          responseMsgLen =
              snprintf(responseMsg, MAX_RESPONSE_LEN,
                       "%s:File already exists on host%d", ticket, host_id);
        } else {
          // Create file and generate response message for ready state
          FILE *fp;
          fp = fopen(fullPath, "w");
          if (fp == NULL) {
            // Generate response message for file creation error
            responseMsgLen = snprintf(responseMsg, MAX_RESPONSE_LEN,
                                      "%s:file could not be created on host%d",
                                      ticket, host_id);
          } else {
            responseMsgLen =
                snprintf(responseMsg, MAX_RESPONSE_LEN, "%s:Ready", ticket);

            // Create upload request and JOB_WAIT_FOR_RESPONSE
            struct Request *uploadReq = createRequest(UPLOAD_REQ, TIMETOLIVE);
            if (!uploadReq) {
              // Handle error
              fprintf(stderr,
                      "ERROR: host%d jobSendResponseHandler could not create "
                      "upreq\n",
                      host_id);
              return;
            }
            uploadReq->ticket = atoi(ticket);
            uploadReq->state = STATE_PENDING;
            strncpy(uploadReq->filename, filename, MAX_RESPONSE_LEN);
            uploadReq->reqFp = fp;
            addToReqList(reqList, uploadReq);

            struct Packet *uploadPkt =
                createPacket(job_from_queue->packet->src, host_id, PKT_UPLOAD);
            strncpy(uploadPkt->payload, job_from_queue->packet->payload,
                    job_from_queue->packet->length);
            uploadPkt->length = job_from_queue->packet->length;
            struct Job *uploadJob = createJob(JOB_WAIT_FOR_RESPONSE, uploadPkt);
            uploadJob->request = uploadReq;
            job_enqueue(host_id, host_q, uploadJob);
          }
        }
        free(fullPath);
      }

      // Add null terminator to response message buffer
      responseMsg[MAX_RESPONSE_LEN - 1] = '\0';

      // Update response packet payload and length
      struct Packet *responsePacket = job_from_queue->packet;
      if (responseMsgLen > PACKET_PAYLOAD_MAX) {
        // Handle error
      } else {
        strncpy(responsePacket->payload, responseMsg, responseMsgLen);
        responsePacket->payload[responseMsgLen] = '\0';
        responsePacket->length = responseMsgLen;
      }

      // Create and enqueue job to send response packet
      struct Job *responseJob = createJob(JOB_SEND_PKT, responsePacket);
      job_enqueue(host_id, host_q, responseJob);

      // Free dynamically allocated memory
      free(ticket);
      free(filename);
      break;
    }  // End of case PKT_UPLOAD_RESPONSE

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

void jobUploadSendHandler(int host_id, int dst, char ticket[TICKETLEN],
                          char directory[MAX_FILENAME_LENGTH],
                          char fname[MAX_RESPONSE_LEN],
                          struct Job_queue *host_q) {
  printf("jobUploadSendHandler called\n");

  // Open the file in binary mode
  char *fullPath =
      (char *)malloc(sizeof(char) * (MAX_FILENAME_LENGTH + MAX_RESPONSE_LEN));
  snprintf(fullPath, MAX_FILENAME_LENGTH + MAX_RESPONSE_LEN, "%s/%s", directory,
           fname);
  FILE *fp = fopen(fullPath, "rb");
  if (fp == NULL) {
    printf("Error opening file.\n");
    free(fullPath);
    return;
  }

  // Get the file size
  fseek(fp, 0L, SEEK_END);
  long fileSize = ftell(fp);
  fseek(fp, 0L, SEEK_SET);

  // Allocate a buffer for reading data from the file
  int bufferSize = MAX_RESPONSE_LEN;
  char *buffer = (char *)malloc(sizeof(char) * bufferSize);

  // Read and send the file in chunks
  while (fileSize > 0) {
    int chunkSize = fileSize < bufferSize ? fileSize : bufferSize;
    int bytesRead = fread(buffer, sizeof(char), chunkSize, fp);
    if (bytesRead <= 0) {
      break;
    }

    // Create a packet and send it
    struct Packet *pkt = createPacket(host_id, dst, PKT_UPLOAD);
    int pktLen = snprintf(pkt->payload, PACKET_PAYLOAD_MAX, "%s:%.*s", ticket,
                          bytesRead, buffer);
    pkt->length = pktLen;
    job_enqueue(host_id, host_q, createJob(JOB_SEND_PKT, pkt));

    fileSize -= bytesRead;
  }

  // Close the file
  fclose(fp);

  // Notify the receiver that the file transfer is complete
  struct Packet *finishPkt = createPacket(host_id, dst, PKT_UPLOAD_END);
  int finishPktLen =
      snprintf(finishPkt->payload, PACKET_PAYLOAD_MAX, "%s:", ticket);
  finishPkt->length = finishPktLen;
  job_enqueue(host_id, host_q, createJob(JOB_SEND_PKT, finishPkt));

  // Free allocated memory
  free(fullPath);
  free(buffer);
}  // End of jobUploadSendHandler()

void jobUploadReceiveHandler(int host_id, struct Packet *pkt,
                             struct Request **reqList) {
  char *ticket = (char *)malloc(sizeof(char) * TICKETLEN);
  char *msg = (char *)malloc(sizeof(char) * PACKET_PAYLOAD_MAX - TICKETLEN - 1);
  parsePacket(pkt->payload, ticket, msg);

  // printf("jobUploadReceiveHandler: ticket: %s\n", ticket);
  // printf("jobUploadReceiveHandler: msg: %s\n", msg);

  struct Request *r = findRequestByStringTicket(*reqList, ticket);
  if (r != NULL) {
    // printf("jobUploadReceiveHandler: found request\n");
    // printf("jobUploadReceiveHandler: r->filename: %s\n", r->filename);

    // Refresh timeToLive of request
    r->timeToLive = TIMETOLIVE;

    if (r->reqFp == NULL) {
      // Open the file in append mode if it hasn't been opened already
      r->reqFp = fopen(r->filename, "ab");
      if (r->reqFp == NULL) {
        printf("Failed to open file %s\n", r->filename);
      } else {
        // printf("jobUploadReceiveHandler: opened file\n");
      }
    }

    if (r->reqFp != NULL) {
      // Write message to the file
      size_t msg_len = strlen(msg);
      fwrite(msg, sizeof(char), msg_len, r->reqFp);

      fflush(r->reqFp);  // Flush the file buffer to make sure data is written
                         // to disk

      // printf("jobUploadReceiveHandler: wrote message to file\n");
    }
  } else {
    printf("Request not found for ticket %s\n", ticket);
  }

  free(ticket);
  free(msg);
  // printf("jobUploadReceiveHandler: exiting\n");
}

void jobWaitForResponseHandler(int host_id, struct Job *job_from_queue,
                               struct Job_queue *host_q, char *hostDirectory,
                               struct Request **reqList, int manFd) {
  // Grab the request ticket from the job_from_queue->packet->payload
  char *ticket = (char *)malloc(sizeof(char) * TICKETLEN);
  char *msg = (char *)malloc(sizeof(char) * PACKET_PAYLOAD_MAX - TICKETLEN - 1);
  parsePacket(job_from_queue->packet->payload, ticket, msg);

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
                   "\x1b[31;1mPing request timed out!\x1b[0m");
          sendMsgToManager(manFd, responseMsg);
          break;
        case UPLOAD_REQ:
          snprintf(responseMsg, MAX_MSG_LENGTH, "Upload request timed out!");
          sendMsgToManager(manFd, responseMsg);
          break;
        case DOWNLOAD_REQ:
          snprintf(responseMsg, MAX_MSG_LENGTH, "Download request timed out!");
          sendMsgToManager(manFd, responseMsg);
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

      if (r->state == STATE_PENDING) {
        job_enqueue(host_id, host_q, job_from_queue);
      } else {
        switch (r->type) {
          case PING_REQ:
            if (r->state == STATE_COMPLETE) {
              snprintf(responseMsg, MAX_MSG_LENGTH,
                       "\x1b[32;1mPing request acknowledged!\x1b[0m");
              sendMsgToManager(manFd, responseMsg);
              deleteFromReqList(*reqList, r->ticket);
              destroyJob(job_from_queue);
            }
            break;

          case UPLOAD_REQ:
            if (r->state == STATE_READY) {
              jobUploadSendHandler(host_id, job_from_queue->packet->dst, ticket,
                                   hostDirectory, msg, host_q);
              snprintf(responseMsg, MAX_MSG_LENGTH, "OK");
              sendMsgToManager(manFd, responseMsg);

            } else if (r->state == STATE_ERROR) {
              snprintf(responseMsg, MAX_MSG_LENGTH, "%s", r->errorMsg);
              sendMsgToManager(manFd, responseMsg);

            } else if (r->state == STATE_COMPLETE) {
              printf("UPLOAD_REQ complete\n");
              fclose(r->reqFp);
              deleteFromReqList(*reqList, r->ticket);
              destroyJob(job_from_queue);
            }
            break;

          case DOWNLOAD_REQ:
            break;
        }
      }
    }

  } else {
    printf("Request could not be found in list\n");
  }
  // Clean up memory allocation
  free(ticket);
  free(msg);
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
            write(man_port->send_fd, "", sizeof(""));
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
            write(man_port->send_fd, "", sizeof(""));
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

          case PKT_UPLOAD:
            jobUploadReceiveHandler(host_id, received_packet, &requestList);
            break;

          case PKT_UPLOAD_END:
            char *ticket = (char *)malloc(sizeof(char) * TICKETLEN);
            char *msg = (char *)malloc(sizeof(char) * TICKETLEN);
            parsePacket(received_packet->payload, ticket, msg);
            struct Request *r = findRequestByStringTicket(requestList, ticket);

            if (r == NULL) {
              fprintf(stderr,
                      "ERROR: Host%d Packet Handler, PKT_UPLOAD_END could not "
                      "find response\n",
                      host_id);
            } else {
              // Response with ticket value was found
              printf("PKT_UPLOAD_END: setting %s to STATE_COMPLETE\n", ticket);
              r->state = STATE_COMPLETE;
            }
            free(ticket);
            free(msg);
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
                                   hostDirectory, &requestList);
            break;
          }  //////////////// End of case JOB_SEND_RESPONSE

          case JOB_SEND_PKT: {
            sendPacketTo(node_port_array, node_port_array_size,
                         job_from_queue->packet);
            destroyJob(job_from_queue);
            break;
          }  //////////////// End of case JOB_SEND_PKT

          case JOB_WAIT_FOR_RESPONSE: {
            jobWaitForResponseHandler(host_id, job_from_queue, &host_q,
                                      hostDirectory, &requestList,
                                      man_port->send_fd);
            break;
          }  //////////////// End of case JOB_WAIT_FOR_RESPONSE

          case JOB_UPLOAD: {
            // jobUploadSendHandler();
            break;
          }  //////////////// End of case JOB_UPLOAD

          case JOB_DOWNLOAD: {
            // jobUploadSendHandler();
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
