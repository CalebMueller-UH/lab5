/*
  host.c
*/

#include "host.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "color.h"
#include "constants.h"
#include "debug.h"
#include "job.h"
#include "manager.h"
#include "nameServer.h"
#include "net.h"
#include "packet.h"
#include "switch.h"

struct HostContext {
  int _id;
  char *linkedDirPath;
  struct Man_port_at_host *man_port;
  char man_msg[MAX_MSG_LENGTH];
  struct JobQueue **jobq;
  struct Net_port *node_port_list;
  struct Net_port **node_port_array;
  int node_port_array_size;
  char **nametable;
  int isRequestingDownload;
};

// Forward Declarations of host.c specific functions:
void commandDownloadHandler(struct HostContext *host, int dst,
                            char fname[MAX_FILENAME_LENGTH]);
void commandHandler(struct HostContext *host);
void commandUploadHandler(struct HostContext *host, int dst, char *fname);
struct HostContext *initHostContext(int host_id);
void jobSendDownloadResponseHandler(struct HostContext *host,
                                    struct Job *job_from_queue);
void jobSendResponseHandler(struct HostContext *host,
                            struct Job *job_from_queue);
void jobSendRequestHandler(struct HostContext *host,
                           struct Job *job_from_queue);
void jobSendUploadResponseHandler(struct HostContext *host,
                                  struct Job *job_from_queue);
void jobUploadSendHandler(struct HostContext *host, struct Job *job_from_queue);
void jobWaitForResponseHandler(struct HostContext *host, struct Job *job);
int parseManMsg(char *msg, char *cmd, char *dstStr, char *fname);
void parsePacket(const char *inputStr, char *ticketStr, char *dataStr);
void pktIncomingRequest(struct HostContext *host, struct Packet *inPkt);
void pktIncomingResponse(struct HostContext *host, struct Packet *inPkt);
void pktUploadEnd(struct HostContext *host, struct Packet *pkt);
void pktUploadReceive(struct HostContext *host, struct Packet *pkt);
void sendMsgToManager(int fd, char msg[MAX_MSG_LENGTH]);
int sendPacketTo(struct Net_port **node_port_array, int node_port_array_size,
                 struct Packet *p);
int resolveHostname(struct HostContext *host, char *name);
int requestIDFromDNS(struct HostContext *host, char *nameToResolve);
int updateNametable(struct HostContext *host, int hostId,
                    char name[MAX_NAME_LEN]);

