/*
    job.c
*/

#include "job.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "color.h"
#include "extensionFns.h"
#include "packet.h"

/* Takes an enumeration value representing a job type and returns the
 * corresponding string representation. */
char *get_job_type_literal(enum JobType t)
{
  switch (t)
  {
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
  case JOB_UPLOAD:
    return "JOB_UPLOAD";
  case JOB_DOWNLOAD:
    return "JOB_DOWNLOAD";
  case JOB_INVALID_TYPE:
    return "JOB_INVALID_TYPE";
  case JOB_DNS_REGISTER:
    return "JOB_DNS_REGISTER";
  case JOB_DNS_QUERY:
    return "JOB_DNS_QUERY";
  };
  return "UNKNOWN_JobType";
}

/* Takes an enumeration value representing a job state and returns the
 * corresponding string representation. */
char *get_job_state_literal(enum JobState s)
{
  switch (s)
  {
  case JOB_PENDING_STATE:
    return "PENDING";
  case JOB_COMPLETE_STATE:
    return "COMPLETE";
  case JOB_READY_STATE:
    return "READY";
  case JOB_ERROR_STATE:
    return "ERROR";
  default:
    return "INVALID";
  }
}

/* Adds a new job to the end of a job queue. */
void job_enqueue(int host_id, struct JobQueue *jq, struct Job *jobToEnqueue)
{
#ifdef DEBUG
  colorPrint(GREEN, "DEBUG: id:%d job_enqueue: type: %s\n", host_id,
             get_job_type_literal(jobToEnqueue->type));
#endif

  if (jq->head == NULL)
  {
    jq->head = jobToEnqueue;
    jq->tail = jobToEnqueue;
    jq->occ = 1;
  }
  else
  {
    (jq->tail)->next = jobToEnqueue;
    jobToEnqueue->next = NULL;
    jq->tail = jobToEnqueue;
    jq->occ++;
  }
} // End of job_enqueue()

/* Removes the first job from a job queue and returns it. */
struct Job *job_dequeue(int host_id, struct JobQueue *jq)
{
  struct Job *j;
  if (jq->occ == 0)
    return (NULL);
  j = jq->head;

#ifdef DEBUG
  colorPrint(RED, "DEBUG: id:%d job_dequeue: type: %s\n", host_id,
             get_job_type_literal(j->type));
#endif

  jq->head = (jq->head)->next;
  jq->occ--;
  return (j);
} // End of job_dequeue()

/* job_create:
 * creates a new job with the given parameters and a randomly generated job ID
 * if no ID is provided. It initializes all other properties to the given
 * values, prepends the job ID to the packet payload if provided, and returns a
 * pointer to the new job*/
struct Job *job_create(const char *jid, int timeToLive, enum JobType type,
                       enum JobState state, struct Packet *packet)
{
  struct Job *j = job_create_empty();
  if (jid == NULL)
  {
    char t[JIDLEN];
    job_jid_gen(t);
    strncpy(j->jid, t, JIDLEN);
  }
  else
  {
    strncpy(j->jid, jid, JIDLEN);
  }
  j->timeToLive = timeToLive;
  j->type = type;
  j->state = state;
  j->packet = packet;

  if (packet != NULL)
  {
    job_prepend_jid_to_payload(j->jid, j->packet);
  }
  return j;
}

void job_delete(int host_id, struct Job *j)
{
  j->fp = NULL;
  if (j->packet)
  {
    free(j->packet);
    j->packet = NULL;
  }
  j->next = NULL;
  free(j);
}

/* job_create_empty:
 * allocates memory for a new empty job and initializes all its properties to
 * default values. It returns a pointer to the new job*/
struct Job *job_create_empty()
{
  struct Job *j = (struct Job *)malloc(sizeof(struct Job));
  if (j == NULL)
  {
    fprintf(stderr, "Failed to allocate memory for job\n");
    exit(EXIT_FAILURE);
  }
  memset(j->jid, 0, (sizeof(char) * JIDLEN));
  j->timeToLive = 0;
  j->fp = NULL;
  memset(j->filepath, 0, sizeof(j->filepath));
  j->type = JOB_INVALID_TYPE;
  j->state = JOB_INVALID_STATE;
  j->packet = NULL;
  j->next = NULL;
  return j;
}

