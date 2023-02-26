/*
    job.c
*/

#include "job.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "color.h"
#include "packet.h"

/* Takes an enumeration value representing a job type and returns the
 * corresponding string representation. */
char *get_job_type_literal(enum job_type t) {
  switch (t) {
    case JOB_SEND_PKT:
      return "JOB_SEND_PKT";
    case JOB_BROADCAST_PKT:
      return "JOB_BROADCAST_PKT";
    case JOB_FORWARD_PKT:
      return "JOB_FORWARD_PKT";
    case JOB_SEND_REQUEST:
      return "JOB_SEND_REQUEST";
    case JOB_SEND_RESPONSE:
      return "JOB_SEND_RESPONSE";
    case JOB_WAIT_FOR_RESPONSE:
      return "JOB_WAIT_FOR_RESPONSE";
    case JOB_UPLOAD_SEND:
      return "JOB_UPLOAD_SEND";
    case JOB_RECV_START:
      return "JOB_RECV_START";
    case JOB_RECV_END:
      return "JOB_RECV_END";
    case JOB_INVALID_TYPE:
      return "JOB_INVALID_TYPE";
  };
  return "UNKNOWN_JOB_TYPE";
}

/* Adds a new job to the end of a job queue. */
void job_enqueue(int id, struct Job_queue *j_q, struct Job *j) {
#ifdef DEBUG
  colorPrint(GREEN, "DEBUG: id:%d job_enqueue: Job.type: %s\n", id,
             get_job_type_literal(j->type));
#endif
  if (j_q->head == NULL) {
    j_q->head = j;
    j_q->tail = j;
    j_q->occ = 1;
  } else {
    (j_q->tail)->next = j;
    j->next = NULL;
    j_q->tail = j;
    j_q->occ++;
  }
}

/* Removes the first job from a job queue and returns it. */
struct Job *job_dequeue(int id, struct Job_queue *j_q) {
  struct Job *j;
  if (j_q->occ == 0) return (NULL);
  j = j_q->head;

#ifdef DEBUG
  colorPrint(RED, "DEBUG: id:%d job_dequeue: Job.type: %s\n", id,
             get_job_type_literal(j->type));
#endif

  j_q->head = (j_q->head)->next;
  j_q->occ--;
  return (j);
}

/* Initializes a job queue. */
void job_queue_init(struct Job_queue *j_q) {
  j_q->occ = 0;
  j_q->head = NULL;
  j_q->tail = NULL;
}

/* Returns the number of jobs in a job queue. */
int job_queue_length(struct Job_queue *j_q) { return j_q->occ; }

struct Job *createJob(enum job_type type, struct Packet *packet) {
  struct Job *j = createEmptyJob();
  j->type = type;
  j->packet = packet;
  return j;
}

struct Job *createEmptyJob() {
  struct Job *j = (struct Job *)malloc(sizeof(struct Job));
  j->type = JOB_INVALID_TYPE;
  j->packet = NULL;
  j->request = NULL;
  j->next = NULL;
  return j;
}
