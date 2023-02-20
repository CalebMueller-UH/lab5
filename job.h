/*
    job.h
*/

#pragma once

#include <stdio.h>
#include <stdlib.h>

struct job_queue {
  struct job_struct *head;
  struct job_struct *tail;
  int occ;
};

enum job_type {
  JOB_SEND_PKT_ALL_PORTS,
  JOB_PING_SEND_REQ,
  JOB_PING_SEND_REPLY,
  JOB_PING_WAIT_FOR_REPLY,
  JOB_FILE_UPLOAD_SEND,
  JOB_FILE_UPLOAD_RECV_START,
  JOB_FILE_UPLOAD_RECV_END,
  FORWARD_PACKET_TO_PORT,
  UNKNOWN_PORT_BROADCAST
};

struct job_struct {
  enum job_type type;
  struct packet *packet;
  int in_port_index;
  int out_port_index;
  char fname_download[100];
  char fname_upload[100];
  int timeToLive;
  int file_upload_dst;
  struct job_struct *next;
};

/////////////////////////////////////////////////////////
/////////////// Job Related Functions ///////////////////

char *get_job_type_literal(enum job_type t);

/* Add a job to the job queue */
void job_enqueue(int id, struct job_queue *j_q, struct job_struct *j);

/* Remove job from the job queue, and return pointer to the job*/
struct job_struct *job_dequeue(int id, struct job_queue *j_q);

/* Initialize job queue */
void job_queue_init(struct job_queue *j_q);

/*
This function returns the number of jobs in a job queue.
Parameters:
j_q - a pointer to a job queue structure
Returns:
int - the number of jobs in the job queue
*/
int job_queue_length(struct job_queue *j_q);

/////////////// Job Related Functions ///////////////////
/////////////////////////////////////////////////////////