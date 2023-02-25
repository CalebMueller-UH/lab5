/*
    job.h
*/

#pragma once

#include "constants.h"

enum job_type {
  JOB_DEFAULT,
  JOB_REQUEST,
  JOB_RESPONSE,
  JOB_SEND_PKT,
  JOB_BROADCAST_PKT,
  JOB_FORWARD_PKT,
  JOB_SEND_REQUEST,
  JOB_SEND_RESPONSE,
  JOB_WAIT_FOR_RESPONSE,
  JOB_FILE_UPLOAD_SEND,
  JOB_FILE_RECV_START,
  JOB_FILE_RECV_END,
};

struct Job_queue {
  struct Job *head;
  struct Job *tail;
  int occ;
};

// Forward declaration
struct Request;

struct Job {
  enum job_type type;
  struct Packet *packet;
  int timeToLive;
  struct Request *request;
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

struct Job *createJob(enum job_type type, struct Packet *packet);

/* Allocates memory for a new job, initializes its fields with default
 * values, and returns a pointer to it.*/
struct Job *createEmptyJob();
