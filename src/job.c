/*
    job.c
*/

#include "job.h"

/////////////////////////////////////////////////////////
/////////////// Job Related Functions ///////////////////

/*
This function takes in an enum job_type and returns a string literal that
corresponds to the enum. It does this by using a switch statement to check the
value of the enum and then returning the corresponding string literal. If the
enum value is not found, it will return "UNKNOWN JOB TYPE".
*/
char *get_job_type_literal(enum job_type t) {
  switch (t) {
    case SEND_PKT_ALL_PORTS:
      return "SEND_PKT_ALL_PORTS";
    case PING_SEND_REQ:
      return "PING_SEND_REQ";
    case PING_SEND_REPLY:
      return "PING_SEND_REPLY";
    case PING_WAIT_FOR_REPLY:
      return "PING_WAIT_FOR_REPLY";
    case FILE_UPLOAD_SEND:
      return "FILE_UPLOAD_SEND";
    case FILE_UPLOAD_RECV_START:
      return "FILE_UPLOAD_RECV_START";
    case FILE_UPLOAD_RECV_END:
      return "FILE_UPLOAD_RECV_END";
    case FILE_DOWNLOAD_REQUEST:
      return "FILE_DOWNLOAD_REQUEST";
    case SEND_REQUEST_RESPONSE:
      return "SEND_REQUEST_RESPONSE";
    case DISPLAY_REQUEST_RESPONSE:
      return "DISPLAY_REQUEST_RESPONSE";
    case FORWARD_PACKET_TO_PORT:
      return "FORWARD_PACKET_TO_PORT";
    case UNKNOWN_PORT_BROADCAST:
      return "UNKNOWN_PORT_BROADCAST";
  }
  return "UNKNOWN JOB TYPE";
}

/* Add a job to the job queue */
void job_enqueue(int id, struct job_queue *j_q, struct job_struct *j) {
#ifdef DEBUG
  colorPrint(GREEN, "DEBUG: id:%d job_enqueue: job_struct.type: %s\n", id,
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

/* Remove job from the job queue, and return pointer to the job*/
struct job_struct *job_dequeue(int id, struct job_queue *j_q) {
  struct job_struct *j;
  if (j_q->occ == 0) return (NULL);
  j = j_q->head;

#ifdef DEBUG
  colorPrint(RED, "DEBUG: id:%d job_dequeue: job_struct.type: %s\n", id,
             get_job_type_literal(j->type));
#endif

  j_q->head = (j_q->head)->next;
  j_q->occ--;
  return (j);
}

/* Initialize job queue */
void job_queue_init(struct job_queue *j_q) {
  j_q->occ = 0;
  j_q->head = NULL;
  j_q->tail = NULL;
}

/*
This function returns the number of jobs in a job queue.
Parameters:
j_q - a pointer to a job queue structure
Returns:
int - the number of jobs in the job queue
*/
int job_queue_length(struct job_queue *j_q) { return j_q->occ; }