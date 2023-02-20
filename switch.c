/*
    switch.c
*/

#include "switch.h"

struct packet *in_packet; /* Incoming packet */
struct packet *new_packet;

void switch_main(int switch_id) {
  sleep(1);
  printf(
      "\n\033[35"  // magenta text
      "Hello from switch_main\n"
      "\033[0m"  // regular text
  );
  ////////////////// Initializing //////////////////
  struct net_port *node_port_list;
  struct net_port **node_port_array;
  int node_port_array_size;
  struct net_port *p;
  struct job_struct *new_job;
  struct job_struct *new_job2;

  ////// Initialize Router Table //////
  struct packet *in_packet;
  struct packet *new_packet;

  ////// Initialize Switch Job Queue //////
  struct job_queue switch_q;

  ////// Initialize Router Table //////
  /*
  routingTable index == switch port number
  routingTable value at index = node ID
  */
  int routingTable[MAX_NUM_ROUTES];
  for (int i = 0; i < MAX_NUM_ROUTES; i++) {
    routingTable[i] = -1;
  }

  /*
   * Create an array node_port_array[ ] to store the network link ports
   * at the switch.  The number of ports is node_port_array_size
   */
  node_port_list = net_get_port_list(switch_id);

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
  for (int k = 0; k < node_port_array_size; k++) {
    node_port_array[k] = p;
    p = p->next;
  }

  /* Initialize the job queue */
  job_queue_init(&switch_q);

  while (1) {
    /////// Receive In-Coming packet and translate it to job //////
    for (int k = 0; k < node_port_array_size; k++) {
      in_packet = (struct packet *)malloc(sizeof(struct packet));
      int n = packet_recv(node_port_array[k], in_packet);
      if ((n > 0) && ((int)in_packet->dst == switch_id)) {
#ifdef DEBUG
        printf(
            "\033[35"  // magenta text
            "DEBUG: id:%d switch_main: Switch received packet on port %d, "
            "src:%d dst:%d\n"
            "\033[0m",  // regular text
            switch_id, node_port_array[k]->link_node_id, in_packet->src,
            in_packet->dst);
#endif

        new_job = (struct job_struct *)malloc(sizeof(struct job_struct));
        new_job->in_port_index = k;
        new_job->packet = in_packet;

        // int srcPortNum = -1;
        // int dstPortNum = -1;

        //// Check to see if in_packet->src is in router table
        // for(int i = 0; i< MAX_NUM_ROUTES; i++){
        //   if(routingTable[i] == in_packet->src){

        //   }
        // }

        //// Check to see if in_packet->dst is in router table
        // switch (in_packet->type) {
        //   case (char)PKT_PING_REQ:
        //     new_job->type = JOB_PING_SEND_REPLY;
        //     job_enqueue(switch_id, &switch_q, new_job);
        //     break;

        //   case (char)PKT_PING_REPLY:
        //     free(in_packet);
        //     ping_reply_received = 1;
        //     free(new_job);
        //     break;

        //   case (char)PKT_FILE_UPLOAD_START:
        //     new_job->type = JOB_FILE_UPLOAD_RECV_START;
        //     job_enqueue(switch_id, &switch_q, new_job);
        //     break;

        //   case (char)PKT_FILE_UPLOAD_END:
        //     new_job->type = JOB_FILE_UPLOAD_RECV_END;
        //     job_enqueue(switch_id, &switch_q, new_job);
        //     break;
        //   default:
        //     free(in_packet);
        //     free(new_job);
        // }
      } else {
        free(in_packet);
      }
    }

    // //////////// FETCH JOB FROM QUEUE ////////////
    // if (job_queue_length(&switch_q) > 0) {
    //   /* Get a new job from the job queue */
    //   new_job = job_dequeue(switch_id, &switch_q);

    //   //////////// EXECUTE FETCHED JOB ////////////
    //   switch (new_job->type) {
    //     case JOB_SEND_PKT_ALL_PORTS:
    //       for (k = 0; k < node_port_array_size; k++) {
    //         packet_send(node_port_array[k], new_job->packet);
    //       }
    //       free(new_job->packet);
    //       free(new_job);
    //       break;

    //     case JOB_PING_SEND_REPLY:
    //       /* Create ping reply packet */
    //       new_packet = (struct packet *)malloc(sizeof(struct packet));
    //       new_packet->dst = new_job->packet->src;
    //       new_packet->src = (char)switch_id;
    //       new_packet->type = PKT_PING_REPLY;
    //       new_packet->length = 0;

    //       /* Create job for the ping reply */
    //       new_job2 = (struct job_struct *)malloc(sizeof(struct job_struct));
    //       new_job2->type = JOB_SEND_PKT_ALL_PORTS;
    //       new_job2->packet = new_packet;

    //       /* Enter job in the job queue */
    //       job_enqueue(switch_id, &switch_q, new_job2);

    //       /* Free old packet and job memory space */
    //       free(new_job->packet);
    //       free(new_job);
    //       break;

    //     case JOB_PING_WAIT_FOR_REPLY:
    //       if (ping_reply_received == 1) {
    //         n = snprintf(man_reply_msg, MAN_MSG_LENGTH, "Ping acknowleged!");
    //         man_reply_msg[n] = '\0';
    //         write(man_port->send_fd, man_reply_msg, n + 1);
    //         free(new_job);
    //       } else if (new_job->timeToLive > 1) {
    //         new_job->timeToLive--;
    //         job_enqueue(switch_id, &switch_q, new_job);
    //       } else { /* Time out */
    //         n = snprintf(man_reply_msg, MAN_MSG_LENGTH, "Ping timed out!");
    //         man_reply_msg[n] = '\0';
    //         write(man_port->send_fd, man_reply_msg, n + 1);
    //         free(new_job);
    //       }

    //       break;

    //       /* The next three jobs deal with uploading a file */
    //       /* This job is for the sending host */
    //     case JOB_FILE_UPLOAD_SEND:
    //       /* Open file */
    //       if (dir_valid == 1) {
    //         n = snprintf(name, MAX_FILE_NAME_LENGTH, "./%s/%s", dir,
    //                      new_job->fname_upload);
    //         name[n] = '\0';
    //         fp = fopen(name, "r");
    //         if (fp != NULL) {
    //           /*
    //            * Create first packet which
    //            * has the file name
    //            */
    //           new_packet = (struct packet *)malloc(sizeof(struct packet));
    //           new_packet->dst = new_job->file_upload_dst;
    //           new_packet->src = (char)switch_id;
    //           new_packet->type = PKT_FILE_UPLOAD_START;
    //           for (i = 0; new_job->fname_upload[i] != '\0'; i++) {
    //             new_packet->payload[i] = new_job->fname_upload[i];
    //           }
    //           new_packet->length = i;
    //           /*
    //            * Create a job to send the packet
    //            * and put it in the job queue
    //            */
    //           new_job2 = (struct job_struct *)malloc(sizeof(struct
    //           job_struct)); new_job2->type = JOB_SEND_PKT_ALL_PORTS;
    //           new_job2->packet = new_packet;
    //           job_enqueue(switch_id, &switch_q, new_job2);
    //           /*
    //            * Create the second packet which
    //            * has the file contents
    //            */
    //           new_packet = (struct packet *)malloc(sizeof(struct packet));
    //           new_packet->dst = new_job->file_upload_dst;
    //           new_packet->src = (char)switch_id;
    //           new_packet->type = PKT_FILE_UPLOAD_END;
    //           n = fread(string, sizeof(char), PKT_PAYLOAD_MAX, fp);
    //           fclose(fp);
    //           string[n] = '\0';
    //           for (i = 0; i < n; i++) {
    //             new_packet->payload[i] = string[i];
    //           }
    //           new_packet->length = n;
    //           /*
    //            * Create a job to send the packet
    //            * and put the job in the job queue
    //            */

    //           new_job2 = (struct job_struct *)malloc(sizeof(struct
    //           job_struct)); new_job2->type = JOB_SEND_PKT_ALL_PORTS;
    //           new_job2->packet = new_packet;
    //           job_enqueue(switch_id, &switch_q, new_job2);
    //           free(new_job);
    //         } else {
    //           /* Didn't open file */
    //         }
    //       }
    //       break;

    //       /* The next two jobs are for the receving host */
    //     case JOB_FILE_UPLOAD_RECV_START:
    //       /* Initialize the file buffer data structure */
    //       file_buf_init(&f_buf_upload);
    //       /*
    //        * Transfer the file name in the packet payload
    //        * to the file buffer data structure
    //        */
    //       file_buf_put_name(&f_buf_upload, new_job->packet->payload,
    //                         new_job->packet->length);
    //       free(new_job->packet);
    //       free(new_job);
    //       break;

    //     case JOB_FILE_UPLOAD_RECV_END:
    //       /*
    //        * Download packet payload into file buffer
    //        * data structure
    //        */
    //       file_buf_add(&f_buf_upload, new_job->packet->payload,
    //                    new_job->packet->length);
    //       free(new_job->packet);
    //       free(new_job);
    //       if (dir_valid == 1) {
    //         /*
    //          * Get file name from the file buffer
    //          * Then open the file
    //          */
    //         file_buf_get_name(&f_buf_upload, string);
    //         n = snprintf(name, MAX_FILE_NAME_LENGTH, "./%s/%s", dir, string);
    //         name[n] = '\0';
    //         fp = fopen(name, "w");
    //         if (fp != NULL) {
    //           /*
    //            * Write contents in the file
    //            * buffer into file
    //            */
    //           while (f_buf_upload.occ > 0) {
    //             n = file_buf_remove(&f_buf_upload, string, PKT_PAYLOAD_MAX);
    //             string[n] = '\0';
    //             n = fwrite(string, sizeof(char), n, fp);
    //           }
    //           fclose(fp);
    //         }
    //       }
    //       break;
    //   }
    // }

    /* The host goes to sleep for 10 ms */
    usleep(TENMILLISEC);

  } /* End of while loop */
}