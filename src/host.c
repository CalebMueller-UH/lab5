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

#define WATCHDOG_TIMEOUT_SEC 3

// Helper Function Forward Declarations
void commandDownloadHandler(int host_id, struct JobQueue *hostq,
                            char *hostDirectory, int dst, char *fname,
                            int manFd);

void commandUploadHandler(int host_id, struct JobQueue *hostq,
                          char *hostDirectory, int dst, char *fname, int manFd);

void jobSendRequestHandler(int host_id, struct JobQueue *hostq,
                           struct Job *job_from_queue, struct Net_port **arr,
                           int arrSize);

void jobSendResponseHandler(int host_id, struct JobQueue *hostq,
                            char *hostDirectory, struct Job *job_from_queue,
                            struct Net_port **arr, int arrSize, int manFd);

void jobSendDownloadResponseHandler(int host_id, struct JobQueue *hostq,
                                    char *hostDirectory,
                                    struct Job *job_from_queue,
                                    struct Net_port **arr, int arrSize);

void jobSendUploadResponseHandler(int host_id, struct JobQueue *hostq,
                                  char *hostDirectory,
                                  struct Job *job_from_queue,
                                  struct Net_port **arr, int arrSize);

void jobUploadSendHandler(int host_id, struct JobQueue *hostq,
                          struct Job *job_from_queue, struct Net_port **arr,
                          int arrSize);

void pktUploadReceive(int host_id, struct Packet *pkt, struct JobQueue *hostq);

void pktUploadEnd(int host_id, struct Packet *pkt, struct JobQueue *hostq);

void jobWaitForResponseHandler(int host_id, struct Job *job_from_queue,
                               struct JobQueue *hostq, char *hostDirectory,
                               struct Net_port **arr, int arrSize, int manFd);

void parsePacket(const char *inputStr, char *ticketStr, char *dataStr);

void pktIncomingRequest(int host_id, struct JobQueue *hostq,
                        struct Packet *inPkt);

void pktIncomingResponse(struct Packet *inPkt, struct JobQueue *hostq);

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
      char *responseMsg = (char *)malloc(sizeof(char) * MAX_MSG_LENGTH);

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
            colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_YELLOW,
                          "Can not ping self");
            sendMsgToManager(man_port->send_fd, responseMsg);
            break;
          }

          // Create ping request packet
          struct Packet *preqPkt =
              createPacket(host_id, dst, PKT_PING_REQ, 0, NULL);

          // Create send request job
          struct Job *sendReqJob = job_create(
              NULL, TIMETOLIVE, JOB_SEND_REQUEST, JOB_PENDING_STATE, preqPkt);
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
          int dst;
          char fname[MAX_FILENAME_LENGTH] = {0};
          sscanf(man_msg, "%d %s", &dst, fname);
          int fnameLen = strnlen(fname, MAX_FILENAME_LENGTH);
          fname[fnameLen] = '\0';
          commandDownloadHandler(host_id, &hostq, hostDirectory, dst, fname,
                                 man_port->send_fd);

          break;
        }  //////////////// End of case 'd'

        default:;
      }
      free(responseMsg);
    }  // End of command

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
            pktIncomingRequest(host_id, &hostq, inPkt);
            break;

            ////////////////
          case PKT_PING_RESPONSE:
          case PKT_UPLOAD_RESPONSE:
          case PKT_DOWNLOAD_RESPONSE:
            pktIncomingResponse(inPkt, &hostq);
            break;

          case PKT_UPLOAD:
            pktUploadReceive(host_id, inPkt, &hostq);
            break;

          case PKT_UPLOAD_END:
            pktUploadEnd(host_id, inPkt, &hostq);
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
            jobSendResponseHandler(host_id, &hostq, hostDirectory,
                                   job_from_queue, node_port_array,
                                   node_port_array_size, man_port->send_fd);
            break;
          }  //////////////// End of case JOB_SEND_RESPONSE

          case JOB_SEND_PKT: {
            sendPacketTo(node_port_array, node_port_array_size,
                         job_from_queue->packet);
            job_delete(host_id, job_from_queue);
            break;
          }  //////////////// End of case JOB_SEND_PKT

          case JOB_WAIT_FOR_RESPONSE: {
            jobWaitForResponseHandler(host_id, job_from_queue, &hostq,
                                      hostDirectory, node_port_array,
                                      node_port_array_size, man_port->send_fd);
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
    }  // End of for (int portNum = 0; portNum ...

  }  // End of while(1)
}  // End of host_main()