////////////////////////////////////////////////
////////////////// HOST MAIN ///////////////////
void host_main(int host_id) {
  ////// Initialize state of host //////
  struct HostContext *host = initHostContext(host_id);

  unsigned int numCtrlMsgsSent = 0;
  static long long timeLast = 0;

  while (1) {
    // Periodically broadcast STP Control Packets
    long long timeNow = current_time_ms();
    if (timeNow - timeLast > PERIODIC_CTRL_MSG_WAITTIME_MS &&
        numCtrlMsgsSent < ALLOWED_CONVERGENCE_ROUNDS) {
      numCtrlMsgsSent++;
      controlPacketSender_endpoint(host->_id, host->node_port_array,
                                   host->node_port_array_size);
      timeLast = timeNow;
    }
    ////////////////////////////////////////////////////////////////
    ////////////////////////////////
    //////////////// COMMAND HANDLER

    // Attempt to retrieve issued command from manager
    int n = get_man_msg(host->man_port, host->man_msg);
    if (n > 0) {
      // Received a man_msg...
      commandHandler(host);
    }

    //////////////// COMMAND HANDLER
    ////////////////////////////////
    ////////////////////////////////////////////////////////////////

    // -------------------------------------------------------------

    ////////////////////////////////////////////////////////////////
    ////////////////////////////////
    //////////////// PACKET HANDLER

    for (int portNum = 0; portNum < host->node_port_array_size; portNum++) {
      // Receive packets for all ports in node_port_array
      struct Packet *inPkt = createEmptyPacket();
      n = packet_recv(host->node_port_array[portNum], inPkt);
      // if portNum has received a packet, translate the packet into a job
      if ((n > 0) && ((int)inPkt->dst == host->_id)) {
        if (inPkt->type != PKT_CONTROL) {
#ifdef HOST_DEBUG_PACKET_RECEIPT
          colorPrint(MAGENTA, "Host%d received packet: ", host->_id);
          printPacket(inPkt);
#endif
        }

        switch (inPkt->type) {
          case PKT_CONTROL:
            break;

          case PKT_PING_REQ:
          case PKT_UPLOAD_REQ:
            pktIncomingRequest(host, inPkt);
            break;

          case PKT_DOWNLOAD_REQ: {
            char *id = (char *)malloc(sizeof(char) * JIDLEN);
            char *fname = (char *)malloc(sizeof(char) * MAX_RESPONSE_LEN);
            parsePacket(inPkt->payload, id, fname);
            commandUploadHandler(host, inPkt->src, fname);
            free(id);
            free(fname);
            break;
          }

          case PKT_PING_RESPONSE:
          case PKT_UPLOAD_RESPONSE:
          case PKT_DOWNLOAD_RESPONSE:
          case PKT_DNS_REGISTRATION_RESPONSE:
          case PKT_DNS_QUERY_RESPONSE:
            pktIncomingResponse(host, inPkt);
            break;

          case PKT_UPLOAD:
            pktUploadReceive(host, inPkt);
            break;

          case PKT_UPLOAD_END:
            pktUploadEnd(host, inPkt);
            break;

          default:
            fprintf(stderr, "Host%d received a packet of unknown type\n",
                    host->_id);
            free(inPkt);
            break;
        }  // end of switch
      } else {
        // No packet addressed to host received
        packet_delete(inPkt);
      }

      //////////////// PACKET HANDLER
      ////////////////////////////////
      ////////////////////////////////////////////////////////////////
      // -------------------------------------------------------------
      ////////////////////////////////////////////////////////////////
      ////////////////////////////////
      //////////////// JOB HANDLER

      if (job_queue_length(*host->jobq) > 0) {
        /* Get a new job from the job queue */
        struct Job *job_from_queue = job_dequeue(host->_id, *host->jobq);

        //////////////////// EXECUTE FETCHED JOB ////////////////////
        switch (job_from_queue->type) {
          ////////////////
          case JOB_SEND_REQUEST: {
            jobSendRequestHandler(host, job_from_queue);
            break;
          }  //////////////// End of JOB_SEND_REQUEST

          case JOB_SEND_RESPONSE: {
            jobSendResponseHandler(host, job_from_queue);
            break;
          }  //////////////// End of case JOB_SEND_RESPONSE

          case JOB_SEND_PKT: {
            sendPacketTo(host->node_port_array, host->node_port_array_size,
                         job_from_queue->packet);
            job_delete(host->_id, job_from_queue);
            break;
          }  //////////////// End of case JOB_SEND_PKT

          case JOB_WAIT_FOR_RESPONSE: {
            jobWaitForResponseHandler(host, job_from_queue);
            break;
          }  //////////////// End of case JOB_WAIT_FOR_RESPONSE

          case JOB_UPLOAD: {
            jobUploadSendHandler(host, job_from_queue);
            break;
          }  //////////////// End of case JOB_UPLOAD

          case JOB_DOWNLOAD: {
            break;
          }  //////////////// End of case JOB_DOWNLOAD

          default:
#ifdef HOST_DEBUG
            colorPrint(YELLOW,
                       "Host%d's job_handler encountered a job with %s\n",
                       host->_id, get_job_type_literal(job_from_queue->type));
#endif
        }  // End of switch (job_from_queue->type)
      }    // End of if (job_queue_length(&host->jobq) > 0)

      /////////////////// JOB HANDLER
      ///////////////////////////////////
      ///////////////////////////////////////////////////////////////////////

      /* The host goes to sleep for 10 ms */
      usleep(LOOP_SLEEP_TIME_US);
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

void commandDownloadHandler(struct HostContext *host, int dst,
                            char fname[MAX_FILENAME_LENGTH]) {
  char *responseMsg = malloc(sizeof(char) * MAX_MSG_LENGTH);
  memset(responseMsg, 0, MAX_MSG_LENGTH);

  // Error conditions checking
  if (!isValidDirectory(host->linkedDirPath)) {
    // Check to see that local file directory is set and valid
    colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED,
                  "Host %d does not have a valid directory set", host->_id);
  } else if (dst == host->_id) {
    // Can't download to self
    colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED,
                  "Cannot download to self");
  } else {
    // File already exists in local file directory
    char fullPath[2 * MAX_FILENAME_LENGTH] = {0};
    snprintf(fullPath, sizeof(fullPath), "%s/%s", host->linkedDirPath, fname);

    if (fileExists(fullPath)) {
      colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED,
                    "This file already exists in %s", host->linkedDirPath);
    } else {
      // Directory is set, and file does not already exist
      host->isRequestingDownload = 1;
      struct Packet *reqPkt =
          createPacket(host->_id, dst, PKT_DOWNLOAD_REQ, 0, NULL);
      reqPkt->length =
          (char)snprintf(reqPkt->payload, PACKET_PAYLOAD_MAX, "0000:%s", fname);
      sendPacketTo(host->node_port_array, host->node_port_array_size, reqPkt);
      packet_delete(reqPkt);
    }
  }

  sendMsgToManager(host->man_port->send_fd, responseMsg);
  free(responseMsg);
}  // End of commandDownloadHandler()

