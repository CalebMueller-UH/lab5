/*
    job.c
*/

#include "job.h"

/////////////////////////////////////////////////////////
/////////////// Job Related Functions ///////////////////

/* Add a job to the job queue */
void job_q_add(struct job_queue *j_q, struct job_struct *j) {
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
struct job_struct *job_q_remove(struct job_queue *j_q) {
  struct job_struct *j;

  if (j_q->occ == 0) return (NULL);
  j = j_q->head;
  j_q->head = (j_q->head)->next;
  j_q->occ--;
  return (j);
}

/* Initialize job queue */
void job_q_init(struct job_queue *j_q) {
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
int job_q_num(struct job_queue *j_q) { return j_q->occ; }

/////////////// Job Related Functions ///////////////////
/////////////////////////////////////////////////////////