//////////////////////////////////////////////////
//////////////////////////////////////////////////
//////////////////////////////////////////////////
//////////////////////////////////////////////////
//////////////////////////////////////////////////
//////////////////////////////////////////////////
//////////////// HELPER FUNCTIONS ////////////////

void commandDownloadHandler(int host_id, struct JobQueue *hostq,
                            char *hostDirectory, int dst, char *fname,
                            int manFd) {
  char *responseMsg = malloc(sizeof(char) * MAX_MSG_LENGTH);
  memset(responseMsg, 0, MAX_MSG_LENGTH);

  if (!isValidDirectory(hostDirectory)) {
    colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED,
                  "Host %d does not have a valid directory set", host_id);
  } else if (dst == host_id) {
    colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED,
                  "Cannot upload to self");
  } else {
    char fullPath[2 * MAX_FILENAME_LENGTH] = {0};
    snprintf(fullPath, sizeof(fullPath), "%s/%s", hostDirectory, fname);

    if (fileExists(fullPath)) {
      colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED,
                    "This file already exists in %s", hostDirectory);
    } else {
      // Directory is set, and file does not already exist

      // Create a download request packet
      struct Packet *dwnReqPkt =
          createPacket(host_id, dst, PKT_DOWNLOAD_REQ, 0, fname);
      // Create a send request job
      struct Job *sendReqJob = job_create(NULL, TIMETOLIVE, JOB_SEND_REQUEST,
                                          JOB_PENDING_STATE, dwnReqPkt);
      strncpy(sendReqJob->filepath, fullPath, sizeof(fullPath));

      job_enqueue(host_id, hostq, sendReqJob);
    }
  }

  sendMsgToManager(manFd, responseMsg);
  free(responseMsg);
}  // End of commandDownloadHandler()

void commandUploadHandler(int host_id, struct JobQueue *hostq,
                          char *hostDirectory, int dst, char *fname,
                          int manFd) {
  char *responseMsg = malloc(sizeof(char) * MAX_MSG_LENGTH);
  memset(responseMsg, 0, MAX_MSG_LENGTH);

  if (!isValidDirectory(hostDirectory)) {
    colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED,
                  "Host %d does not have a valid directory set", host_id);
  } else if (dst == host_id) {
    colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED,
                  "Cannot upload to self");
  } else {
    char fullPath[2 * MAX_FILENAME_LENGTH] = {0};
    snprintf(fullPath, sizeof(fullPath), "%s/%s", hostDirectory, fname);

    if (!fileExists(fullPath)) {
      colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED,
                    "This file does not exist in %s", hostDirectory);
    } else {
      // Directory is set, and file exists

      // Open file to read from
      FILE *fp = fopen(fullPath, "rb");

      // Create an upload request packet
      struct Packet *upReqPkt =
          createPacket(host_id, dst, PKT_UPLOAD_REQ, 0, fname);
      // Create a send request job
      struct Job *sendReqJob = job_create(NULL, TIMETOLIVE, JOB_SEND_REQUEST,
                                          JOB_PENDING_STATE, upReqPkt);
      sendReqJob->fp = fp;
      strncpy(sendReqJob->filepath, fullPath, sizeof(sendReqJob->filepath));
      // Enque job
      job_enqueue(host_id, hostq, sendReqJob);
    }
  }

  sendMsgToManager(manFd, responseMsg);
  free(responseMsg);
}  // End of commandUploadHandler()

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

}  // End of jobSendRequestHandler

void jobSendResponseHandler(int host_id, struct JobQueue *hostq,
                            char *hostDirectory, struct Job *job_from_queue,
                            struct Net_port **arr, int arrSize, int manFd) {
  // printf("host%d: jobSendResponseHandler\n", host_id);
  // printJob(job_from_queue);

  switch (job_from_queue->packet->type) {
    case PKT_PING_RESPONSE:
      sendPacketTo(arr, arrSize, job_from_queue->packet);
      break;
    case PKT_UPLOAD_RESPONSE:
      jobSendUploadResponseHandler(host_id, hostq, hostDirectory,
                                   job_from_queue, arr, arrSize);
      break;
    case PKT_DOWNLOAD_RESPONSE: {
      jobSendDownloadResponseHandler(host_id, hostq, hostDirectory,
                                     job_from_queue, arr, arrSize);
      break;
    }
  }
}  // End of jobSendResponseHandler()