void commandHandler(struct HostContext *host) {
  char cmd;
  char dstStr[PACKET_PAYLOAD_MAX];
  char fname[MAX_FILENAME_LENGTH];

  if (!parseManMsg(host->man_msg, &cmd, dstStr, fname)) {
    fprintf(stderr, "Failed to parse man_msg\n");
  }

#ifdef HOST_MANMSG_DEBUG
  colorPrint(GREY, "man_msg: %s\n", host->man_msg);
  colorPrint(GREY, "cmd: %c\tdstStr: %s\tfname:%s\n", cmd, dstStr, fname);
#endif

  int needsDst = 0;
  switch (cmd) {
    case 's':
    case 'm':
    case 'a':
      break;
    default:
      needsDst = 1;
  }

  int dst;
  if (needsDst == 1) {
    dst = resolveHostname(host, dstStr);
    if (dst < 0) {
      // Unable to resolve hostname in local cache
      // Send a DNS Query to the server to retrieve the id associated with
      // that domain name
      requestIDFromDNS(host, dstStr);
      return;
    }
  }

  // Host Name WAS able to be resolved
  char *responseMsg = (char *)malloc(sizeof(char) * MAX_MSG_LENGTH);

  switch (cmd) {
    case 's': {
      // Display host state
      reply_display_host_state(host->man_port, host->linkedDirPath,
                               isValidDirectory(host->linkedDirPath),
                               host->_id);
      break;
    }  //////////////// End of case 's'

    case 'm': {
      // Change Active Host's local file directory
      size_t len = strnlen(dstStr, MAX_FILENAME_LENGTH - 1);
      if (isValidDirectory(dstStr)) {
        memcpy(host->linkedDirPath, dstStr, len);
        host->linkedDirPath[len] = '\0';  // add null character
        colorPrint(BOLD_GREEN, "Host%d's main directory set to %s\n", host->_id,
                   host->man_msg);
      } else {
        colorPrint(BOLD_RED, "%s is not a valid directory\n", host->man_msg);
      }
      break;
    }  //////////////// End of case 'm'

    case 'p': {
      ////// Have active Host ping another host //////
      // Check to see if pinging self
      if (dst == host->_id) {
        memset(responseMsg, '\0', MAX_MSG_LENGTH);
        colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_GREEN,
                      "Pinging Self...");
        sendMsgToManager(host->man_port->send_fd, responseMsg);
        break;
      }

      // Create ping request packet
      struct Packet *preqPkt =
          createPacket(host->_id, dst, PKT_PING_REQ, 0, NULL);

      // Create send request job
      struct Job *sendReqJob = job_create(NULL, TIMETOLIVE, JOB_SEND_REQUEST,
                                          JOB_PENDING_STATE, preqPkt);
      // Enqueue job
      job_enqueue(host->_id, *host->jobq, sendReqJob);
      break;
    }  //////////////// End of case 'p'

    case 'u': {
      // Upload a file from active host to another host
      commandUploadHandler(host, dst, fname);
      break;
    }  //////////////// End of case 'u'

    case 'd': {
      // Download a file from another host to active host
      commandDownloadHandler(host, dst, fname);

      break;
    }  //////////////// End of case 'd'

    case 'a': {
      // Register a domain name to the active host through manager interface
      char dnsName[MAX_NAME_LEN];
      strncpy(dnsName, dstStr, MAX_NAME_LEN);

      int dnsNameLen = strnlen(dnsName, MAX_FILENAME_LENGTH);
      dnsName[dnsNameLen] = '\0';

      // register own domain name in local cache
      memcpy(host->nametable[host->_id], dnsName, PACKET_PAYLOAD_MAX);

      // Create a registration packet to send to DNS Server
      struct Packet *registerPkt = createPacket(
          host->_id, STATIC_DNS_ID, PKT_DNS_REGISTRATION, dnsNameLen, dnsName);
      // Create send DNS Register request job
      struct Job *sendRegReqJob = job_create(NULL, TIMETOLIVE, JOB_SEND_REQUEST,
                                             JOB_PENDING_STATE, registerPkt);
      // Enqueue job
      job_enqueue(host->_id, *host->jobq, sendRegReqJob);
      break;
    }  //////////////// End of case 'a'

    default:;
  }
  // if (responseMsg) {
  //   free(responseMsg);
  // }

}  // End of commandHandler()

