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

// Add a new Response node to the beginning of the linked list
void addToResList(struct Response *list, struct Response *add) {
  add->next = list;
  list = add;
}

// Remove and free response in list with id matching idToDelete
void deleteFromResList(struct Response **list, int idToDelete) {
  struct Response *prev = NULL;
  struct Response *curr = *list;
  while (curr != NULL && curr->id != idToDelete) {
    prev = curr;
    curr = curr->next;
  }
  if (curr == NULL) {
    return;  // element not found in list
  }
  if (prev == NULL) {
    *list = curr->next;
  } else {
    prev->next = curr->next;
  }
  curr->next = NULL;
  free(curr);
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
  int portNum;

  n = read(port->recv_fd, msg,
           MAN_MAX_MSG_LENGTH); /* Get command from manager */
  if (n > 0) {                  /* Remove the first char from "msg" */
    for (i = 0; msg[i] == ' ' && i < n; i++)
      ;
    *c = msg[i];
    i++;
    for (; msg[i] == ' ' && i < n; i++)
      ;
    for (portNum = 0; portNum + i < n; portNum++) {
      msg[portNum] = msg[portNum + i];
    }
    msg[portNum] = '\0';
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

  char string[PACKET_PAYLOAD_MAX + 1];

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
  for (int portNum = 0; portNum < node_port_array_size; portNum++) {
    node_port_array[portNum] = p;
    p = p->next;
  }

  /* Initialize the job queue */
  job_queue_init(&host_q);

  /* Initialize response list */
  struct Response *responseList = NULL;
  int responseListIdNum = 0;

  while (1) {
    ////////////////////////////////////////////////////////////////////////////
    ////////////////////////////// COMMAND HANDLER /////////////////////////////

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
        case 'p':
          // Have active Host ping another host
          break;
        case 'u':
          // Upload a file from active host to another host
          break;
        case 'd':
          // Download a file from another host to active host
          break;
        default:;
      }
      // Release console_print_access semaphore
      sem_signal(&console_print_access);
    }

    ////////////////////////////////////////////////////////////////////////////
    ////////////////////////////// COMMAND HANDLER /////////////////////////////
    // -------------------------------------------------------------------------
    ////////////////////////////////////////////////////////////////////////////
    ////////////////////////////// PACKET HANDLER //////////////////////////////
    for (int portNum = 0; portNum < node_port_array_size; portNum++) {
      // Receive packets for all ports in node_port_array
      struct Packet *received_packet = createBlankPacket();
      n = packet_recv(node_port_array[portNum], received_packet);

      // if portNum has received a packet, translate the packet into a job
      if ((n > 0) && ((int)received_packet->dst == host_id)) {
#ifdef DEBUG
        colorPrint(YELLOW,
                   "DEBUG: id:%d host_main: Host %d received packet of type "
                   "%s\n",
                   host_id, (int)received_packet->dst,
                   get_packet_type_literal(received_packet->type));
#endif
        struct Job *job_from_pkt = createBlankJob();
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

    ////////////////////////////// PACKET HANDLER //////////////////////////////
    ////////////////////////////////////////////////////////////////////////////
    // -------------------------------------------------------------------------
    ////////////////////////////////////////////////////////////////////////////
    //////////////////////////////// JOB HANDLER ///////////////////////////////

    if (job_queue_length(&host_q) > 0) {
      /* Get a new job from the job queue */
      struct Job *job_from_queue = job_dequeue(host_id, &host_q);

      //////////// EXECUTE FETCHED JOB ////////////
      switch (job_from_queue->type) {
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

    //////////////////////////////// JOB HANDLER ///////////////////////////////
    ////////////////////////////////////////////////////////////////////////////

    /* The host goes to sleep for 10 ms */
    usleep(TENMILLISEC);

  } /* End of while loop */
}
