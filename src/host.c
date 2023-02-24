/*
  host.c
*/

#include "host.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "color.h"
#include "job.h"
#include "main.h"
#include "manager.h"
#include "net.h"
#include "packet.h"
#include "semaphore.h"

///////////////////////////////////////////////
////////////// RESPONSE STUFF /////////////////
// An issued request is contingent on a response
// Multiple requests may be issued simultaneously
// Must track all requests that have been made, and their status
struct Response {
  int id;
  int req_type;
  int timeToLive;
  struct Response *next;
};

struct Response *createResponse(int id, int req_type, int ttl) {
  struct Response *r = (struct Response *)malloc(sizeof(struct Response));
  r->id = id;
  r->req_type = req_type;
  r->timeToLive = ttl;
  r->next = NULL;
  return r;
}

// Add a new Response node to the end of the linked list
void addResList(struct Response *list, struct Response *add) {
  // Traverse to the end of the list
  struct Response *last = list;
  while (last->next != NULL) {
    last = last->next;
  }
  // Add the new node to the end of the list
  last->next = add;
  add->next = NULL;
}

// Remove the first Response node from the linked list and return it
struct Response popResList(struct Response *list, struct Response *pop) {
  // Store the first node in a separate pointer
  struct Response *first = list->next;
  if (first == NULL) {
    // The list is empty, return a NULL node
    return (struct Response){0, 0, 0, NULL};
  }
  // Update the head of the list to point to the second node
  list->next = first->next;
  // Clear the next pointer of the popped node
  first->next = NULL;
  // Return the popped node
  return *first;
}

// Find the first Response node in the linked list with a matching id
struct Response *findResList(struct Response *list, int id) {
  // Traverse the list and search for a node with matching src_id
  struct Response *curr = list->next;
  while (curr != NULL) {
    if (curr->id == id) {
      return curr;
    }
    curr = curr->next;
  }

  // No node with matching src_id was found, return NULL
  return NULL;
}

////////////// RESPONSE STUFF /////////////////
///////////////////////////////////////////////

//////////////////////////////
////// FILEBUFFER STUFF //////

/* Initialize file buffer data structure */
void file_buf_init(struct File_buf *f) {
  f->head = 0;
  f->tail = HOST_MAX_FILE_BUFFER;
  f->occ = 0;
  f->name_length = 0;
}

/* Get the file name in the file buffer and store it in name
   Terminate the string in name with the null character
 */
void file_buf_get_name(struct File_buf *f, char name[]) {
  strncpy(name, f->name, f->name_length);
  name[f->name_length] = '\0';
}

void file_buf_put_name(struct File_buf *f, char *name, int length) {
  strncpy(f->name, name, length);
  f->name_length = length;
}

/*
 *  Add 'length' bytes n string[] to the file buffer
 */
int file_buf_add(struct File_buf *f, char string[], int length) {
  int i = 0;

  while (i < length && f->occ < HOST_MAX_FILE_BUFFER) {
    f->tail = (f->tail + 1) % (HOST_MAX_FILE_BUFFER + 1);
    f->buffer[f->tail] = string[i];
    i++;
    f->occ++;
  }
  return (i);
}

/*
 *  Remove bytes from the file buffer and store it in string[]
 *  The number of bytes is length.
 */
int file_buf_remove(struct File_buf *f, char string[], int length) {
  int i = 0;

  while (i < length && f->occ > 0) {
    string[i] = f->buffer[f->head];
    f->head = (f->head + 1) % (HOST_MAX_FILE_BUFFER + 1);
    i++;
    f->occ--;
  }

  return (i);
}

////// FILEBUFFER STUFF //////
//////////////////////////////

/*
This function reads a command from a manager port and removes the first
character from the message. It takes in three parameters: a pointer to a struct
 Man_port_at_host, an array of characters called msg, and a pointer to a
character called c. The function first reads the command from the manager port
using the read() function and stores it in the msg array. It then loops through
the message until it finds a non-space character, which it stores in c. It then
continues looping until it finds another non-space character, which is used to
start copying the rest of the message into msg starting at index 0. Finally, it
adds a null terminator at the end of msg and returns n.
*/
int get_man_command(struct Man_port_at_host *port, char msg[], char *c) {
  int n;
  int i;
  int k;

  n = read(port->recv_fd, msg,
           MAN_MAX_MSG_LENGTH); /* Get command from manager */
  if (n > 0) {                  /* Remove the first char from "msg" */
    for (i = 0; msg[i] == ' ' && i < n; i++)
      ;
    *c = msg[i];
    i++;
    for (; msg[i] == ' ' && i < n; i++)
      ;
    for (k = 0; k + i < n; k++) {
      msg[k] = msg[k + i];
    }
    msg[k] = '\0';
  }
  return n;
}