void commandUploadHandler(struct HostContext *host, int dst, char *fname) {
  char *responseMsg = malloc(sizeof(char) * MAX_MSG_LENGTH);
  memset(responseMsg, 0, MAX_MSG_LENGTH);

  if (!isValidDirectory(host->linkedDirPath)) {
    colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED,
                  "Host %d does not have a valid directory set", host->_id);
  } else if (dst == host->_id) {
    colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED,
                  "Cannot upload to self");
  } else {
    char fullPath[2 * MAX_FILENAME_LENGTH] = {0};
    snprintf(fullPath, sizeof(fullPath), "%s/%s", host->linkedDirPath, fname);

    if (!fileExists(fullPath)) {
      colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED,
                    "This file does not exist in %s", host->linkedDirPath);
    } else {
      // Directory is set, and file exists

      // Open file to read from
      FILE *fp = fopen(fullPath, "rb");

      // Create an upload request packet
      struct Packet *upReqPkt =
          createPacket(host->_id, dst, PKT_UPLOAD_REQ, 0, fname);
      // Create a send request job
      struct Job *sendReqJob = job_create(NULL, TIMETOLIVE, JOB_SEND_REQUEST,
                                          JOB_PENDING_STATE, upReqPkt);
      sendReqJob->fp = fp;
      strncpy(sendReqJob->filepath, fullPath, sizeof(sendReqJob->filepath));
      // Enque job
      job_enqueue(host->_id, *host->jobq, sendReqJob);
    }
  }

  sendMsgToManager(host->man_port->send_fd, responseMsg);
  free(responseMsg);
}  // End of commandUploadHandler()

struct HostContext *initHostContext(int host_id) {
  struct HostContext *host_context =
      (struct HostContext *)malloc(sizeof(struct HostContext));

  host_context->_id = host_id;

  host_context->linkedDirPath =
      (char *)malloc(sizeof(char) * MAX_FILENAME_LENGTH);

  host_context->man_port = net_get_host_port(host_id);

  host_context->node_port_list = net_get_port_list(host_id);

  host_context->node_port_array_size = 0;
  for (struct Net_port *p = host_context->node_port_list; p != NULL;
       p = p->next) {
    host_context->node_port_array_size++;
  }

  host_context->node_port_array = (struct Net_port **)malloc(
      host_context->node_port_array_size * sizeof(struct Net_port *));
  {
    struct Net_port *p = host_context->node_port_list;
    for (int portNum = 0; portNum < host_context->node_port_array_size;
         portNum++) {
      host_context->node_port_array[portNum] = p;
      p = p->next;
    }
  }

  // Allocate memory for the JobQueue struct
  host_context->jobq = (struct JobQueue **)malloc(sizeof(struct JobQueue *));
  *host_context->jobq = (struct JobQueue *)malloc(sizeof(struct JobQueue));

  // Initialize the JobQueue struct
  job_queue_init(*host_context->jobq);

  host_context->nametable = malloc((MAX_NUM_NAMES + 1) * sizeof(char *));
  init_nametable(host_context->nametable);

  host_context->isRequestingDownload = 0;

  return host_context;
}  // End of initHostContext()

