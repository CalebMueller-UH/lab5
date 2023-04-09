/*
    job.h
*/

#pragma once

#include <stdio.h> // For FILE type

#include "constants.h"

#define JIDLEN 4 // Job id (jid) length

enum JobType
{
  JOB_SEND_PKT,
  JOB_BROADCAST_PKT,
  JOB_FORWARD_PKT,
  JOB_SEND_REQUEST,
  JOB_SEND_RESPONSE,
  JOB_WAIT_FOR_RESPONSE,
  JOB_UPLOAD,
  JOB_DOWNLOAD,
  JOB_DNS_REGISTER,
  JOB_DNS_QUERY,
  JOB_INVALID_TYPE = -1
};

enum JobState
{
  JOB_PENDING_STATE,
  JOB_COMPLETE_STATE,
  JOB_READY_STATE,
  JOB_ERROR_STATE,
  JOB_INVALID_STATE = -1
};

struct JobQueue
{
  struct Job *head;
  struct Job *tail;
  int occ;
};

struct Job
{
  char jid[JIDLEN];
  char errorMsg[MAX_MSG_LENGTH];
  int timeToLive;
  FILE *fp;
  char filepath[MAX_FILENAME_LENGTH * 2];
  enum JobType type;
  enum JobState state;
  struct Packet *packet;
  struct Job *next;
};

/* Takes an enumeration value representing a job type and returns the
 * corresponding string representation.*/
char *get_job_type_literal(enum JobType t);

/* Takes an enumeration value representing a job state and returns the
 * corresponding string representation. */
char *get_job_state_literal(enum JobState s);

/* Adds a new job to the end of a job queue.*/
void job_enqueue(int host_id, struct JobQueue *jq, struct Job *j);

/* Removes the first job from a job queue and returns it.*/
struct Job *job_dequeue(int host_id, struct JobQueue *j_q);

struct Job *job_create(const char *jid, int timeToLive, enum JobType type,
                       enum JobState state, struct Packet *packet);

/* Allocates memory for a new job, initializes its fields with default
 * values, and returns a pointer to it.*/
struct Job *job_create_empty();

void job_delete(int host_id, struct Job *j);

void job_jid_gen(char *dst);

void job_prepend_jid_to_payload(char jid[JIDLEN], struct Packet *p);

void printJob(struct Job *j);

/* Initializes a job queue.*/
void job_queue_init(struct JobQueue *jq);

/* Returns the number of jobs in a job queue.*/
int job_queue_length(struct JobQueue *jq);

struct Job *job_queue_find_id(struct JobQueue *jq, char findjid[JIDLEN]);

int job_queue_delete_id(struct JobQueue *jq, const char *deljid);