void jobSendDownloadResponseHandler(int host_id, struct JobQueue *hostq,
                                    char *hostDirectory,
                                    struct Job *job_from_queue,
                                    struct Net_port **arr, int arrSize) {
  // printf("host%d jobSendDownloadResponseHandler\n", host_id);
  // printJob(job_from_queue);

  struct Packet *qPkt = job_from_queue->packet;

  char *id = (char *)malloc(sizeof(char) * JIDLEN);
  char *fname = (char *)malloc(sizeof(char) * MAX_RESPONSE_LEN);
  parsePacket(qPkt->payload, id, fname);

  char fullPath[2 * MAX_FILENAME_LENGTH] = {0};

  char *payloadMsg = malloc(sizeof(char) * MAX_MSG_LENGTH);
  memset(payloadMsg, 0, MAX_MSG_LENGTH);

  int readyFlag = 0;

  if (!isValidDirectory(hostDirectory)) {
    snprintf(payloadMsg, MAX_MSG_LENGTH,
             "%s:Host %d does not have a valid directory set", id, host_id);
  } else {
    snprintf(fullPath, sizeof(fullPath), "%s/%s", hostDirectory, fname);

    if (!fileExists(fullPath)) {
      snprintf(payloadMsg, MAX_MSG_LENGTH, "%s:This file does not exist in %s",
               id, hostDirectory);
    } else {
      // Directory is set, and file exists
      snprintf(payloadMsg, PACKET_PAYLOAD_MAX, "%s:Ready", id);
      FILE *fp = fopen(fullPath, "rb");
      job_from_queue->fp = fp;
      strncpy(job_from_queue->filepath, fullPath, sizeof(fullPath));
      readyFlag = 1;
    }
  }
  int payloadMsgLen = strnlen(payloadMsg, PACKET_PAYLOAD_MAX);
  memset(qPkt->payload, 0, PACKET_PAYLOAD_MAX);
  memcpy(qPkt->payload, payloadMsg, payloadMsgLen);
  qPkt->length = payloadMsgLen;

  sendPacketTo(arr, arrSize, qPkt);

  if (readyFlag) {
    printf("jobUploadSendHandler about to be called\n");
    jobUploadSendHandler(host_id, hostq, job_from_queue, arr, arrSize);
  }

  // Clean up memory
  free(id);
  free(fname);
  free(payloadMsg);

}  // End of jobSendDownloadResponseHandler()

void jobSendUploadResponseHandler(int host_id, struct JobQueue *hostq,
                                  char *hostDirectory,
                                  struct Job *job_from_queue,
                                  struct Net_port **arr, int arrSize) {
  struct Packet *qPkt = job_from_queue->packet;

  char *id = (char *)malloc(sizeof(char) * JIDLEN);
  char *fname = (char *)malloc(sizeof(char) * MAX_RESPONSE_LEN);
  parsePacket(qPkt->payload, id, fname);

  char fullPath[2 * MAX_FILENAME_LENGTH] = {0};

  char *payloadMsg = malloc(sizeof(char) * MAX_MSG_LENGTH);
  memset(payloadMsg, 0, MAX_MSG_LENGTH);

  if (!isValidDirectory(hostDirectory)) {
    snprintf(payloadMsg, MAX_MSG_LENGTH,
             "%s:Host %d does not have a valid directory set", id, host_id);
  } else {
    snprintf(fullPath, sizeof(fullPath), "%s/%s", hostDirectory, fname);

    if (fileExists(fullPath)) {
      snprintf(payloadMsg, MAX_MSG_LENGTH, "%s:This file already exists in %s!",
               id, hostDirectory);
    } else {
      // Directory is set, and file exists
      snprintf(payloadMsg, PACKET_PAYLOAD_MAX, "%s:Ready", id);

      FILE *fp = fopen(fullPath, "w");
      job_from_queue->fp = fp;
      strncpy(job_from_queue->filepath, fullPath,
              strnlen(fullPath, MAX_FILENAME_LENGTH * 2));
      job_from_queue->type = JOB_WAIT_FOR_RESPONSE;
      job_enqueue(host_id, hostq, job_from_queue);
    }
  }
  int payloadMsgLen = strnlen(payloadMsg, PACKET_PAYLOAD_MAX);
  memset(qPkt->payload, 0, PACKET_PAYLOAD_MAX);
  memcpy(qPkt->payload, payloadMsg, payloadMsgLen);
  qPkt->length = payloadMsgLen;

  sendPacketTo(arr, arrSize, qPkt);

  // Clean up memory
  free(id);
  free(fname);
  free(payloadMsg);
}  // End of jobSendUploadResponseHandler()

