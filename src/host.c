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
  int i;

  for (i = 0; i < f->name_length; i++) {
    name[i] = f->name[i];
  }
  name[f->name_length] = '\0';
}

/*
 *  Put name[] into the file name in the file buffer
 *  length = the length of name[]
 */
void file_buf_put_name(struct File_buf *f, char name[], int length) {
  int i;

  for (i = 0; i < length; i++) {
    f->name[i] = name[i];
  }
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
 man_port_at_host, an array of characters called msg, and a pointer to a
character called c. The function first reads the command from the manager port
using the read() function and stores it in the msg array. It then loops through
the message until it finds a non-space character, which it stores in c. It then
continues looping until it finds another non-space character, which is used to
start copying the rest of the message into msg starting at index 0. Finally, it
adds a null terminator at the end of msg and returns n.
*/
int get_man_command(struct man_port_at_host *port, char msg[], char *c) {
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

int is_valid_directory(const char *path) {
  struct stat sb;
  if (stat(path, &sb) != 0 || !S_ISDIR(sb.st_mode)) {
    return 0;
  }
  return 1;
}

/* Send back state of the host to the manager as a text message */
void reply_display_host_state(struct man_port_at_host *port, char dir[],
                              int dir_valid, int host_id) {
  int n;
  char reply_msg[HOST_MAX_MSG_LENGTH];

  if (dir_valid == 1) {
    n = snprintf(reply_msg, HOST_MAX_MSG_LENGTH, "%s %d", dir, host_id);
  } else {
    n = snprintf(reply_msg, HOST_MAX_MSG_LENGTH, "\033[1;31mNone %d\033[0m",
                 host_id);
  }

  write(port->send_fd, reply_msg, n);
}

int sendPacketTo(struct net_port **arr, int arrSize, struct Packet *p) {
  // Find which net_port entry in net_port_array has desired destination
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
  char dir[MAX_DIR_NAME_LENGTH];
  int dir_valid = 0;

  char man_msg[MAN_MAX_MSG_LENGTH];
  char man_reply_msg[MAN_MAX_MSG_LENGTH];
  char man_cmd;
  struct man_port_at_host *man_port;  // Port to the manager

  struct net_port *node_port_list;
  struct net_port **node_port_array;  // Array of pointers to node ports
  int node_port_array_size;           // Number of node ports

  int ping_reply_received;

  int i, k, n;
  int dst;
  char name[HOST_MAX_FILE_NAME_LENGTH];
  char string[PKT_PAYLOAD_MAX + 1];

  FILE *fp;

  struct net_port *p;

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
  node_port_array = (struct net_port **)malloc(node_port_array_size *
                                               sizeof(struct net_port *));

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
          reply_display_host_state(man_port, dir, dir_valid, host_id);
          break;

        case 'm':
          size_t len = strnlen(man_msg, MAX_DIR_NAME_LENGTH - 1);
          if (is_valid_directory(man_msg)) {
            memcpy(dir, man_msg, len);
            dir[len] = '\0';  // add null character
            dir_valid = 1;
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
          new_packet->type = (char)PKT_PING_REQ;
          new_packet->length = 0;
          struct Job *sendPingJob = createBlankJob();
          sendPingJob->packet = new_packet;
          sendPingJob->type = SEND_PKT_ALL_PORTS;
          job_enqueue(host_id, &host_q, sendPingJob);

          struct Job *waitForPingResponseJob = createBlankJob();
          ping_reply_received = 0;
          waitForPingResponseJob->type = PING_WAIT_FOR_REPLY;
          waitForPingResponseJob->timeToLive = 10;
          job_enqueue(host_id, &host_q, waitForPingResponseJob);
          break;

        case 'u': /* Upload a file to a host */
          sscanf(man_msg, "%d %s", &dst, name);
          struct Job *fileUploadJob = (struct Job *)malloc(sizeof(struct Job));
          fileUploadJob->type = FILE_UPLOAD_SEND;
          fileUploadJob->file_upload_dst = dst;
          for (i = 0; name[i] != '\0'; i++) {
            fileUploadJob->fname_upload[i] = name[i];
          }
          fileUploadJob->fname_upload[i] = '\0';
          job_enqueue(host_id, &host_q, fileUploadJob);
          break;

        case 'd': /* Download a file to host */
          sscanf(man_msg, "%d %s", &dst, name);
          struct Packet *downloadRequestPkt = createBlankPacket();
          downloadRequestPkt->src = (char)host_id;
          downloadRequestPkt->dst = (char)dst;
          downloadRequestPkt->type = (char)PKT_FILE_DOWNLOAD_REQUEST;
          downloadRequestPkt->length = strnlen(name, MAX_DIR_NAME_LENGTH);
          strncpy(downloadRequestPkt->payload, name, MAX_DIR_NAME_LENGTH);
          struct Job *downloadRequestJob = createBlankJob();
          downloadRequestJob->packet = downloadRequestPkt;
          downloadRequestJob->type = FILE_DOWNLOAD_REQUEST;
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
          case (char)PKT_PING_REQ:
            new_job->type = PING_SEND_REPLY;
            job_enqueue(host_id, &host_q, new_job);
            break;

          case (char)PKT_PING_REPLY:
            ping_reply_received = 1;
            free(in_packet);
            free(new_job);
            new_job = NULL;
            break;

          case (char)PKT_FILE_UPLOAD_START:
            new_job->type = FILE_UPLOAD_RECV_START;
            job_enqueue(host_id, &host_q, new_job);
            break;

          case (char)PKT_FILE_UPLOAD_END:
            new_job->type = FILE_UPLOAD_RECV_END;
            job_enqueue(host_id, &host_q, new_job);
            break;

          case (char)PKT_FILE_DOWNLOAD_REQUEST:
            // Grab payload from in_packet
            char msg[PKT_PAYLOAD_MAX] = {0};
            strncpy(msg, in_packet->payload, PKT_PAYLOAD_MAX);
            // Sanitize msg to ensure it's null terminated
            int endIndex = in_packet->length;
            in_packet->payload[endIndex] = '\0';
            // Check to see if file exists
            char filepath[MAX_DIR_NAME_LENGTH + PKT_PAYLOAD_MAX];
            sprintf(filepath, "%s/%s", dir, in_packet->payload);
            FILE *file = fopen(filepath, "r");
            if (file == NULL) {
              // File does not exist
              new_job->type = SEND_REQUEST_RESPONSE;
              new_job->packet->dst = in_packet->src;
              new_job->packet->src = host_id;
              new_job->packet->type = PKT_REQUEST_RESPONSE;
              const char *response = "File does not exist\0";
              new_job->packet->length = strlen(response);
              strncpy(new_job->packet->payload, response, strlen(response));
              job_enqueue(host_id, &host_q, new_job);
            } else {
              // File exists, start file upload
              new_job->type = FILE_UPLOAD_SEND;
              new_job->file_upload_dst = in_packet->src;
              strncpy(new_job->fname_upload, in_packet->payload,
                      MAX_DIR_NAME_LENGTH);
              new_job->fname_upload[strnlen(in_packet->payload,
                                            MAX_DIR_NAME_LENGTH)] = '\0';
              job_enqueue(host_id, &host_q, new_job);
            }
            break;

          case (char)PKT_REQUEST_RESPONSE:
            new_job->type = DISPLAY_REQUEST_RESPONSE;
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
        case SEND_PKT_ALL_PORTS:
          for (k = 0; k < node_port_array_size; k++) {
            packet_send(node_port_array[k], new_job->packet);
          }
          free(new_job->packet);
          free(new_job);
          new_job = NULL;
          break;

        case PING_SEND_REPLY:
          /* Create ping reply packet */
          struct Packet *ping_reply_pkt = createBlankPacket();
          ping_reply_pkt->dst = new_job->packet->src;
          ping_reply_pkt->src = (char)host_id;
          ping_reply_pkt->type = PKT_PING_REPLY;
          ping_reply_pkt->length = 0;

          /* Create job for the ping reply */
          struct Job *new_job2 = (struct Job *)malloc(sizeof(struct Job));
          new_job2->type = SEND_PKT_ALL_PORTS;
          new_job2->packet = ping_reply_pkt;

          /* Enter job in the job queue */
          job_enqueue(host_id, &host_q, new_job2);

          /* Free old packet and job memory space */
          free(new_job->packet);
          free(new_job);
          new_job = NULL;
          break;

        case PING_WAIT_FOR_REPLY:
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

          /* The next three jobs deal with uploading a file */
          /* This job is for the sending host */
        case FILE_UPLOAD_SEND:
          /* Open file */
          if (dir_valid == 1) {
            n = snprintf(name, HOST_MAX_FILE_NAME_LENGTH, "./%s/%s", dir,
                         new_job->fname_upload);
            name[n] = '\0';
            fp = fopen(name, "r");
            if (fp != NULL) {
              /*
               * Create first packet which
               * has the file name
               */
              struct Packet *new_packet =
                  (struct Packet *)malloc(sizeof(struct Packet));
              new_packet->dst = new_job->file_upload_dst;
              new_packet->src = (char)host_id;
              new_packet->type = PKT_FILE_UPLOAD_START;
              for (i = 0; new_job->fname_upload[i] != '\0'; i++) {
                new_packet->payload[i] = new_job->fname_upload[i];
              }
              new_packet->length = i;
              /*
               * Create a job to send the packet
               * and put it in the job queue
               */
              new_job2 = (struct Job *)malloc(sizeof(struct Job));
              new_job2->type = SEND_PKT_ALL_PORTS;
              new_job2->packet = new_packet;
              job_enqueue(host_id, &host_q, new_job2);
              /*
               * Create the second packet which
               * has the file contents
               */
              new_packet = (struct Packet *)malloc(sizeof(struct Packet));
              new_packet->dst = new_job->file_upload_dst;
              new_packet->src = (char)host_id;
              new_packet->type = PKT_FILE_UPLOAD_END;
              n = fread(string, sizeof(char), PKT_PAYLOAD_MAX, fp);
              fclose(fp);
              string[n] = '\0';
              for (i = 0; i < n; i++) {
                new_packet->payload[i] = string[i];
              }
              new_packet->length = n;
              /*
               * Create a job to send the packet
               * and put the job in the job queue
               */

              new_job2 = (struct Job *)malloc(sizeof(struct Job));
              new_job2->type = SEND_PKT_ALL_PORTS;
              new_job2->packet = new_packet;
              job_enqueue(host_id, &host_q, new_job2);
              free(new_job);
              new_job = NULL;
            } else {
              /* Didn't open file */
            }
          }
          break;

          /* The next two jobs are for the receving host */
        case FILE_UPLOAD_RECV_START:
          /* Initialize the file buffer data structure */
          file_buf_init(&f_buf_upload);
          /*
           * Transfer the file name in the packet payload
           * to the file buffer data structure
           */

          file_buf_put_name(&f_buf_upload, new_job->packet->payload,
                            new_job->packet->length);
          free(new_job->packet);
          free(new_job);
          new_job = NULL;
          break;

        case FILE_UPLOAD_RECV_END:
          /*
           * Download packet payload into file buffer
           * data structure
           */
          file_buf_add(&f_buf_upload, new_job->packet->payload,
                       new_job->packet->length);
          free(new_job->packet);
          free(new_job);
          new_job = NULL;
          if (dir_valid == 1) {
            /*
             * Get file name from the file buffer
             * Then open the file
             */
            file_buf_get_name(&f_buf_upload, string);
            n = snprintf(name, HOST_MAX_FILE_NAME_LENGTH, "./%s/%s", dir,
                         string);
            name[n] = '\0';
            fp = fopen(name, "w");
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

        case FILE_DOWNLOAD_REQUEST:
          sendPacketTo(node_port_array, node_port_array_size, new_job->packet);
          free(new_job->packet);
          free(new_job);
          new_job = NULL;
          break;

        case SEND_REQUEST_RESPONSE:
          sendPacketTo(node_port_array, node_port_array_size, new_job->packet);
          free(new_job->packet);
          free(new_job);
          new_job = NULL;
          break;

        case DISPLAY_REQUEST_RESPONSE:
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