void jobSendDownloadResponseHandler(struct HostContext *host,
                                    struct Job *job_from_queue) {
  struct Packet *qPkt = job_from_queue->packet;

  char *id = (char *)malloc(sizeof(char) * JIDLEN);
  char *fname = (char *)malloc(sizeof(char) * MAX_RESPONSE_LEN);
  parsePacket(qPkt->payload, id, fname);

  char fullPath[2 * MAX_FILENAME_LENGTH] = {0};

  char *payloadMsg = malloc(sizeof(char) * MAX_MSG_LENGTH);
  memset(payloadMsg, 0, MAX_MSG_LENGTH);

  int readyFlag = 0;

  // Error Checking
  if (!isValidDirectory(host->linkedDirPath)) {
    snprintf(payloadMsg, MAX_MSG_LENGTH,
             "%s:Host %d does not have a valid directory set", id, host->_id);
  } else {
    // Valid local file directory is set
    snprintf(fullPath, sizeof(fullPath), "%s/%s", host->linkedDirPath, fname);

    if (!fileExists(fullPath)) {
      snprintf(payloadMsg, MAX_MSG_LENGTH, "%s:This file does not exist in %s",
               id, host->linkedDirPath);
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

  sendPacketTo(host->node_port_array, host->node_port_array_size, qPkt);

  if (readyFlag) {
    jobUploadSendHandler(host, job_from_queue);
  }

  // Clean up memory
  free(id);
  free(fname);
  free(payloadMsg);
}  // End of jobSendDownloadResponseHandler()

void jobSendResponseHandler(struct HostContext *host,
                            struct Job *job_from_queue) {
  // Send response according to packet type contained in passed-in job
  switch (job_from_queue->packet->type) {
    case PKT_PING_RESPONSE:
      sendPacketTo(host->node_port_array, host->node_port_array_size,
                   job_from_queue->packet);
      break;

    case PKT_UPLOAD_RESPONSE:
      jobSendUploadResponseHandler(host, job_from_queue);
      break;

    case PKT_DOWNLOAD_RESPONSE: {
      jobSendDownloadResponseHandler(host, job_from_queue);
      break;
    }
  }
}  // End of jobSendResponseHandler()

void jobSendRequestHandler(struct HostContext *host,
                           struct Job *job_from_queue) {
  if (!job_from_queue || !host->jobq) {
    fprintf(stderr,
            "ERROR: host%d jobSendRequestHandler invoked with invalid function "
            "parameter: %s %s\n",
            host->_id, !job_from_queue ? "job_from_queue " : "",
            !host->jobq ? "host->jobq " : "");
    return;
  }
  sendPacketTo(host->node_port_array, host->node_port_array_size,
               job_from_queue->packet);

  job_from_queue->type = JOB_WAIT_FOR_RESPONSE;
  job_enqueue(host->_id, *host->jobq, job_from_queue);

}  // End of jobSendRequestHandler

void jobSendUploadResponseHandler(struct HostContext *host,
                                  struct Job *job_from_queue) {
  struct Packet *qPkt = job_from_queue->packet;

  char *id = (char *)malloc(sizeof(char) * JIDLEN);
  char *fname = (char *)malloc(sizeof(char) * MAX_RESPONSE_LEN);
  parsePacket(qPkt->payload, id, fname);

  char fullPath[2 * MAX_FILENAME_LENGTH] = {0};

  char *payloadMsg = malloc(sizeof(char) * MAX_MSG_LENGTH);
  memset(payloadMsg, 0, MAX_MSG_LENGTH);

  if (!isValidDirectory(host->linkedDirPath)) {
    snprintf(payloadMsg, MAX_MSG_LENGTH,
             "%s:Host %d does not have a valid directory set", id, host->_id);
  } else {
    snprintf(fullPath, sizeof(fullPath), "%s/%s", host->linkedDirPath, fname);

    if (fileExists(fullPath)) {
      snprintf(payloadMsg, MAX_MSG_LENGTH, "%s:This file already exists in %s!",
               id, host->linkedDirPath);
    } else {
      // Directory is set, and file exists
      snprintf(payloadMsg, PACKET_PAYLOAD_MAX, "%s:Ready", id);

      FILE *fp = fopen(fullPath, "w");
      job_from_queue->fp = fp;
      strncpy(job_from_queue->filepath, fullPath,
              strnlen(fullPath, MAX_FILENAME_LENGTH * 2));
      job_from_queue->type = JOB_WAIT_FOR_RESPONSE;
      job_enqueue(host->_id, *host->jobq, job_from_queue);
    }
  }
  int payloadMsgLen = strnlen(payloadMsg, PACKET_PAYLOAD_MAX);
  memset(qPkt->payload, 0, PACKET_PAYLOAD_MAX);
  memcpy(qPkt->payload, payloadMsg, payloadMsgLen);
  qPkt->length = payloadMsgLen;

  sendPacketTo(host->node_port_array, host->node_port_array_size, qPkt);

  // Clean up memory
  free(id);
  free(fname);
  free(payloadMsg);
}  // End of jobSendUploadResponseHandler()

void jobUploadSendHandler(struct HostContext *host,
                          struct Job *job_from_queue) {
  char *id = (char *)malloc(sizeof(char) * JIDLEN);
  char *msg = (char *)malloc(sizeof(char) * MAX_RESPONSE_LEN);
  parsePacket(job_from_queue->packet->payload, id, msg);
  int dst = job_from_queue->packet->dst;
  int src = host->_id;

  FILE *fp = job_from_queue->fp;

  // Get the file size
  fseek(fp, 0L, SEEK_END);
  long fileSize = ftell(fp);

  // Set the file position to the current offset
  fseek(fp, job_from_queue->fileOffset, SEEK_SET);

  // Allocate a buffer for reading data from the file
  int bufferSize = MAX_RESPONSE_LEN - 1;
  char *buffer = (char *)malloc(sizeof(char) * bufferSize);

  // Read and send one chunk of the file
  int chunkSize;
  if (fileSize - job_from_queue->fileOffset < bufferSize) {
    chunkSize = fileSize - job_from_queue->fileOffset;
  } else {
    chunkSize = bufferSize;
  }

  // Clear the buffer before reading new data
  memset(buffer, 0, bufferSize * sizeof(char));

  int bytesRead = fread(buffer, sizeof(char), chunkSize, fp);

  if (bytesRead >= 0) {
    struct Packet *p = createPacket(src, dst, PKT_UPLOAD, 0, buffer);
    struct Job *j = job_create(id, 0, JOB_SEND_PKT, JOB_COMPLETE_STATE, p);
    job_enqueue(host->_id, *host->jobq, j);

    // Update the file offset
    job_from_queue->fileOffset += bytesRead;

    // If there's still data left to send
    if (job_from_queue->fileOffset >= fileSize) {
      // Notify the receiver that the file transfer is complete
      struct Packet *finPkt = createPacket(src, dst, PKT_UPLOAD_END, 0, NULL);
      struct Job *finJob =
          job_create(id, TIMETOLIVE, JOB_SEND_PKT, JOB_COMPLETE_STATE, finPkt);
      job_enqueue(host->_id, *host->jobq, finJob);
      job_from_queue->state = JOB_COMPLETE_STATE;
    }
  }

  // Free allocated memory
  free(id);
  free(msg);
  free(buffer);
}  // End of jobUploadSendHandler()

void jobWaitForResponseHandler(struct HostContext *host,
                               struct Job *job_from_queue) {
  char *responseMsg = malloc(sizeof(char) * MAX_MSG_LENGTH);
  memset(responseMsg, 0, MAX_MSG_LENGTH);

  if (job_from_queue->timeToLive <= 0) {  // Handle expired job
    // Generate Expiration Notice for expired jobs
    switch (job_from_queue->packet->type) {
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
      case PKT_DNS_REGISTRATION:
        colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED,
                      "DNS Registration Timed Out!");
        break;
      case PKT_DNS_QUERY:
        colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED,
                      "DNS Query Timed Out!");
        break;
    }

    // Send expiration notice to waiting manager
    sendMsgToManager(host->man_port->send_fd, responseMsg);

    // Discard expired job and packet
    job_delete(host->_id, job_from_queue);

  } else {  // Handle pending job
    // Decrement time to live every time it's processed
    job_from_queue->timeToLive--;

    if (job_from_queue->state == JOB_PENDING_STATE) {
      // Re-enqueue job while pending and ttl > 0
      job_enqueue(host->_id, *host->jobq, job_from_queue);

    } else {
      // Handle non-expired non-pending jobs according to their packet type
      switch (job_from_queue->packet->type) {
        case PKT_PING_REQ:
          if (job_from_queue->state == JOB_COMPLETE_STATE) {
            colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_GREEN,
                          "Ping request acknowledged!");
            sendMsgToManager(host->man_port->send_fd, responseMsg);
            job_delete(host->_id, job_from_queue);
          }
          break;

        case PKT_UPLOAD_REQ:
          if (job_from_queue->state == JOB_READY_STATE) {
            // Send file upload packets
            jobUploadSendHandler(host, job_from_queue);
            job_enqueue(host->_id, *host->jobq, job_from_queue);
          } else if (job_from_queue->state == JOB_ERROR_STATE) {
            colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED, "%s",
                          job_from_queue->errorMsg);
            sendMsgToManager(host->man_port->send_fd, responseMsg);
            job_delete(host->_id, job_from_queue);
          } else if (job_from_queue->state == JOB_COMPLETE_STATE) {
            fclose(job_from_queue->fp);
            job_delete(host->_id, job_from_queue);
            colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_GREEN,
                          "Upload Complete");
            sendMsgToManager(host->man_port->send_fd, responseMsg);
          }
          break;

        case PKT_DOWNLOAD_REQ:
          if (job_from_queue->state == JOB_READY_STATE) {
            job_enqueue(host->_id, *host->jobq, job_from_queue);
          } else if (job_from_queue->state == JOB_ERROR_STATE) {
            colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED, "%s",
                          job_from_queue->errorMsg);
            sendMsgToManager(host->man_port->send_fd, responseMsg);
            job_delete(host->_id, job_from_queue);
          } else if (job_from_queue->state == JOB_COMPLETE_STATE) {
            colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_GREEN,
                          "Download Complete");
            sendMsgToManager(host->man_port->send_fd, responseMsg);
            job_delete(host->_id, job_from_queue);
          }
          break;

        case PKT_DNS_REGISTRATION:
          if (job_from_queue->state == JOB_READY_STATE) {
            colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_GREEN,
                          "Domain name registered!");
            sendMsgToManager(host->man_port->send_fd, responseMsg);
          } else if (job_from_queue->state == JOB_ERROR_STATE) {
            colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED,
                          "Domain name could not be registered...");
            sendMsgToManager(host->man_port->send_fd, responseMsg);
          }
          job_delete(host->_id, job_from_queue);
          break;

        case PKT_DNS_QUERY:
          if (job_from_queue->state == JOB_READY_STATE) {
            // Get domain name from original packet
            char *id = (char *)malloc(sizeof(char) * (JIDLEN + 1));
            char *dname = (char *)malloc(sizeof(char) * MAX_NAME_LEN);
            parsePacket(job_from_queue->packet->payload, id, dname);
            // get resolved hostId from query response
            int resolvedHostId = atoi(job_from_queue->errorMsg);
            // Update local cache with host id belonging to domain name
            updateNametable(host, resolvedHostId, dname);
            // Rerun command handler with domain name in local cache
            commandHandler(host);
          } else if (job_from_queue->state == JOB_ERROR_STATE) {
            colorSnprintf(responseMsg, MAX_MSG_LENGTH, BOLD_RED,
                          "Domain name could not be resolved...");
            sendMsgToManager(host->man_port->send_fd, responseMsg);
          }
          job_delete(host->_id, job_from_queue);
          break;
      }
    }
  }

  free(responseMsg);
}  // End of jobWaitForResponseHandler()

