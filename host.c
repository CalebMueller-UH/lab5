/*
  host.c
*/

#include "host.h"

/* File buffer operations */
/* Initialize file buffer data structure */
void file_buf_init(struct file_buf *f) {
  f->head = 0;
  f->tail = MAX_FILE_BUFFER;
  f->occ = 0;
  f->name_length = 0;
}

/* Get the file name in the file buffer and store it in name
   Terminate the string in name with the null character
 */
void file_buf_get_name(struct file_buf *f, char name[]) {
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
void file_buf_put_name(struct file_buf *f, char name[], int length) {
  int i;

  for (i = 0; i < length; i++) {
    f->name[i] = name[i];
  }
  f->name_length = length;
}

/*
 *  Add 'length' bytes n string[] to the file buffer
 */
int file_buf_add(struct file_buf *f, char string[], int length) {
  int i = 0;

  while (i < length && f->occ < MAX_FILE_BUFFER) {
    f->tail = (f->tail + 1) % (MAX_FILE_BUFFER + 1);
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
int file_buf_remove(struct file_buf *f, char string[], int length) {
  int i = 0;

  while (i < length && f->occ > 0) {
    string[i] = f->buffer[f->head];
    f->head = (f->head + 1) % (MAX_FILE_BUFFER + 1);
    i++;
    f->occ--;
  }

  return (i);
}

/*
 * Operations with the manager
 */

int get_man_command(struct man_port_at_host *port, char msg[], char *c) {
  int n;
  int i;
  int k;

  n = read(port->recv_fd, msg, MAN_MSG_LENGTH); /* Get command from manager */
  if (n > 0) { /* Remove the first char from "msg" */
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
  char reply_msg[MAX_MSG_LENGTH];

  if (dir_valid == 1) {
    n = snprintf(reply_msg, MAX_MSG_LENGTH, "\x1b[32;1m%s %d\x1b[0m", dir,
                 host_id);
  } else {
    n = snprintf(reply_msg, MAX_MSG_LENGTH, "\x1b[32;1mNone %d\x1b[0m",
                 host_id);
  }

  write(port->send_fd, reply_msg, n);
}

////////////////////////////////////////////////
////////////////// HOST MAIN ///////////////////
void host_main(int host_id) {
  /* Initialize State */
  char dir[MAX_DIR_NAME];
  int dir_valid = 0;

  char man_msg[MAN_MSG_LENGTH];
  char man_reply_msg[MAN_MSG_LENGTH];
  char man_cmd;
  struct man_port_at_host *man_port;  // Port to the manager

  struct net_port *node_port_list;
  struct net_port **node_port_array;  // Array of pointers to node ports
  int node_port_array_size;           // Number of node ports

  int ping_reply_received;

  int i, k, n;
  int dst;
  char name[MAX_FILE_NAME_LENGTH];
  char string[PKT_PAYLOAD_MAX + 1];

  FILE *fp;

  struct packet *in_packet; /* Incoming packet */
  struct packet *new_packet;

  struct net_port *p;
  struct job_struct *new_job;
  struct job_struct *new_job2;

  struct job_queue host_q;

  struct file_buf f_buf_upload;
  struct file_buf f_buf_download;

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
      switch (man_cmd) {
        case 's':
          reply_display_host_state(man_port, dir, dir_valid, host_id);
          break;

        case 'm':
          size_t len = strnlen(man_msg, MAX_DIR_NAME - 1);
          if (is_valid_directory(man_msg)) {
            memcpy(dir, man_msg, len);
            dir[len] = '\0';  // add null character
            dir_valid = 1;
            printf("\x1b[32mhost%d's main directory set to %s\x1b[0m\n",
                   host_id, dir);
          } else {
            printf("\x1b[31m%s is not a valid directory\x1b[0m\n", man_msg);
          }
          break;

        case 'p':  // Sending ping request
          // Create new ping request packet
          sscanf(man_msg, "%d", &dst);
          new_packet = (struct packet *)malloc(sizeof(struct packet));
          new_packet->src = (char)host_id;
          new_packet->dst = (char)dst;
          new_packet->type = (char)PKT_PING_REQ;
          new_packet->length = 0;
          new_job = (struct job_struct *)malloc(sizeof(struct job_struct));
          new_job->packet = new_packet;
          new_job->type = SEND_PKT_ALL_PORTS;
          job_enqueue(host_id, &host_q, new_job);

          new_job2 = (struct job_struct *)malloc(sizeof(struct job_struct));
          ping_reply_received = 0;
          new_job2->type = PING_WAIT_FOR_REPLY;
          new_job2->timeToLive = 10;
          job_enqueue(host_id, &host_q, new_job2);
          break;

        case 'u': /* Upload a file to a host */
          sscanf(man_msg, "%d %s", &dst, name);
          new_job = (struct job_struct *)malloc(sizeof(struct job_struct));
          new_job->type = FILE_UPLOAD_SEND;
          new_job->file_upload_dst = dst;
          for (i = 0; name[i] != '\0'; i++) {
            new_job->fname_upload[i] = name[i];
          }
          new_job->fname_upload[i] = '\0';
          job_enqueue(host_id, &host_q, new_job);
          break;

        case 'd': /* Download a file to host */
          sscanf(man_msg, "%d %s", &dst, name);
          new_packet = (struct packet *)malloc(sizeof(struct packet));
          new_packet->src = (char)host_id;
          new_packet->dst = (char)dst;
          new_packet->type = (char)PKT_FILE_DOWNLOAD_REQUEST;
          new_packet->length = strnlen(name, MAX_DIR_NAME);
          new_job = (struct job_struct *)malloc(sizeof(struct job_struct));
          new_job->packet = new_packet;
          new_job->type = FILE_DOWNLOAD_REQUEST;
          job_enqueue(host_id, &host_q, new_job);
          break;

        default:;
      }
    }

    /////// Receive In-Coming packet and translate it to job //////
    for (k = 0; k < node_port_array_size; k++) {
      in_packet = (struct packet *)malloc(sizeof(struct packet));
      n = packet_recv(node_port_array[k], in_packet);
      if ((n > 0) && ((int)in_packet->dst == host_id)) {
#ifdef DEBUG
        printf(
            "\033[0;33m"  // yellow text
            "DEBUG: id:%d host_main: Host %d received packet of type %s\n"
            "\033[0m",  // regular text
            host_id, (int)in_packet->dst,
            get_job_type_literal(in_packet->type));
#endif
        new_job = (struct job_struct *)malloc(sizeof(struct job_struct));
        new_job->in_port_index = k;
        new_job->packet = in_packet;

        switch (in_packet->type) {
          case (char)PKT_PING_REQ:
            new_job->type = PING_SEND_REPLY;
            job_enqueue(host_id, &host_q, new_job);
            break;

          case (char)PKT_PING_REPLY:
            free(in_packet);
            ping_reply_received = 1;
            free(new_job);
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
            new_job->type = FILE_UPLOAD_SEND;
            job_enqueue(host_id, &host_q, new_job);
            break;

          default:
            free(in_packet);
            free(new_job);
        }
      } else {
        free(in_packet);
      }
    }

    //////////// FETCH JOB FROM QUEUE ////////////
    if (job_queue_length(&host_q) > 0) {
      /* Get a new job from the job queue */
      new_job = job_dequeue(host_id, &host_q);

      //////////// EXECUTE FETCHED JOB ////////////
      switch (new_job->type) {
        case SEND_PKT_ALL_PORTS:
          for (k = 0; k < node_port_array_size; k++) {
            packet_send(node_port_array[k], new_job->packet);
          }
          free(new_job->packet);
          free(new_job);
          break;

        case PING_SEND_REPLY:
          /* Create ping reply packet */
          new_packet = (struct packet *)malloc(sizeof(struct packet));
          new_packet->dst = new_job->packet->src;
          new_packet->src = (char)host_id;
          new_packet->type = PKT_PING_REPLY;
          new_packet->length = 0;

          /* Create job for the ping reply */
          new_job2 = (struct job_struct *)malloc(sizeof(struct job_struct));
          new_job2->type = SEND_PKT_ALL_PORTS;
          new_job2->packet = new_packet;

          /* Enter job in the job queue */
          job_enqueue(host_id, &host_q, new_job2);

          /* Free old packet and job memory space */
          free(new_job->packet);
          free(new_job);
          break;

        case PING_WAIT_FOR_REPLY:
          if (ping_reply_received == 1) {
            n = snprintf(man_reply_msg, MAN_MSG_LENGTH,
                         "\x1b[32;1mPing acknowleged!\x1b[0m");
            man_reply_msg[n] = '\0';
            write(man_port->send_fd, man_reply_msg, n + 1);
            free(new_job);
          } else if (new_job->timeToLive > 1) {
            new_job->timeToLive--;
            job_enqueue(host_id, &host_q, new_job);
          } else { /* Time out */
            n = snprintf(man_reply_msg, MAN_MSG_LENGTH,
                         "\x1b[31;1mPing timed out!\x1b[0m");
            man_reply_msg[n] = '\0';
            write(man_port->send_fd, man_reply_msg, n + 1);
            free(new_job);
          }

          break;

          /* The next three jobs deal with uploading a file */
          /* This job is for the sending host */
        case FILE_UPLOAD_SEND:
          /* Open file */
          if (dir_valid == 1) {
            n = snprintf(name, MAX_FILE_NAME_LENGTH, "./%s/%s", dir,
                         new_job->fname_upload);
            name[n] = '\0';
            fp = fopen(name, "r");
            if (fp != NULL) {
              /*
               * Create first packet which
               * has the file name
               */
              new_packet = (struct packet *)malloc(sizeof(struct packet));
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
              new_job2 = (struct job_struct *)malloc(sizeof(struct job_struct));
              new_job2->type = SEND_PKT_ALL_PORTS;
              new_job2->packet = new_packet;
              job_enqueue(host_id, &host_q, new_job2);
              /*
               * Create the second packet which
               * has the file contents
               */
              new_packet = (struct packet *)malloc(sizeof(struct packet));
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

              new_job2 = (struct job_struct *)malloc(sizeof(struct job_struct));
              new_job2->type = SEND_PKT_ALL_PORTS;
              new_job2->packet = new_packet;
              job_enqueue(host_id, &host_q, new_job2);
              free(new_job);
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
          if (dir_valid == 1) {
            /*
             * Get file name from the file buffer
             * Then open the file
             */
            file_buf_get_name(&f_buf_upload, string);
            n = snprintf(name, MAX_FILE_NAME_LENGTH, "./%s/%s", dir, string);
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
          }
          break;

        case FILE_DOWNLOAD_REQUEST:

          // Find which net_port entry in net_port_array has desired destination
          int destIndex = -1;
          for (int i = 0; i < node_port_array_size; i++) {
            if (node_port_array[i]->link_node_id == (int)new_job->packet->dst) {
              destIndex = i;
            }
          }
          // If node_port_array had the destination id, send to that node
          if (destIndex >= 0) {
            packet_send(node_port_array[destIndex], new_job->packet);
          } else {
            // Else, broadcast packet to all hosts
            for (k = 0; k < node_port_array_size; k++) {
              packet_send(node_port_array[k], new_job->packet);
            }
          }
          free(new_job->packet);
          free(new_job);
          break;
      }
    }

    /* The host goes to sleep for 10 ms */
    usleep(TENMILLISEC);

  } /* End of while loop */
}
