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

// Separate the ticket and data from a packet payload
void parseString(const char *inputStr, char *ticketStr, char *dataStr) {
  // Find the delimiter in the input string
  char *delimPos = strchr(inputStr, ':');
  if (delimPos == NULL) {
    // Handle error - delimiter not found
    ticketStr = NULL;
    strncpy(dataStr, "BAD DATA", 9);
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
}

void handleIncomingResponsePacket(int host_id, struct Packet *recPkt,
                                  struct Job_queue *host_q,
                                  struct Request *reqList) {
  char responseTicket[TICKETLEN];
  parseString(recPkt->payload, responseTicket, NULL);
  struct Request *r = findRequestByStringTicket(reqList, responseTicket);
  if (r != NULL) {
    switch (recPkt->type) {
      case PKT_PING_RESPONSE:
        r->state = COMPLETE;
        break;
      case PKT_UPLOAD_RESPONSE:
        break;
      case PKT_DOWNLOAD_RESPONSE:
        break;
      default:
    }
  }
}

void jobSendResponseHandler(int host_id, struct Job *job_from_queue,
                            struct Job_queue *host_q,
                            char hostDirectory[MAX_FILENAME_LENGTH]) {
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
      parseString(job_from_queue->packet->payload, ticket, fname);

      char responseMsg[MAX_RESPONSE_LEN];

      // Check to see if hostDirectory is valid
      if (!isValidDirectory(hostDirectory)) {
        snprintf(responseMsg, PACKET_PAYLOAD_MAX,
                 "%s:Host%d does not have a valid directory set\n", ticket,
                 host_id);

      } else {
        // Directory is set and valid
        // Create fullpath from hostDirectory and fname
        int fnameLen = strnlen(fname, MAX_FILENAME_LENGTH);
        fname[fnameLen] = '\0';
        char fullPath[2 * MAX_FILENAME_LENGTH];
        snprintf(fullPath, (2 * MAX_FILENAME_LENGTH), "%s/%s", hostDirectory,
                 fname);

        // Check to see if fullPath points to a valid file
        if (fileExists(fullPath)) {
          snprintf(responseMsg, PACKET_PAYLOAD_MAX,
                   "%s:This file already exists\n", ticket);
        } else {
          // directory is set and valid, and file does not already
          // exist
          snprintf(responseMsg, PACKET_PAYLOAD_MAX, "%s:Ready", ticket);
        }
      }
      // struct Packet *responsePkt = createEmptyPacket();
      // memcpy(responsePkt, job_from_queue->packet, sizeof(struct Packet));
      // // Update responsePkt payload and length
      // strncpy(responsePkt->payload, responseMsg, PACKET_PAYLOAD_MAX);
      // int responseMsgLen = strnlen(responseMsg, PACKET_PAYLOAD_MAX);
      // responsePkt->length = responseMsgLen;

      struct Packet *responsePkt = job_from_queue->packet;
      // Update responsePkt payload and length
      strncpy(responsePkt->payload, responseMsg, PACKET_PAYLOAD_MAX);
      int responseMsgLen = strnlen(responseMsg, PACKET_PAYLOAD_MAX);
      responsePkt->length = responseMsgLen;

      // Create and enqueue job
      struct Job *res = createJob(JOB_SEND_PKT, responsePkt);
      job_enqueue(host_id, host_q, res);
      break;
    }  // End of case PKT_UPLOAD_RESPONSE

    case PKT_DOWNLOAD_RESPONSE:
      break;
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
    ////////////////////////////////////////////////////////////////
    ////////////////////////////////
    //////////////// COMMAND HANDLER

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
          // Generate a upload request packet
          // Create a job containing that packet
          // And enqueue the job
          job_enqueue(host_id, &host_q,
                      createJob(JOB_SEND_REQUEST,
                                createPacket(host_id, dst, PKT_UPLOAD_REQ)));
          break;
          ////////////////

        case 'd':
          // Download a file from another host to active host
          // Generate a download request packet
          // Create a job containing that packet
          // And enqueue the job
          job_enqueue(host_id, &host_q,
                      createJob(JOB_SEND_REQUEST,
                                createPacket(host_id, dst, PKT_DOWNLOAD_REQ)));
          break;
          ////////////////

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
                                         requestList);
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

            // Enqueue a JOB_SEND_PKT to send to destination
            struct Job *sendJob = createJob(JOB_SEND_PKT, reqPkt);
            job_enqueue(host_id, &host_q, sendJob);

            // Copy request packet to include it in the JOB_WAITING_FOR_RESPONSE
            struct Packet *waitPkt = createEmptyPacket();
            memcpy(waitPkt, reqPkt, sizeof(struct Packet));

            // Create Job that will wait for request
            struct Job *waitJob = createJob(JOB_WAIT_FOR_RESPONSE, waitPkt);
            // Attach request to the job waiting for response
            waitJob->request = req;

            // Enqueue a JOB_WAIT_FOR_RESPONSE
            job_enqueue(host_id, &host_q, waitJob);
            break;
          }  //////////////// End of JOB_SEND_REQUEST

          case JOB_SEND_RESPONSE: {
            jobSendResponseHandler(host_id, job_from_queue, &host_q,
                                   hostDirectory);
            break;
          }  //////////////// End of case JOB_SEND_RESPONSE

          case JOB_SEND_PKT: {
            sendPacketTo(node_port_array, node_port_array_size,
                         job_from_queue->packet);
            break;
          }  ////////////////

          case JOB_WAIT_FOR_RESPONSE: {
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
          }  //////////////// End of case JOB_WAIT_FOR_RESPONSE

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