void pktUploadReceive(int host_id, struct Packet *pkt, struct JobQueue *hostq) {
  char *id = (char *)malloc(sizeof(char) * JIDLEN);
  char *msg = (char *)malloc(sizeof(char) * MAX_RESPONSE_LEN);
  parsePacket(pkt->payload, id, msg);

  struct Job *rjob = job_queue_find_id(hostq, id);
  if (rjob != NULL) {
    // Refresh timeToLive of request
    rjob->timeToLive = TIMETOLIVE;

    if (rjob->fp == NULL) {
      // Open the file in append mode if it hasn't been opened already
      rjob->fp = fopen(rjob->filepath, "ab");
    }

    if (rjob->fp != NULL) {
      // Write message to the file
      size_t msg_len = strlen(msg);
      fwrite(msg, sizeof(char), msg_len, rjob->fp);

      // Flush the file buffer to make sure data is written to disk
      fflush(rjob->fp);
    }
  } else {
    printf("Request not found for ticket %s\n", id);
  }

  free(id);
  free(msg);
}  // End of pktUploadReceive()

void pktUploadEnd(int host_id, struct Packet *pkt, struct JobQueue *hostq) {
  char *id = (char *)malloc(sizeof(char) * JIDLEN);
  char *msg = (char *)malloc(sizeof(char) * MAX_RESPONSE_LEN);
  parsePacket(pkt->payload, id, msg);

  struct Job *r = job_queue_find_id(hostq, id);
  if (r != NULL) {
    r->state = JOB_COMPLETE_STATE;
  } else {
    printf("Request not found for ticket %s\n", id);
  }

  free(id);
  free(msg);
}  // End of pktUploadEnd()

void jobUploadSendHandler(int host_id, struct JobQueue *hostq,
                          struct Job *job_from_queue, struct Net_port **arr,
                          int arrSize) {
  // printf("host%d: jobUploadSendHandler\n", host_id);
  // printJob(job_from_queue);

  char *id = (char *)malloc(sizeof(char) * JIDLEN);
  char *msg = (char *)malloc(sizeof(char) * MAX_RESPONSE_LEN);
  parsePacket(job_from_queue->packet->payload, id, msg);
  int dst = job_from_queue->packet->dst;
  int src = host_id;

  FILE *fp = job_from_queue->fp;

  // Get the file size
  fseek(fp, 0L, SEEK_END);
  long fileSize = ftell(fp);
  fseek(fp, 0L, SEEK_SET);

  // Allocate a buffer for reading data from the file
  int bufferSize = MAX_RESPONSE_LEN - 1;
  char *buffer = (char *)malloc(sizeof(char) * bufferSize);

  // Read and send the file in chunks
  while (fileSize > 0) {
    int chunkSize = fileSize < bufferSize ? fileSize : bufferSize;
    int bytesRead = fread(buffer, sizeof(char), chunkSize, fp);
    if (bytesRead <= 0) {
      break;
    }

    buffer[bytesRead] = '\0';

    struct Packet *p = createPacket(src, dst, PKT_UPLOAD, 0, buffer);
    struct Job *j = job_create(id, 0, JOB_SEND_PKT, JOB_COMPLETE_STATE, p);
    job_enqueue(host_id, hostq, j);

    fileSize -= bytesRead;
    usleep(5000);
  }

  usleep(10000);
  // Notify the receiver that the file transfer is complete
  struct Packet *finPkt = createPacket(src, dst, PKT_UPLOAD_END, 0, NULL);
  struct Job *finJob =
      job_create(id, TIMETOLIVE, JOB_SEND_PKT, JOB_COMPLETE_STATE, finPkt);
  job_enqueue(host_id, hostq, finJob);

  // Free allocated memory
  free(id);
  free(msg);
  free(buffer);
}  // End of jobUploadSendHandler()

