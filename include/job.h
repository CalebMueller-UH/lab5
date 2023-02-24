/*
    job.h
*/

#pragma once

#include "common.h"

enum job_type {
  JOB_DEFAULT,
  JOB_BROADCAST_PKT,
  JOB_PING_REQ,
  JOB_PING_REPLY,
  JOB_PING_WAIT_FOR_REPLY,
  JOB_FILE_UPLOAD_REQ,
  JOB_FILE_UPLOAD_SEND,
  JOB_FILE_RECV_START,
  JOB_FILE_RECV_END,
  JOB_FILE_DOWNLOAD_REQUEST,
  JOB_SEND_REQ_RESPONSE,
  JOB_DISPLAY_REQ_RESPONSE,
  JOB_FORWARD_PACKET_TO_PORT
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