/* job_jid_gen:
 * generates a random job ID string of length JIDLEN and
 * stores it in the dst parameter.
 * Returns 1 if successful, or 0 if the generated ID is not of the expected
 * length.*/
void job_jid_gen(char *dst)
{
  time_t t = time(NULL);
  int keyMod = 1;
  for (int i = 0; i < JIDLEN; i++)
  {
    keyMod *= 10;
  }
  int ticketMin = keyMod / 10;
  int ticketMax = keyMod - 1;
  int jidInt = (rand() % (ticketMax - ticketMin + 1)) + ticketMin;
  int len = snprintf(NULL, 0, "%d", jidInt);
  snprintf(dst, len + 1, "%d", jidInt);
  dst[len] = '\0';
}

/* job_prepend_jid_to_payload:
 * prepends the job identifier jid to the payload of a given Packet structure,
 * separated by a colon character. If jid is not already present in the payload,
 * it is added to the beginning of the payload */
void job_prepend_jid_to_payload(char jid[JIDLEN], struct Packet *p)
{
  if (strstr(p->payload, jid) == NULL)
  {
    char pbuff[PACKET_PAYLOAD_MAX] = {0};
    snprintf_s(pbuff, PACKET_PAYLOAD_MAX - 1, "%s:%s", jid, p->payload);
    strncpy(p->payload, pbuff, PACKET_PAYLOAD_MAX);
    p->length = strlen(pbuff);
  }
}

/* Prints the contents of a job with its job ID, time to live, file pointer,
 * type, state, and associated packet. */
void printJob(struct Job *j)
{
  colorPrint(BLUE, "jid:%s ttl:%d fp:%p type:%s state:%s packet: ", j->jid,
             j->timeToLive, j->fp, get_job_type_literal(j->type),
             get_job_state_literal(j->state));
  if (j->packet == NULL)
  {
    printf("NULL\n");
  }
  else
  {
    printf("\n");
    printPacket(j->packet);
  }
}

/* Initializes a job queue. */
void job_queue_init(struct JobQueue *jq)
{
  jq->occ = 0;
  jq->head = NULL;
  jq->tail = NULL;
}

/* Returns the number of jobs in a job queue. */
int job_queue_length(struct JobQueue *jq) { return jq->occ; }

/* job_queue_find_id:
 * searches through a job queue for a job with a matching ID. It takes as
 * input a pointer to the job queue (struct JobQueue *jq) and the ID we are
 * looking for (char findjid[JIDLEN])
 * Returns a pointer to the matching job, or NULL if no match is found  */
struct Job *job_queue_find_id(struct JobQueue *jq, char findjid[JIDLEN])
{
  struct Job *curr = jq->head;
  while (curr != NULL)
  {
    if (strcmp(curr->jid, findjid) == 0)
    {
      return curr;
    }
    curr = curr->next;
  }
  return NULL;
}

/*job_queue_delete_id:
 *searches a job queue for a job with a given ID,
 *removes it from the queue, and updates the linked list structure. It returns 1
 *if successful, 0 if the job is not found. */
int job_queue_delete_id(struct JobQueue *jq, const char *deljid)
{
  struct Job *prev = NULL;
  struct Job *curr = jq->head;
  while (curr != NULL)
  {
    if (strcmp(curr->jid, deljid) == 0)
    {
      if (prev == NULL)
      { // Case when deleting the head
        jq->head = curr->next;
      }
      else
      {
        prev->next = curr->next;
      }
      if (jq->tail == curr)
      { // Case when deleting the tail
        jq->tail = prev;
      }
      free(curr);
      jq->occ--;
      return 1;
    }
    prev = curr;
    curr = curr->next;
  }
  return 0;
}