int isValidDirectory(const char *path) {
  DIR *hostDirectory = opendir(path);
  if (hostDirectory) {
    closedir(hostDirectory);
    return 1;
  } else {
    return 0;
  }
}

int isValidFile(const char *path) {
  if (access(path, R_OK) != -1) {
    // File exists and can be read
    return 1;
  } else {
    // File does not exist or cannot be read
    return 0;
  }
}

/* Send back state of the host to the manager as a text message */
void reply_display_host_state(struct Man_port_at_host *port,
                              char hostDirectory[], int dir_valid,
                              int host_id) {
  int n;
  char reply_msg[HOST_MAX_MSG_LENGTH];

  if (isValidDirectory(hostDirectory)) {
    n = snprintf(reply_msg, HOST_MAX_MSG_LENGTH, "%s %d", hostDirectory,
                 host_id);
  } else {
    n = snprintf(reply_msg, HOST_MAX_MSG_LENGTH, "\033[1;31mNone %d\033[0m",
                 host_id);
  }

  write(port->send_fd, reply_msg, n);
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
}

////////////////////////////////////////////////
////////////////// HOST MAIN ///////////////////
void host_main(int host_id) {
  /* Initialize State */
  char hostDirectory[MAX_FILENAME_LENGTH];

  char man_msg[MAN_MAX_MSG_LENGTH];
  char man_reply_msg[MAN_MAX_MSG_LENGTH];
  char man_cmd;
  struct Man_port_at_host *man_port;  // Port to the manager

  struct Net_port *node_port_list;
  struct Net_port **node_port_array;  // Array of pointers to node ports
  int node_port_array_size;           // Number of node ports

  int ping_reply_received;

  // Flag for communicating upload is due to a download request
  int downloadRequestFlag = 0;

  char string[PKT_PAYLOAD_MAX + 1];

  FILE *fp;

  struct Net_port *p;

  struct Job_queue host_q;

  struct File_buf f_buf_upload;
  struct File_buf f_buf_download;

  file_buf_init(&f_buf_upload);
  file_buf_init(&f_buf_download);

  /* Initialize pipes, Get link port to the manager */
  man_port = net_get_host_port(host_id);

  /*
   * Create an array node_port_array[ ] to store the network link ports
   * at the host.  The number of ports is node_port_array_size
   */
  node_port_list = net_get_port_list(host_id);

  /*  Count the number of network link ports */
  node_port_array_size = 0;
  for (p = node_port_list; p != NULL; p = p->next) {
    node_port_array_size++;
  }
  /* Create memory space for the array */
  node_port_array = (struct Net_port **)malloc(node_port_array_size *
                                               sizeof(struct Net_port *));

  /* Load ports into the array */
  p = node_port_list;
  for (int k = 0; k < node_port_array_size; k++) {
    node_port_array[k] = p;
    p = p->next;
  }

  /* Initialize the job queue */
  job_queue_init(&host_q);

  /* Initialize response list */
  struct Response *responseList = NULL;
  int responseIdNum = 0;

  //////////// WORKING LOOP ////////////
  while (1) {
    /* Execute command from manager, if any */

    //////////// COMMAND HANDLER ////////////
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
          // Send ping request
          int dst;
          sscanf(man_msg, "%d", &dst);
          struct Packet *pingReqPkt = createBlankPacket();
          pingReqPkt->src = (char)host_id;
          pingReqPkt->dst = (char)dst;
          pingReqPkt->type = (char)PKT_PING_REQ;
          pingReqPkt->length = 0;
          struct Job *pingReqJob = createBlankJob();
          pingReqJob->packet = pingReqPkt;
          pingReqJob->type = JOB_BROADCAST_PKT;
          job_enqueue(host_id, &host_q, pingReqJob);

          struct Packet *waitPacket = createBlankPacket();
          waitPacket->dst = (char)dst;
          waitPacket->src = (char)host_id;
          waitPacket->type = PKT_PING_REQ;
          struct Job *waitJob = createBlankJob();
          waitJob->type = JOB_WAIT_FOR_RESPONSE;
          waitJob->timeToLive = TIMETOLIVE;
          waitJob->packet = waitPacket;
          job_enqueue(host_id, &host_q, waitJob);
          ping_reply_received = 0;

          struct Response *r =
              createResponse(responseIdNum++, PKT_PING_REQ, 10);
          addResList(responseList, r);
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
          //   // Concatenate hostDirectory and filePath with a slash in between
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
          //   struct Job *fileUploadJob = createBlankJob();
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
          struct Job *fileUploadReqJob = createBlankJob();
          fileUploadReqJob->type = JOB_FILE_UPLOAD_REQ;
          fileUploadReqJob->file_upload_dst = dst;
          strncpy(fileUploadReqJob->fname_upload, filePath, filePathLen);
          fileUploadReqJob->fname_upload[filePathLen] = '\0';
          job_enqueue(host_id, &host_q, fileUploadReqJob);
          break;

        case 'd': /* Download a file to host */
          sscanf(man_msg, "%d %s", &dst, filePath);
          struct Packet *downloadRequestPkt = createBlankPacket();
          downloadRequestPkt->src = (char)host_id;
          downloadRequestPkt->dst = (char)dst;
          downloadRequestPkt->type = (char)PKT_FILE_DOWNLOAD_REQ;
          downloadRequestPkt->length = strnlen(filePath, MAX_FILENAME_LENGTH);
          strncpy(downloadRequestPkt->payload, filePath, MAX_FILENAME_LENGTH);
          struct Job *downloadRequestJob = createBlankJob();
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
    for (int k = 0; k < node_port_array_size; k++) {
      struct Packet *received_packet = createBlankPacket();
      n = packet_recv(node_port_array[k], received_packet);

      if ((n > 0) && ((int)received_packet->dst == host_id)) {
#ifdef DEBUG
        colorPrint(YELLOW,
                   "DEBUG: id:%d host_main: Host %d received packet of type "
                   "%s\n",
                   host_id, (int)received_packet->dst,
                   get_packet_type_literal(received_packet->type));
#endif

        struct Job *new_job = createBlankJob();
        new_job->in_port_index = k;
        new_job->packet = received_packet;

        switch (received_packet->type) {
          case (char)PKT_PING_REQ:
            new_job->type = JOB_PING_REPLY;
            job_enqueue(host_id, &host_q, new_job);
            break;

          case (char)PKT_PING_RESPONSE:
            ping_reply_received = 1;
            free(received_packet);
            free(new_job);
            new_job = NULL;
            break;

          case (char)PKT_FILE_UPLOAD_START:
            new_job->type = JOB_FILE_RECV_START;
            job_enqueue(host_id, &host_q, new_job);
            break;

          case (char)PKT_FILE_UPLOAD_END:
            new_job->type = JOB_FILE_RECV_END;
            job_enqueue(host_id, &host_q, new_job);
            break;

          case (char)PKT_FILE_DOWNLOAD_REQ:
            // Grab payload from received_packet
            char msg[PKT_PAYLOAD_MAX] = {0};
            strncpy(msg, received_packet->payload, PKT_PAYLOAD_MAX);
            // Sanitize msg to ensure it is null terminated
            int endIndex = received_packet->length;
            received_packet->payload[endIndex] = '\0';

            // Check to see if file exists
            char filepath[MAX_FILENAME_LENGTH + PKT_PAYLOAD_MAX];
            sprintf(filepath, "%s/%s", hostDirectory, received_packet->payload);
            if (!isValidFile(filepath)) {
              // File does not exist
              new_job->type = JOB_SEND_REQ_RESPONSE;
              new_job->packet->dst = received_packet->src;
              new_job->packet->src = host_id;
              new_job->packet->type = PKT_REQUEST_RESPONSE;
              const char *response = "File does not exist\0";
              new_job->packet->length = strlen(response);
              strncpy(new_job->packet->payload, response, strlen(response));
              job_enqueue(host_id, &host_q, new_job);
            } else {
              // File exists, start file upload
              new_job->type = JOB_FILE_UPLOAD_SEND;
              new_job->file_upload_dst = received_packet->src;
              strncpy(new_job->fname_upload, received_packet->payload,
                      MAX_FILENAME_LENGTH);
              new_job->fname_upload[strnlen(received_packet->payload,
                                            MAX_FILENAME_LENGTH)] = '\0';
              job_enqueue(host_id, &host_q, new_job);
            }
            break;

          case (char)PKT_REQUEST_RESPONSE:
            new_job->type = JOB_DISPLAY_REQ_RESPONSE;
            new_job->packet = received_packet;
            job_enqueue(host_id, &host_q, new_job);
            break;

          default:
            free(received_packet);
            free(new_job);
            new_job = NULL;
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
          for (int k = 0; k < node_port_array_size; k++) {
            packet_send(node_port_array[k], job_from_queue->packet);
          }
          free(job_from_queue->packet);
          free(job_from_queue);
          job_from_queue = NULL;
          break;

        case JOB_PING_REPLY:
          /* Create ping reply packet */
          struct Packet *ping_reply_pkt = createBlankPacket();
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
          //     n = snprintf(man_reply_msg, MAN_MAX_MSG_LENGTH,
          //                  "\x1b[32;1mPing acknowleged!\x1b[0m");
          //     man_reply_msg[n] = '\0';
          //     write(man_port->send_fd, man_reply_msg, n + 1);
          //     free(job_from_queue);
          //     job_from_queue = NULL;
          //   } else if (job_from_queue->timeToLive > 1) {
          //     job_from_queue->timeToLive--;
          //     job_enqueue(host_id, &host_q, job_from_queue);
          //   } else { /* Time out */
          //     n = snprintf(man_reply_msg, MAN_MAX_MSG_LENGTH,
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
                n = snprintf(man_reply_msg, MAN_MAX_MSG_LENGTH,
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
                n = snprintf(man_reply_msg, MAN_MAX_MSG_LENGTH,
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
          struct Packet *reqPkt = createBlankPacket();
          reqPkt->dst = job_from_queue->file_upload_dst;
          reqPkt->src = host_id;
          reqPkt->type = PKT_FILE_UPLOAD_REQ;
          reqPkt->length =
              strnlen(job_from_queue->fname_upload, MAX_FILENAME_LENGTH);
          strncpy(reqPkt->payload, job_from_queue->fname_upload,
                  MAX_FILENAME_LENGTH);
          struct Job *reqJob = createBlankJob();
          reqJob->type = JOB_BROADCAST_PKT;
          reqJob->packet = reqPkt;
          job_enqueue(host_id, &host_q, reqJob);

          struct Packet *waitPacket = createBlankPacket();
          waitPacket->dst = job_from_queue->file_upload_dst;
          waitPacket->src = host_id;
          waitPacket->type = PKT_FILE_UPLOAD_REQ;
          struct Job *waitJob = createBlankJob();
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
            struct Packet *firstPacket = createBlankPacket();
            firstPacket->dst = job_from_queue->file_upload_dst;
            firstPacket->src = (char)host_id;
            firstPacket->type = PKT_FILE_UPLOAD_START;
            firstPacket->length = filePathLen;
            /* Create a job to send the packet and put it in the job queue
             */
            struct Job *firstFileJob = createBlankJob();
            firstFileJob->type = JOB_BROADCAST_PKT;
            firstFileJob->packet = firstPacket;
            strncpy(firstFileJob->fname_upload, filePath, filePathLen);
            job_enqueue(host_id, &host_q, firstFileJob);

            /* Create the second packet which has the file contents */
            struct Packet *secondPacket = createBlankPacket();
            secondPacket->dst = job_from_queue->file_upload_dst;
            secondPacket->src = (char)host_id;
            secondPacket->type = PKT_FILE_UPLOAD_END;
            int fileLen = fread(string, sizeof(char), PKT_PAYLOAD_MAX, fp);
            fclose(fp);
            string[fileLen] = '\0';
            for (int i = 0; i < fileLen; i++) {
              secondPacket->payload[i] = string[i];
            }
            secondPacket->length = n;
            /* Create a job to send the packet and enqueue */
            struct Job *secondFileJob = createBlankJob();
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
                n = file_buf_remove(&f_buf_upload, string, PKT_PAYLOAD_MAX);
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
    usleep(TENMILLISEC);

  } /* End of while loop */
}
