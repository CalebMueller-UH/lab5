/*
    job.h
*/

#pragma once

#include "common.h"

enum job_type {
  DEFAULT,
  SEND_PKT_ALL_PORTS,
  PING_SEND_REQ,
  PING_SEND_REPLY,
  PING_WAIT_FOR_REPLY,
  FILE_UPLOAD_SEND,
  FILE_UPLOAD_RECV_START,
  FILE_UPLOAD_RECV_END,
  FILE_DOWNLOAD_REQUEST,
  SEND_REQUEST_RESPONSE,
  DISPLAY_REQUEST_RESPONSE,
  FORWARD_PACKET_TO_PORT,
  UNKNOWN_PORT_BROADCAST
};

struct  Job_queue {
  struct  Job *head;
  struct  Job *tail;
  int occ;
};

struct  Job {
  enum job_type type;
  struct  Packet *packet;
  int in_port_index;
  int out_port_index;
  char fname_download[100];
  char fname_upload[100];
  int timeToLive;
  int file_upload_dst;
  struct  Job *next;
};

/* Takes an enumeration value representing a job type and returns the
 * corresponding string representation.*/
char *get_job_type_literal(enum job_type t);

/* Adds a new job to the end of a job queue.*/
void job_enqueue(int id, struct  Job_queue *j_q,
                 struct  Job *j);

/* Removes the first job from a job queue and returns it.*/
struct  Job *job_dequeue(int id, struct  Job_queue *j_q);

/* Initializes a job queue.*/
void job_queue_init(struct  Job_queue *j_q);

/* Returns the number of jobs in a job queue.*/
int job_queue_length(struct  Job_queue *j_q);

/* Allocates memory for a new job, initializes its fields with default values,
 * and returns a pointer to it.*/
struct  Job *createBlankJob();