int parseManMsg(char *msg, char *cmd, char *dstStr, char *fname) {
  *fname = '\0';  // Initialize fname to the empty string

  int result = sscanf(msg, "%c %255s %255s", cmd, dstStr, fname);
  if (result >= 1) {
    return 1;  // Return 1 to indicate success
  } else {
    return 0;  // Return 0 to indicate failure
  }
}  // End of parseManMsg()

/* parsePacket:
 * parse a packet payload inputStr into its ticket and data
 * Note:: Use dynamically allocated buffers for ticket and data */
void parsePacket(const char *inputStr, char *ticketStr, char *dataStr) {
  const char delim = ':';

  int inputLen = strnlen(inputStr, PACKET_PAYLOAD_MAX);
  if (inputLen == 0) {
    fprintf(stderr, "ERROR: parsePacket was passed an empty inputStr\n");
    return;
  }
  if (inputLen > PACKET_PAYLOAD_MAX) {
    fprintf(stderr,
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
    fprintf(stderr, "ERROR: parsePacket: delimiter not found in inputStr\n");
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

void pktIncomingRequest(struct HostContext *host, struct Packet *inPkt) {
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

  // Readdress incoming packet to use as outgoing packet
  inPkt->type = response_type;
  inPkt->dst = inPkt->src;
  inPkt->src = host->_id;

  // Grab jid from request packet payload
  char *id = (char *)malloc(sizeof(char) * JIDLEN);
  char *msg = (char *)malloc(sizeof(char) * MAX_RESPONSE_LEN);
  parsePacket(inPkt->payload, id, msg);

  struct Job *sendRespJob =
      job_create(id, TIMETOLIVE, JOB_SEND_RESPONSE, JOB_PENDING_STATE, inPkt);
  job_enqueue(host->_id, *host->jobq, sendRespJob);

  free(id);
  free(msg);
}  // End of pktIncomingRequest()

void pktIncomingResponse(struct HostContext *host, struct Packet *inPkt) {
  // Grab jid from request packet payload
  char *id = (char *)malloc(sizeof(char) * JIDLEN);
  char *msg = (char *)malloc(sizeof(char) * MAX_RESPONSE_LEN);
  parsePacket(inPkt->payload, id, msg);

  // Look for job id in job queue
  struct Job *waitJob = job_queue_find_id(*host->jobq, id);
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
          FILE *fp = fopen(waitJob->filepath, "w");
          waitJob->fp = fp;
          waitJob->state = JOB_READY_STATE;
        } else {
          strncpy(waitJob->errorMsg, msg, strnlen(msg, MAX_RESPONSE_LEN));
          waitJob->state = JOB_ERROR_STATE;
        }
        break;

      case PKT_DNS_REGISTRATION_RESPONSE:
        if (strncmp(msg, "OK", sizeof("OK")) == 0) {
          waitJob->state = JOB_READY_STATE;
        } else {
          strncpy(waitJob->errorMsg, msg, strnlen(msg, MAX_RESPONSE_LEN));
          waitJob->state = JOB_ERROR_STATE;
        }
        break;

      case PKT_DNS_QUERY_RESPONSE:
        int msgVal = atoi(msg);
        if (msgVal < 0) {
          waitJob->state = JOB_ERROR_STATE;
        } else {
          waitJob->state = JOB_READY_STATE;
          strncpy(waitJob->errorMsg, msg, strnlen(msg, MAX_RESPONSE_LEN));
        }
        break;
    }
  } else {
    // job id was not found in queue
    colorPrint(GREY, "Host%d received a response with an unrecognized job id\n",
               inPkt->dst);
  }

  // Clean up packet and dynamic vars
  free(id);
  free(msg);
  packet_delete(inPkt);
}  // End of pktIncomingResponse()

