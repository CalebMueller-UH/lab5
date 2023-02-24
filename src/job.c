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
    case DEFAULT_JOB:
      return "DEFAULT_JOB";
    case BROADCAST_PKT_JOB:
      return "BROADCAST_PKT_JOB";
    case PING_REQ_JOB:
      return "PING_REQ_JOB";
    case PING_REPLY_JOB:
      return "PING_REPLY_JOB";
    case PING_WAIT_FOR_REPLY_JOB:
      return "PING_WAIT_FOR_REPLY_JOB";
    case FILE_SEND_JOB:
      return "FILE_SEND_JOB";
    case FILE_RECV_START_JOB:
      return "FILE_RECV_START_JOB";
    case FILE_RECV_END_JOB:
      return "FILE_RECV_END_JOB";
    case FILE_DOWNLOAD_REQUEST_JOB:
      return "FILE_DOWNLOAD_REQUEST_JOB";
    case SEND_REQ_RESPONSE_JOB:
      return "SEND_REQ_RESPONSE_JOB";
    case DISPLAY_REQ_RESPONSE_JOB:
      return "DISPLAY_REQ_RESPONSE_JOB";
    case FORWARD_PACKET_TO_PORT_JOB:
      return "FORWARD_PACKET_TO_PORT_JOB";
  }
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

struct Job *createBlankJob() {
  struct Job *j = (struct Job *)malloc(sizeof(struct Job));
  j->type = DEFAULT_JOB;
  j->packet = NULL;
  j->in_port_index = 0;
  j->out_port_index = 0;
  memset(j->fname_download, 0, sizeof(j->fname_download));
  memset(j->fname_upload, 0, sizeof(j->fname_upload));
  j->timeToLive = 0;
  j->file_upload_dst = 0;
  j->next = NULL;
  return j;
}