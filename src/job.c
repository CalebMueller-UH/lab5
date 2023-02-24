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
    case JOB_DEFAULT:
      return "JOB_DEFAULT";
    case JOB_BROADCAST_PKT:
      return "JOB_BROADCAST_PKT";
    case JOB_PING_REQ:
      return "JOB_PING_REQ";
    case JOB_PING_REPLY:
      return "JOB_PING_REPLY";
    case JOB_WAIT_FOR_RESPONSE:
      return "JOB_WAIT_FOR_RESPONSE";
    case JOB_FILE_UPLOAD_REQ:
      return "JOB_FILE_UPLOAD_REQ";
    case JOB_FILE_UPLOAD_SEND:
      return "JOB_FILE_UPLOAD_SEND";
    case JOB_FILE_RECV_START:
      return "JOB_FILE_RECV_START";
    case JOB_FILE_RECV_END:
      return "JOB_FILE_RECV_END";
    case JOB_FILE_DOWNLOAD_REQUEST:
      return "JOB_FILE_DOWNLOAD_REQUEST";
    case JOB_SEND_REQ_RESPONSE:
      return "JOB_SEND_REQ_RESPONSE";
    case JOB_DISPLAY_REQ_RESPONSE:
      return "JOB_DISPLAY_REQ_RESPONSE";
    case JOB_FORWARD_PACKET_TO_PORT:
      return "JOB_FORWARD_PACKET_TO_PORT";
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
  j->type = JOB_DEFAULT;
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