void pktUploadEnd(struct HostContext *host, struct Packet *pkt) {
  char *id = (char *)malloc(sizeof(char) * JIDLEN);
  char *msg = (char *)malloc(sizeof(char) * MAX_RESPONSE_LEN);
  parsePacket(pkt->payload, id, msg);

  struct Job *r = job_queue_find_id(*host->jobq, id);
  if (r != NULL) {
    r->state = JOB_COMPLETE_STATE;
  } else {
    fprintf(stderr, "Request not found for ticket %s\n", id);
  }

  if (host->isRequestingDownload) {
    char doneMsg[MAX_MSG_LENGTH];
    colorSnprintf(doneMsg, MAX_MSG_LENGTH, BOLD_GREEN, "Download Complete.\n");
    sendMsgToManager(host->man_port->send_fd, doneMsg);
    host->isRequestingDownload = 0;
  }

  free(id);
  free(msg);
}  // End of pktUploadEnd()

void pktUploadReceive(struct HostContext *host, struct Packet *pkt) {
  char *id = (char *)malloc(sizeof(char) * JIDLEN);
  char *msg = (char *)malloc(sizeof(char) * MAX_RESPONSE_LEN);
  parsePacket(pkt->payload, id, msg);

  struct Job *rjob = job_queue_find_id(*host->jobq, id);
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
    fprintf(stderr, "Request not found for ticket %s\n", id);
  }

  free(id);
  free(msg);
  packet_delete(pkt);
}  // End of pktUploadReceive()