void jobWaitForResponseHandler(int host_id, struct Job *job,
                               struct JobQueue *hostq, char *hostDirectory,
                               struct Net_port **arr, int arrSize, int manFd) {
  // Uncomment to print debug info
  // colorPrint(BOLD_GREY, "\nhost%d: jobWaitForResponseHandler called\n",
  //            host_id);
  // printJob(job);
  // printf("\n");

  char *responseMsg = malloc(sizeof(char) * MAX_MSG_LENGTH);
  memset(responseMsg, 0, MAX_MSG_LENGTH);

  if (job->timeToLive <= 0) {
    // Handle expired job
    switch (job->packet->type) {
      case PKT_PING_REQ:
        colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED,
                      "Ping request timed out!");
        break;
      case PKT_UPLOAD_REQ:
        colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED,
                      "Upload request timed out!");
        break;
      case PKT_DOWNLOAD_REQ:
        colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED,
                      "Download request timed out!");
        break;
    }
    sendMsgToManager(manFd, responseMsg);
    job_delete(host_id, job);

  } else {
    // Handle pending job
    job->timeToLive--;
    if (job->state == JOB_PENDING_STATE) {
      // Re-enqueue job while ttl > 0
      job_enqueue(host_id, hostq, job);
    } else {
      switch (job->packet->type) {
        case PKT_PING_REQ:
          if (job->state == JOB_COMPLETE_STATE) {
            colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_GREEN,
                          "Ping request acknowledged!");
            sendMsgToManager(manFd, responseMsg);
            job_delete(host_id, job);
          }
          break;

        case PKT_UPLOAD_REQ:
          if (job->state == JOB_READY_STATE) {
            // Send file upload packets
            jobUploadSendHandler(host_id, hostq, job, arr, arrSize);
            job->state = JOB_COMPLETE_STATE;
            job_enqueue(host_id, hostq, job);
          } else if (job->state == JOB_ERROR_STATE) {
            colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED, "%s",
                          job->errorMsg);
            sendMsgToManager(manFd, responseMsg);
            job_delete(host_id, job);
          } else if (job->state == JOB_COMPLETE_STATE) {
            fclose(job->fp);
            job_delete(host_id, job);
            colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_GREEN,
                          "Upload Complete");
            sendMsgToManager(manFd, responseMsg);
          }
          break;

        case PKT_DOWNLOAD_REQ:
          if (job->state == JOB_READY_STATE) {
            job_enqueue(host_id, hostq, job);
          } else if (job->state == JOB_ERROR_STATE) {
            colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED, "%s",
                          job->errorMsg);
            sendMsgToManager(manFd, responseMsg);
            job_delete(host_id, job);
          } else if (job->state == JOB_COMPLETE_STATE) {
            colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_GREEN,
                          "Download Complete");
            sendMsgToManager(manFd, responseMsg);
            job_delete(host_id, job);
          }
          break;
      }
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

void pktIncomingRequest(int host_id, struct JobQueue *hostq,
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

  struct Job *sendRespJob =
      job_create(id, TIMETOLIVE, JOB_SEND_RESPONSE, JOB_PENDING_STATE, inPkt);
  job_enqueue(host_id, hostq, sendRespJob);

  free(id);
  free(msg);
}  // End of pktIncomingRequest()

void pktIncomingResponse(struct Packet *inPkt, struct JobQueue *hostq) {
  // Grab jid from request packet payload
  char *id = (char *)malloc(sizeof(char) * JIDLEN);
  char *msg = (char *)malloc(sizeof(char) * MAX_RESPONSE_LEN);
  parsePacket(inPkt->payload, id, msg);

  // Look for job id in job queue
  struct Job *waitJob = job_queue_find_id(hostq, id);
  if (waitJob != NULL) {
    // job id was found in queue

    switch (inPkt->type) {
      case PKT_PING_RESPONSE:
        // Ping request acknowledged
        waitJob->state = JOB_COMPLETE_STATE;
        break;
      case PKT_UPLOAD_RESPONSE:
        if (strncmp(msg, "Ready", sizeof("Ready")) == 0) {
          waitJob->state = JOB_READY_STATE;
        } else {
          strncpy(waitJob->errorMsg, msg, strnlen(msg, MAX_RESPONSE_LEN));
          waitJob->state = JOB_ERROR_STATE;
        }
        break;

      case PKT_DOWNLOAD_RESPONSE:
        if (strncmp(msg, "Ready", sizeof("Ready")) == 0) {
          // banana
          FILE *fp = fopen(waitJob->filepath, "w");
          waitJob->fp = fp;
          waitJob->state = JOB_READY_STATE;
        } else {
          strncpy(waitJob->errorMsg, msg, strnlen(msg, MAX_RESPONSE_LEN));
          waitJob->state = JOB_ERROR_STATE;
        }
        break;
    }
  } else {
    // job id was not found in queue
    colorPrint(BOLD_YELLOW,
               "Host%d received a response with an unrecognized job id\n",
               inPkt->dst);
  }

  // Clean up packet and dynamic vars
  packet_delete(inPkt);
  free(id);
  free(msg);
}  // End of pktIncomingResponse()

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
