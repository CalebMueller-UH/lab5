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

struct Job *makeRequestJob(int host_id, char man_msg[MAX_MSG_LENGTH],
                           char *reqPayload) {
  int dst;
  sscanf(man_msg, "%d", &dst);
  struct Packet *reqPkt =
      createPacket(host_id, dst, PKT_REQ, strlen(reqPayload), reqPayload);
  return createJob(JOB_REQUEST, reqPkt);
}

enum reqType { PING, UPLOAD, DOWNLOAD, INVALID_REQ_TYPE };
enum reqType getReqType(char str[PACKET_PAYLOAD_MAX]) {
  if (strncmp(str, "PING", strlen("PING")) == 0) {
    return PING;
  } else if (strncmp(str, "UPLOAD", strlen("UPLOAD")) == 0) {
    return UPLOAD;
  } else if (strncmp(str, "DOWNLOAD", strlen("DOWNLOAD")) == 0) {
    return DOWNLOAD;
  } else {
    return INVALID_REQ_TYPE;
  }
}

////////////////////////////////////////////////
////////////////// HOST MAIN ///////////////////
void host_main(int host_id) {
  ////// Initialize state of host //////
  char hostDirectory[MAX_FILENAME_LENGTH];

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
  struct Net_port **node_port_array;  // Array of pointers to node ports
  int node_port_array_size;           // Number of node ports
  node_port_list = net_get_port_list(host_id);
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
  struct Job_queue host_q;
  job_queue_init(&host_q);

  /* Initialize response list */
  struct Response *responseList = NULL;
  int responseListIdNum = 0;

  //   while (1) {
  //     ////////////////////////////////////////////////////////////////////////////
  //     ////////////////////////////// COMMAND HANDLER
  //     ///////////////////////////// char man_msg[MAX_MSG_LENGTH]; char
  //     man_cmd; int n = get_man_command(man_port, man_msg, &man_cmd);
  //     /* Execute command */
  //     if (n > 0) {
  //       // sem_wait(&console_print_access);

  //       switch (man_cmd) {
  //         case 's':
  //           // Display host state
  //           reply_display_host_state(man_port, hostDirectory,
  //                                    isValidDirectory(hostDirectory),
  //                                    host_id);
  //           break;
  //         case 'm':
  //           // Change Active Host's hostDirectory
  //           size_t len = strnlen(man_msg, MAX_FILENAME_LENGTH - 1);
  //           if (isValidDirectory(man_msg)) {
  //             memcpy(hostDirectory, man_msg, len);
  //             hostDirectory[len] = '\0';  // add null character
  //             colorPrint(BOLD_GREEN, "Host%d's main directory set to %s\n",
  //                        host_id, man_msg);
  //           } else {
  //             colorPrint(BOLD_RED, "%s is not a valid directory\n", man_msg);
  //           }
  //           break;
  //         case 'p':
  //           ////// Ping //////
  //           struct Job *pingReqJob = makeRequestJob(host_id, man_msg,
  //           "PING"); job_enqueue(host_id, &host_q, pingReqJob); break;
  //         case 'u':
  //           ////// Upload //////
  //           struct Job *uploadReqJob = makeRequestJob(host_id, man_msg,
  //           "UPLOAD"); job_enqueue(host_id, &host_q, uploadReqJob); break;
  //         case 'd':
  //           ////// Download //////
  //           struct Job *downloadReqJob =
  //               makeRequestJob(host_id, man_msg, "DOWNLOAD");
  //           job_enqueue(host_id, &host_q, downloadReqJob);
  //           break;
  //         default:;
  //       }
  //       // Release console_print_access semaphore
  //       // sem_signal(&console_print_access);
  //     }

  //     ////////////////////////////////////////////////////////////////////////////
  //     ////////////////////////////// COMMAND HANDLER
  //     /////////////////////////////

  //     //
  //     -------------------------------------------------------------------------

  //     ////////////////////////////////////////////////////////////////////////////
  //     ////////////////////////////// PACKET HANDLER
  //     //////////////////////////////
  //     // Receive packets for all ports in node_port_array
  //     for (int portNum = 0; portNum < node_port_array_size; portNum++) {
  //       struct Packet *received_packet = createEmptyPacket();
  //       n = packet_recv(node_port_array[portNum], received_packet);

  //       // if portNum has received a packet, translate the packet into a job
  //       if ((n > 0) && ((int)received_packet->dst == host_id)) {
  // #ifdef DEBUG
  //         colorPrint(YELLOW,
  //                    "DEBUG: id:%d host_main: Host %d received packet of type
  //                    "
  //                    "%s\n",
  //                    host_id, (int)received_packet->dst,
  //                    get_packet_type_literal(received_packet->type));
  // #endif
  //         struct Job *job_from_pkt = createEmptyJob();
  //         job_from_pkt->packet = received_packet;

  //         switch (received_packet->type) { default: }
  //       } else {
  //         free(received_packet);
  //       }
  //     }

  //     ////////////////////////////// PACKET HANDLER
  //     //////////////////////////////
  //     ////////////////////////////////////////////////////////////////////////////

  //     //
  //     -------------------------------------------------------------------------

  //     ////////////////////////////////////////////////////////////////////////////
  //     //////////////////////////////// JOB HANDLER
  //     ///////////////////////////////

  //     if (job_queue_length(&host_q) > 0) {
  //       /* Get a new job from the job queue */
  //       struct Job *job_from_queue = job_dequeue(host_id, &host_q);

  //       //////////// EXECUTE FETCHED JOB ////////////
  //       switch (job_from_queue->type) {
  //         case JOB_REQUEST:
  //           /////////////////////// REQUEST HANDLER ///////////////////////
  //           enum reqType rt = getReqType(job_from_queue->packet->payload);
  //           switch (rt) {
  //             case PING:
  //               break;
  //             case UPLOAD:
  //               break;
  //             case DOWNLOAD:
  //               break;
  //             default:
  //           }
  //           break;
  //         default:
  // #ifdef DEBUG
  //           colorPrint(YELLOW,
  //                      "DEBUG: id:%d host_main: job_handler defaulted with "
  //                      "job type: "
  //                      "%s\n",
  //                      host_id, get_job_type_literal(job_from_queue->type));
  // #endif
  //       }
  //     }

  //     //////////////////////////////// JOB HANDLER
  //     ///////////////////////////////
  //     ////////////////////////////////////////////////////////////////////////////

  //     /* The host goes to sleep for 10 ms */
  //     usleep(LOOP_SLEEP_TIME_MS);

  //   } /* End of while loop */

  //////////// WORKING LOOP ////////////
  int ping_reply_received = 0;
  while (1) {
    /* Execute command from manager, if any */

    //////////// COMMAND HANDLER ////////////
    char man_msg[MAX_MSG_LENGTH];
    char man_cmd;
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

        case 'm':
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
          // Set up a Response
          int resId = responseListIdNum++;
          struct Response *r = createResponse(resId, PKT_PING_REQ, TIMETOLIVE);
          addToResList(responseList, r);
          char resIdStr[PACKET_PAYLOAD_MAX];
          snprintf(resIdStr, PACKET_PAYLOAD_MAX, "rid:%d", resId);
          int resIdStrLen = strnlen(resIdStr, PACKET_PAYLOAD_MAX);

          // Send ping request
          int dst;
          sscanf(man_msg, "%d", &dst);
          struct Packet *pingReqPkt = createEmptyPacket();
          pingReqPkt->src = (char)host_id;
          pingReqPkt->dst = (char)dst;
          pingReqPkt->type = (char)PKT_PING_REQ;
          pingReqPkt->length = resIdStrLen;
          strncpy(pingReqPkt->payload, resIdStr, PACKET_PAYLOAD_MAX);
          struct Job *pingReqJob = createEmptyJob();
          pingReqJob->packet = pingReqPkt;
          pingReqJob->type = JOB_BROADCAST_PKT;
          job_enqueue(host_id, &host_q, pingReqJob);

          struct Packet *waitPacket = createEmptyPacket();
          waitPacket->dst = (char)dst;
          waitPacket->src = (char)host_id;
          waitPacket->type = PKT_PING_REQ;
          pingReqPkt->length = resIdStrLen;
          strncpy(pingReqPkt->payload, resIdStr, PACKET_PAYLOAD_MAX);
          struct Job *waitJob = createEmptyJob();
          waitJob->type = JOB_WAIT_FOR_RESPONSE;
          waitJob->timeToLive = TIMETOLIVE;
          waitJob->packet = waitPacket;
          job_enqueue(host_id, &host_q, waitJob);
          ping_reply_received = 0;
          break;

          // case 'u': /* Upload a file to a host */
          //   // Check to see if hostDirectory is valid
          //   if (!isValidDirectory(hostDirectory)) {
          //     colorPrint(BOLD_RED, "Host%d does not have a valid directory
          //     set\n",
          //                host_id);
          //     break;
          //   }
          //   char filePath[MAX_FILENAME_LENGTH] = {0};
          //   sscanf(man_msg, "%d %s", &dst, filePath);
          //   int filePathLen = strnlen(filePath, MAX_FILENAME_LENGTH);
          //   // Concatenate hostDirectory and filePath with a slash in
          // between
          //   char fullPath[2 * MAX_FILENAME_LENGTH] = {0};
          //   snprintf(fullPath, (2 * MAX_FILENAME_LENGTH), "%s/%s",
          //   hostDirectory,
          //            filePath);
          //   // Check to see if fullPath points to a valid file
          //   if (!isValidFile(fullPath)) {
          //     colorPrint(BOLD_RED, "This file does not exist\n", host_id);
          //     break;
          //   }
          //   // User input is valid, enqueue File upload job
          //   struct Job *fileUploadJob = createEmptyJob();
          //   fileUploadJob->type = JOB_FILE_UPLOAD_SEND;
          //   fileUploadJob->file_upload_dst = dst;
          //   strncpy(fileUploadJob->fname_upload, filePath, filePathLen);
          //   fileUploadJob->fname_upload[filePathLen] = '\0';
          //   job_enqueue(host_id, &host_q, fileUploadJob);
          //   break;

        case 'u': /* Upload a file to a host */
          // Check to see if hostDirectory is valid
          if (!isValidDirectory(hostDirectory)) {
            colorPrint(BOLD_RED, "Host%d does not have a valid directory set\n",
                       host_id);
            break;
          }
          char filePath[MAX_FILENAME_LENGTH] = {0};
          sscanf(man_msg, "%d %s", &dst, filePath);
          int filePathLen = strnlen(filePath, MAX_FILENAME_LENGTH);
          // Concatenate hostDirectory and filePath with a slash in between
          char fullPath[2 * MAX_FILENAME_LENGTH] = {0};
          snprintf(fullPath, (2 * MAX_FILENAME_LENGTH), "%s/%s", hostDirectory,
                   filePath);
          // Check to see if fullPath points to a valid file
          if (!isValidFile(fullPath)) {
            colorPrint(BOLD_RED, "This file does not exist\n", host_id);
            break;
          }
          // User input is valid, enqueue upload request to verify destination
          struct Job *fileUploadReqJob = createEmptyJob();
          fileUploadReqJob->type = JOB_FILE_UPLOAD_REQ;
          fileUploadReqJob->file_upload_dst = dst;
          strncpy(fileUploadReqJob->fname_upload, filePath, filePathLen);
          fileUploadReqJob->fname_upload[filePathLen] = '\0';
          job_enqueue(host_id, &host_q, fileUploadReqJob);
          break;

        case 'd': /* Download a file to host */
          sscanf(man_msg, "%d %s", &dst, filePath);
          struct Packet *downloadRequestPkt = createEmptyPacket();
          downloadRequestPkt->src = (char)host_id;
          downloadRequestPkt->dst = (char)dst;
          downloadRequestPkt->type = (char)PKT_FILE_DOWNLOAD_REQ;
          downloadRequestPkt->length = strnlen(filePath, MAX_FILENAME_LENGTH);
          strncpy(downloadRequestPkt->payload, filePath, MAX_FILENAME_LENGTH);
          struct Job *downloadRequestJob = createEmptyJob();
          downloadRequestJob->packet = downloadRequestPkt;
          downloadRequestJob->type = JOB_FILE_DOWNLOAD_REQUEST;
          job_enqueue(host_id, &host_q, downloadRequestJob);
          break;

        default:;
      }
      // Release semaphore console_print_access once commands have been executed
      sem_signal(&console_print_access);
    }

    /////// Receive In-Coming packet and translate it to job //////
    for (int portNum = 0; portNum < node_port_array_size; portNum++) {
      struct Packet *received_packet = createEmptyPacket();
      n = packet_recv(node_port_array[portNum], received_packet);

      if ((n > 0) && ((int)received_packet->dst == host_id)) {
#ifdef DEBUG
        colorPrint(YELLOW,
                   "DEBUG: id:%d host_main: Host %d received packet of type "
                   "%s\n",
                   host_id, (int)received_packet->dst,
                   get_packet_type_literal(received_packet->type));
#endif

        struct Job *job_from_pkt = createEmptyJob();
        job_from_pkt->in_port_index = portNum;
        job_from_pkt->packet = received_packet;

        switch (received_packet->type) {
          case (char)PKT_PING_REQ:
            job_from_pkt->type = JOB_PING_REPLY;
            job_enqueue(host_id, &host_q, job_from_pkt);
            break;

          case (char)PKT_PING_RESPONSE:
            ping_reply_received = 1;
            free(received_packet);
            free(job_from_pkt);
            job_from_pkt = NULL;
            break;

          case (char)PKT_FILE_UPLOAD_START:
            job_from_pkt->type = JOB_FILE_RECV_START;
            job_enqueue(host_id, &host_q, job_from_pkt);
            break;

          case (char)PKT_FILE_UPLOAD_END:
            job_from_pkt->type = JOB_FILE_RECV_END;
            job_enqueue(host_id, &host_q, job_from_pkt);
            break;

          case (char)PKT_FILE_DOWNLOAD_REQ:
            // Grab payload from received_packet
            char msg[PACKET_PAYLOAD_MAX] = {0};
            strncpy(msg, received_packet->payload, PACKET_PAYLOAD_MAX);
            // Sanitize msg to ensure it is null terminated
            int endIndex = received_packet->length;
            received_packet->payload[endIndex] = '\0';

            // Check to see if file exists
            char filepath[MAX_FILENAME_LENGTH + PACKET_PAYLOAD_MAX];
            sprintf(filepath, "%s/%s", hostDirectory, received_packet->payload);
            if (!isValidFile(filepath)) {
              // File does not exist
              job_from_pkt->type = JOB_SEND_REQ_RESPONSE;
              job_from_pkt->packet->dst = received_packet->src;
              job_from_pkt->packet->src = host_id;
              job_from_pkt->packet->type = PKT_REQUEST_RESPONSE;
              const char *response = "File does not exist\0";
              job_from_pkt->packet->length = strlen(response);
              strncpy(job_from_pkt->packet->payload, response,
                      strlen(response));
              job_enqueue(host_id, &host_q, job_from_pkt);
            } else {
              // File exists, start file upload
              job_from_pkt->type = JOB_FILE_UPLOAD_SEND;
              job_from_pkt->file_upload_dst = received_packet->src;
              strncpy(job_from_pkt->fname_upload, received_packet->payload,
                      MAX_FILENAME_LENGTH);
              job_from_pkt->fname_upload[strnlen(received_packet->payload,
                                                 MAX_FILENAME_LENGTH)] = '\0';
              job_enqueue(host_id, &host_q, job_from_pkt);
            }
            break;

          case (char)PKT_REQUEST_RESPONSE:
            job_from_pkt->type = JOB_DISPLAY_REQ_RESPONSE;
            job_from_pkt->packet = received_packet;
            job_enqueue(host_id, &host_q, job_from_pkt);
            break;

          default:
            free(received_packet);
            free(job_from_pkt);
            job_from_pkt = NULL;
        }
      } else {
        free(received_packet);
      }
    }

    //////////// FETCH JOB FROM QUEUE ////////////
    if (job_queue_length(&host_q) > 0) {
      /* Get a new job from the job queue */
      struct Job *job_from_queue = job_dequeue(host_id, &host_q);

      //////////// EXECUTE FETCHED JOB ////////////
      switch (job_from_queue->type) {
        case JOB_BROADCAST_PKT:
          for (int portNum = 0; portNum < node_port_array_size; portNum++) {
            packet_send(node_port_array[portNum], job_from_queue->packet);
          }
          free(job_from_queue->packet);
          free(job_from_queue);
          job_from_queue = NULL;
          break;

        case JOB_PING_REPLY:
          /* Create ping reply packet */
          struct Packet *ping_reply_pkt = createEmptyPacket();
          ping_reply_pkt->dst = job_from_queue->packet->src;
          ping_reply_pkt->src = (char)host_id;
          ping_reply_pkt->type = PKT_PING_RESPONSE;
          ping_reply_pkt->length = 0;
          /* Create job for the ping reply */
          struct Job *job_from_queue2 =
              (struct Job *)malloc(sizeof(struct Job));
          job_from_queue2->type = JOB_BROADCAST_PKT;
          job_from_queue2->packet = ping_reply_pkt;
          /* Enter job in the job queue */
          job_enqueue(host_id, &host_q, job_from_queue2);
          /* Free old packet and job memory space */
          free(job_from_queue->packet);
          free(job_from_queue);
          job_from_queue = NULL;
          break;

          // case JOB_PING_WAIT_FOR_REPLY:
          //   if (ping_reply_received == 1) {
          //     n = snprintf(man_reply_msg, MAX_MSG_LENGTH,
          //                  "\x1b[32;1mPing acknowleged!\x1b[0m");
          //     man_reply_msg[n] = '\0';
          //     write(man_port->send_fd, man_reply_msg, n + 1);
          //     free(job_from_queue);
          //     job_from_queue = NULL;
          //   } else if (job_from_queue->timeToLive > 1) {
          //     job_from_queue->timeToLive--;
          //     job_enqueue(host_id, &host_q, job_from_queue);
          //   } else { /* Time out */
          //     n = snprintf(man_reply_msg, MAX_MSG_LENGTH,
          //                  "\x1b[31;1mPing timed out!\x1b[0m");
          //     man_reply_msg[n] = '\0';
          //     write(man_port->send_fd, man_reply_msg, n + 1);
          //     free(job_from_queue);
          //     job_from_queue = NULL;
          //   }
          //   break;

        case JOB_WAIT_FOR_RESPONSE:
          switch (job_from_queue->packet->type) {
            case PKT_PING_REQ:
              if (ping_reply_received == 1) {
                ping_reply_received = 0;
                n = snprintf(man_reply_msg, MAX_MSG_LENGTH,
                             "\x1b[32;1mPing acknowleged!\x1b[0m");
                man_reply_msg[n] = '\0';
                write(man_port->send_fd, man_reply_msg, n + 1);
                free(job_from_queue->packet);
                free(job_from_queue);
                job_from_queue = NULL;
              } else if (job_from_queue->timeToLive > 1) {
                job_from_queue->timeToLive--;
                job_enqueue(host_id, &host_q, job_from_queue);
              } else { /* Time out */
                n = snprintf(man_reply_msg, MAX_MSG_LENGTH,
                             "\x1b[31;1mPing timed out!\x1b[0m");
                man_reply_msg[n] = '\0';
                write(man_port->send_fd, man_reply_msg, n + 1);
                free(job_from_queue->packet);
                free(job_from_queue);
                job_from_queue = NULL;
              }
              break;

            case PKT_FILE_UPLOAD_REQ:
              break;

            case PKT_FILE_DOWNLOAD_REQ:
              break;

            default:
#ifdef DEBUG
              fprintf(stderr,
                      "Host%d, JOB HANDLER: JOB_WAIT_FOR_RESPONSE: "
                      "Unidentified type\n",
                      host_id);
#endif
          }
          break;

        case JOB_FILE_UPLOAD_REQ:
          struct Packet *reqPkt = createEmptyPacket();
          reqPkt->dst = job_from_queue->file_upload_dst;
          reqPkt->src = host_id;
          reqPkt->type = PKT_FILE_UPLOAD_REQ;
          reqPkt->length =
              strnlen(job_from_queue->fname_upload, MAX_FILENAME_LENGTH);
          strncpy(reqPkt->payload, job_from_queue->fname_upload,
                  MAX_FILENAME_LENGTH);
          struct Job *reqJob = createEmptyJob();
          reqJob->type = JOB_BROADCAST_PKT;
          reqJob->packet = reqPkt;
          job_enqueue(host_id, &host_q, reqJob);

          struct Packet *waitPacket = createEmptyPacket();
          waitPacket->dst = job_from_queue->file_upload_dst;
          waitPacket->src = host_id;
          waitPacket->type = PKT_FILE_UPLOAD_REQ;
          struct Job *waitJob = createEmptyJob();
          waitJob->type = JOB_WAIT_FOR_RESPONSE;
          waitJob->timeToLive = TIMETOLIVE;
          waitJob->packet = waitPacket;
          job_enqueue(host_id, &host_q, waitJob);
          ping_reply_received = 0;
          break;

        case JOB_FILE_UPLOAD_SEND:
          char filePath[MAX_FILENAME_LENGTH] = {0};
          int filePathLen =
              snprintf(filePath, MAX_FILENAME_LENGTH, "./%s/%s", hostDirectory,
                       job_from_queue->fname_upload);
          filePath[filePathLen] = '\0';
          fp = fopen(filePath, "r");
          if (fp != NULL) {
            /* Create first packet which has the filePath */
            struct Packet *firstPacket = createEmptyPacket();
            firstPacket->dst = job_from_queue->file_upload_dst;
            firstPacket->src = (char)host_id;
            firstPacket->type = PKT_FILE_UPLOAD_START;
            firstPacket->length = filePathLen;
            /* Create a job to send the packet and put it in the job queue
             */
            struct Job *firstFileJob = createEmptyJob();
            firstFileJob->type = JOB_BROADCAST_PKT;
            firstFileJob->packet = firstPacket;
            strncpy(firstFileJob->fname_upload, filePath, filePathLen);
            job_enqueue(host_id, &host_q, firstFileJob);

            /* Create the second packet which has the file contents */
            struct Packet *secondPacket = createEmptyPacket();
            secondPacket->dst = job_from_queue->file_upload_dst;
            secondPacket->src = (char)host_id;
            secondPacket->type = PKT_FILE_UPLOAD_END;
            int fileLen = fread(string, sizeof(char), PACKET_PAYLOAD_MAX, fp);
            fclose(fp);
            string[fileLen] = '\0';
            for (int i = 0; i < fileLen; i++) {
              secondPacket->payload[i] = string[i];
            }
            secondPacket->length = n;
            /* Create a job to send the packet and enqueue */
            struct Job *secondFileJob = createEmptyJob();
            secondFileJob->type = JOB_BROADCAST_PKT;
            secondFileJob->packet = secondPacket;
            job_enqueue(host_id, &host_q, secondFileJob);

            free(job_from_queue);
            job_from_queue = NULL;
          } else {
            /* Didn't open file */
          }
          break;

        case JOB_FILE_RECV_START:
          /* Initialize the file buffer data structure */
          file_buf_init(&f_buf_upload);

          /* Transfer the filePath in the packet payload to the file
           * buffer */
          file_buf_put_name(&f_buf_upload, job_from_queue->packet->payload,
                            job_from_queue->packet->length);
          free(job_from_queue->packet);
          free(job_from_queue);
          job_from_queue = NULL;
          break;

        case JOB_FILE_RECV_END:
          file_buf_add(&f_buf_upload, job_from_queue->packet->payload,
                       job_from_queue->packet->length);
          free(job_from_queue->packet);
          free(job_from_queue);
          job_from_queue = NULL;
          if (isValidDirectory) {
            /*
             * Get file filePath from the file buffer
             * Then open the file
             */
            file_buf_get_name(&f_buf_upload, string);
            char filePath[MAX_FILENAME_LENGTH] = {0};
            n = snprintf(filePath, MAX_FILENAME_LENGTH, "./%s/%s",
                         hostDirectory, string);
            filePath[n] = '\0';
            fp = fopen(filePath, "w");
            if (fp != NULL) {
              /*
               * Write contents in the file
               * buffer into file
               */
              while (f_buf_upload.occ > 0) {
                n = file_buf_remove(&f_buf_upload, string, PACKET_PAYLOAD_MAX);
                string[n] = '\0';
                n = fwrite(string, sizeof(char), n, fp);
              }
              fclose(fp);
            }
            colorPrint(BOLD_GREEN, "Host%d: File Transfer Done\n", host_id);
          }
          break;

        case JOB_FILE_DOWNLOAD_REQUEST:
          sendPacketTo(node_port_array, node_port_array_size,
                       job_from_queue->packet);
          free(job_from_queue->packet);
          free(job_from_queue);
          job_from_queue = NULL;
          break;

        case JOB_SEND_REQ_RESPONSE:
          sendPacketTo(node_port_array, node_port_array_size,
                       job_from_queue->packet);
          free(job_from_queue->packet);
          free(job_from_queue);
          job_from_queue = NULL;
          break;

        case JOB_DISPLAY_REQ_RESPONSE:
          colorPrint(BOLD_YELLOW, "%s\n", job_from_queue->packet->payload);
          free(job_from_queue->packet);
          free(job_from_queue);
          job_from_queue = NULL;
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
    }

    /* The host goes to sleep for 10 ms */
    usleep(LOOP_SLEEP_TIME_MS);

  } /* End of while loop */
}