// Send a message back to manager
void sendMsgToManager(int fd, char msg[MAX_MSG_LENGTH]) {
  int msgLen = strnlen(msg, MAX_MSG_LENGTH);
  write(fd, msg, msgLen);
}  // End of sendMsgToManager()

int sendPacketTo(struct Net_port **node_port_array, int node_port_array_size,
                 struct Packet *p) {
  // Find which Net_port entry in net_port_array has desired destination
  int destIndex = -1;
  for (int i = 0; i < node_port_array_size; i++) {
    if (node_port_array[i]->link_node_id == p->dst) {
      destIndex = i;
    }
  }
  // If node_port_array had a valid destination set, send to that node
  if (destIndex >= 0) {
    packet_send(node_port_array[destIndex], p);
  } else {
    // Else, broadcast packet to all connected hosts
    for (int i = 0; i < node_port_array_size; i++) {
      packet_send(node_port_array[i], p);
    }
  }
}  // End of sendPacketTo()

/*
Attempts to convert the 'name' argument to an integer and returns the integer
value if successful. If 'name' cannot be converted, searches the local
nametable array for a matching entry and returns its index. Returns -1 if no
match found.
*/
int resolveHostname(struct HostContext *host, char *name) {
  // Try to convert name to integer
  int value = atoi(name);

  // Check if the conversion was successful
  if (value != 0 || name[0] == '0') {
    return value;
  }

  // Search nametable for matching entry
  for (int i = 0; i < MAX_NUM_NAMES; i++) {
    if (host->nametable[i] != NULL && strcmp(name, host->nametable[i]) == 0) {
      return i;
    }
  }

  // No match found
  return -1;
}  // End of resolveHostname()

int requestIDFromDNS(struct HostContext *host, char *nameToResolve) {
  int nameLen = strlen(nameToResolve);

  // Create DNS Query Packet
  struct Packet *p = createPacket(host->_id, STATIC_DNS_ID, PKT_DNS_QUERY,
                                  nameLen, nameToResolve);
  // Create DNS Query Job
  struct Job *j =
      job_create(NULL, TIMETOLIVE, JOB_SEND_PKT, JOB_PENDING_STATE, p);
  job_enqueue(host->_id, *host->jobq, j);

  // Create a deep copy of DNS Query Packet to keep as reference
  struct Packet *p2 = deepcopy_packet(p);
  // Create a job for waiting for response
  struct Job *j2 = job_create(j->jid, TIMETOLIVE, JOB_WAIT_FOR_RESPONSE,
                              JOB_PENDING_STATE, p2);
  job_enqueue(host->_id, *host->jobq, j2);
  return 0;
}  // End of requestIDFromDNS()

int updateNametable(struct HostContext *host, int hostId,
                    char name[MAX_NAME_LEN]) {
  // Update the nametable
  strcpy(host->nametable[hostId], name);

#ifdef HOST_DEBUG
  colorPrint(GREY, "\tlocal cache nametable for host%d updated: [%d]::\"%s\"\n",
             host->_id, hostId, name);
#endif

  return 0;
}  // End of updateNametable()