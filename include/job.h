/*
    job.h
*/

#pragma once

#include "common.h"

enum job_type {
  DEFAULT_JOB,
  BROADCAST_PKT_JOB,
  PING_REQ_JOB,
  PING_REPLY_JOB,
  PING_WAIT_FOR_REPLY_JOB,
  FILE_SEND_JOB,
  FILE_RECV_START_JOB,
  FILE_RECV_END_JOB,
  FILE_DOWNLOAD_REQUEST_JOB,
  SEND_REQ_RESPONSE_JOB,
  DISPLAY_REQ_RESPONSE_JOB,
  FORWARD_PACKET_TO_PORT_JOB
};

struct Job_queue {
  struct Job *head;
  struct Job *tail;
  int occ;
};

struct Job {
  enum job_type type;
  struct Packet *packet;
  int in_port_index;
  int out_port_index;
  char fname_download[100];
  char fname_upload[100];
  int timeToLive;
  int file_upload_dst;
  struct Job *next;
};

/* Takes an enumeration value representing a job type and returns the
 * corresponding string representation.*/
char *get_job_type_literal(enum job_type t);

/* Adds a new job to the end of a job queue.*/
void job_enqueue(int id, struct Job_queue *j_q, struct Job *j);

/* Removes the first job from a job queue and returns it.*/
struct Job *job_dequeue(int id, struct Job_queue *j_q);

/* Initializes a job queue.*/
void job_queue_init(struct Job_queue *j_q);

/* Returns the number of jobs in a job queue.*/
int job_queue_length(struct Job_queue *j_q);

/* Allocates memory for a new job, initializes its fields with default values,
 * and returns a pointer to it.*/
struct Job *createBlankJob();
