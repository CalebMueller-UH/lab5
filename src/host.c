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

  int i, k, n;
  int dst;
  char string[PKT_PAYLOAD_MAX + 1];

  FILE *fp;

  struct Net_port *p;

  struct Job_queue host_q;

  struct File_buf f_buf_upload;
  struct File_buf f_buf_download;

  file_buf_init(&f_buf_upload);
  file_buf_init(&f_buf_download);

  /*
   * Initialize pipes
   * Get link port to the manager
   */
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
  for (k = 0; k < node_port_array_size; k++) {
    node_port_array[k] = p;
    p = p->next;
  }

  /* Initialize the job queue */
  job_queue_init(&host_q);

  while (1) {
    /* Execute command from manager, if any */
    /* Get command from manager */
    n = get_man_command(man_port, man_msg, &man_cmd);

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
          sscanf(man_msg, "%d", &dst);
          struct Packet *new_packet = createBlankPacket();
          new_packet->src = (char)host_id;
          new_packet->dst = (char)dst;
          new_packet->type = (char)PING_REQ_PKT;
          new_packet->length = 0;
          struct Job *sendPingJob = createBlankJob();
          sendPingJob->packet = new_packet;
          sendPingJob->type = BROADCAST_PKT_JOB;
          job_enqueue(host_id, &host_q, sendPingJob);

          struct Job *waitForPingResponseJob = createBlankJob();
          ping_reply_received = 0;
          waitForPingResponseJob->type = PING_WAIT_FOR_REPLY_JOB;
          waitForPingResponseJob->timeToLive = 10;
          job_enqueue(host_id, &host_q, waitForPingResponseJob);
          break;

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
          char fullPath[MAX_FILENAME_LENGTH] = {0};
          snprintf(fullPath, (MAX_FILENAME_LENGTH * 2), "%s/%s", hostDirectory,
                   filePath);
          // Check to see if fullPath points to a valid file
          if (!isValidFile(fullPath)) {
            colorPrint(BOLD_RED, "This file does not exist\n", host_id);
            break;
          }
          // User input is valid, enqueue File upload job
          struct Job *fileUploadJob = createBlankJob();
          fileUploadJob->type = FILE_SEND_JOB;
          fileUploadJob->file_upload_dst = dst;
          strncpy(fileUploadJob->fname_upload, filePath, filePathLen);
          fileUploadJob->fname_upload[filePathLen] = '\0';
          job_enqueue(host_id, &host_q, fileUploadJob);
          break;

        case 'd': /* Download a file to host */
          sscanf(man_msg, "%d %s", &dst, filePath);
          struct Packet *downloadRequestPkt = createBlankPacket();
          downloadRequestPkt->src = (char)host_id;
          downloadRequestPkt->dst = (char)dst;
          downloadRequestPkt->type = (char)FILE_DOWNLOAD_REQUEST_PKT;
          downloadRequestPkt->length = strnlen(filePath, MAX_FILENAME_LENGTH);
          strncpy(downloadRequestPkt->payload, filePath, MAX_FILENAME_LENGTH);
          struct Job *downloadRequestJob = createBlankJob();
          downloadRequestJob->packet = downloadRequestPkt;
          downloadRequestJob->type = FILE_DOWNLOAD_REQUEST_JOB;
          job_enqueue(host_id, &host_q, downloadRequestJob);
          break;

        default:;
      }
      // Release semaphore console_print_access once commands have been executed
      sem_signal(&console_print_access);
    }

    /////// Receive In-Coming packet and translate it to job //////
    for (k = 0; k < node_port_array_size; k++) {
      struct Packet *in_packet = createBlankPacket();
      n = packet_recv(node_port_array[k], in_packet);

      if ((n > 0) && ((int)in_packet->dst == host_id)) {
#ifdef DEBUG
        colorPrint(YELLOW,
                   "DEBUG: id:%d host_main: Host %d received packet of type "
                   "%s\n",
                   host_id, (int)in_packet->dst,
                   get_packet_type_literal(in_packet->type));
#endif

        struct Job *new_job = createBlankJob();
        new_job->in_port_index = k;
        new_job->packet = in_packet;

        switch (in_packet->type) {
          case (char)PING_REQ_PKT:
            new_job->type = PING_REPLY_JOB;
            job_enqueue(host_id, &host_q, new_job);
            break;

          case (char)PING_REPLY_PKT:
            ping_reply_received = 1;
            free(in_packet);
            free(new_job);
            new_job = NULL;
            break;

          case (char)FILE_UPLOAD_START_PKT:
            new_job->type = FILE_RECV_START_JOB;
            job_enqueue(host_id, &host_q, new_job);
            break;

          case (char)FILE_UPLOAD_END_PKT:
            new_job->type = FILE_RECV_END_JOB;
            job_enqueue(host_id, &host_q, new_job);
            break;

          case (char)FILE_DOWNLOAD_REQUEST_PKT:
            // Grab payload from in_packet
            char msg[PKT_PAYLOAD_MAX] = {0};
            strncpy(msg, in_packet->payload, PKT_PAYLOAD_MAX);
            // Sanitize msg to ensure it is null terminated
            int endIndex = in_packet->length;
            in_packet->payload[endIndex] = '\0';

            // Check to see if file exists
            char filepath[MAX_FILENAME_LENGTH + PKT_PAYLOAD_MAX];
            sprintf(filepath, "%s/%s", hostDirectory, in_packet->payload);
            if (!isValidFile(filepath)) {
              // File does not exist
              new_job->type = SEND_REQ_RESPONSE_JOB;
              new_job->packet->dst = in_packet->src;
              new_job->packet->src = host_id;
              new_job->packet->type = REQUEST_RESPONSE_PKT;
              const char *response = "File does not exist\0";
              new_job->packet->length = strlen(response);
              strncpy(new_job->packet->payload, response, strlen(response));
              job_enqueue(host_id, &host_q, new_job);
            } else {
              // File exists, start file upload
              new_job->type = FILE_SEND_JOB;
              new_job->file_upload_dst = in_packet->src;
              strncpy(new_job->fname_upload, in_packet->payload,
                      MAX_FILENAME_LENGTH);
              new_job->fname_upload[strnlen(in_packet->payload,
                                            MAX_FILENAME_LENGTH)] = '\0';
              job_enqueue(host_id, &host_q, new_job);
            }
            break;

          case (char)REQUEST_RESPONSE_PKT:
            new_job->type = DISPLAY_REQ_RESPONSE_JOB;
            new_job->packet = in_packet;
            job_enqueue(host_id, &host_q, new_job);
            break;

          default:
            free(in_packet);
            free(new_job);
            new_job = NULL;
        }
      } else {
        free(in_packet);
      }
    }

    //////////// FETCH JOB FROM QUEUE ////////////
    if (job_queue_length(&host_q) > 0) {
      /* Get a new job from the job queue */
      struct Job *new_job = job_dequeue(host_id, &host_q);

      //////////// EXECUTE FETCHED JOB ////////////
      switch (new_job->type) {
        case BROADCAST_PKT_JOB:
          for (k = 0; k < node_port_array_size; k++) {
            packet_send(node_port_array[k], new_job->packet);
          }
          free(new_job->packet);
          free(new_job);
          new_job = NULL;
          break;

        case PING_REPLY_JOB:
          /* Create ping reply packet */
          struct Packet *ping_reply_pkt = createBlankPacket();
          ping_reply_pkt->dst = new_job->packet->src;
          ping_reply_pkt->src = (char)host_id;
          ping_reply_pkt->type = PING_REPLY_PKT;
          ping_reply_pkt->length = 0;

          /* Create job for the ping reply */
          struct Job *new_job2 = (struct Job *)malloc(sizeof(struct Job));
          new_job2->type = BROADCAST_PKT_JOB;
          new_job2->packet = ping_reply_pkt;

          /* Enter job in the job queue */
          job_enqueue(host_id, &host_q, new_job2);

          /* Free old packet and job memory space */
          free(new_job->packet);
          free(new_job);
          new_job = NULL;
          break;

        case PING_WAIT_FOR_REPLY_JOB:
          if (ping_reply_received == 1) {
            n = snprintf(man_reply_msg, MAN_MAX_MSG_LENGTH,
                         "\x1b[32;1mPing acknowleged!\x1b[0m");
            man_reply_msg[n] = '\0';
            write(man_port->send_fd, man_reply_msg, n + 1);
            free(new_job);
            new_job = NULL;
          } else if (new_job->timeToLive > 1) {
            new_job->timeToLive--;
            job_enqueue(host_id, &host_q, new_job);
          } else { /* Time out */
            n = snprintf(man_reply_msg, MAN_MAX_MSG_LENGTH,
                         "\x1b[31;1mPing timed out!\x1b[0m");
            man_reply_msg[n] = '\0';
            write(man_port->send_fd, man_reply_msg, n + 1);
            free(new_job);
            new_job = NULL;
          }
          break;

        case FILE_SEND_JOB:
          char filePath[MAX_FILENAME_LENGTH] = {0};
          int filePathLen = snprintf(filePath, MAX_FILENAME_LENGTH, "./%s/%s",
                                     hostDirectory, new_job->fname_upload);
          filePath[filePathLen] = '\0';
          fp = fopen(filePath, "r");
          if (fp != NULL) {
            /* Create first packet which has the filePath */
            struct Packet *firstPacket = createBlankPacket();
            firstPacket->dst = new_job->file_upload_dst;
            firstPacket->src = (char)host_id;
            firstPacket->type = FILE_UPLOAD_START_PKT;
            firstPacket->length = filePathLen;
            /* Create a job to send the packet and put it in the job queue */
            struct Job *firstFileJob = createBlankJob();
            firstFileJob->type = BROADCAST_PKT_JOB;
            firstFileJob->packet = firstPacket;
            strncpy(firstFileJob->fname_upload, filePath, filePathLen);
            job_enqueue(host_id, &host_q, firstFileJob);

            /* Create the second packet which has the file contents */
            struct Packet *secondPacket = createBlankPacket();
            secondPacket->dst = new_job->file_upload_dst;
            secondPacket->src = (char)host_id;
            secondPacket->type = FILE_UPLOAD_END_PKT;
            int fileLen = fread(string, sizeof(char), PKT_PAYLOAD_MAX, fp);
            fclose(fp);
            string[fileLen] = '\0';
            for (i = 0; i < fileLen; i++) {
              secondPacket->payload[i] = string[i];
            }
            secondPacket->length = n;
            /* Create a job to send the packet and enqueue */
            struct Job *secondFileJob = createBlankJob();
            secondFileJob->type = BROADCAST_PKT_JOB;
            secondFileJob->packet = secondPacket;
            job_enqueue(host_id, &host_q, secondFileJob);

            free(new_job);
            new_job = NULL;
          } else {
            /* Didn't open file */
          }

          break;

        case FILE_RECV_START_JOB:
          /* Initialize the file buffer data structure */
          file_buf_init(&f_buf_upload);

          /* Transfer the filePath in the packet payload to the file buffer */
          file_buf_put_name(&f_buf_upload, new_job->packet->payload,
                            new_job->packet->length);
          free(new_job->packet);
          free(new_job);
          new_job = NULL;
          break;

        case FILE_RECV_END_JOB:
          file_buf_add(&f_buf_upload, new_job->packet->payload,
                       new_job->packet->length);
          free(new_job->packet);
          free(new_job);
          new_job = NULL;
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

        case FILE_DOWNLOAD_REQUEST_JOB:
          sendPacketTo(node_port_array, node_port_array_size, new_job->packet);
          free(new_job->packet);
          free(new_job);
          new_job = NULL;
          break;

        case SEND_REQ_RESPONSE_JOB:
          sendPacketTo(node_port_array, node_port_array_size, new_job->packet);
          free(new_job->packet);
          free(new_job);
          new_job = NULL;
          break;

        case DISPLAY_REQ_RESPONSE_JOB:
          colorPrint(BOLD_YELLOW, "%s\n", new_job->packet->payload);
          free(new_job->packet);
          free(new_job);
          new_job = NULL;
          break;

        default:
#ifdef DEBUG
          colorPrint(
              YELLOW,
              "DEBUG: id:%d host_main: job_handler defaulted with job type: "
              "%s\n",
              host_id, get_job_type_literal(new_job->type));
#endif
      }
    }

    /* The host goes to sleep for 10 ms */
    usleep(TENMILLISEC);

  } /* End of while loop */
}
