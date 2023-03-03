/*
request.c
*/

#include "request.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "constants.h"

struct Request *createRequest(requestType req_type, int ttl) {
  struct Request *r = (struct Request *)malloc(sizeof(struct Request));
  time_t t = time(NULL);  // Get Unix epoch time
  int keyMod = 1;
  for (int i = 0; i < TICKETLEN; i++) {
    keyMod *= 10;
  }
  int ticketMin = keyMod / 10;
  int ticketMax = keyMod - 1;
  r->ticket = (rand() % (ticketMax - ticketMin + 1)) + ticketMin;
  r->type = req_type;
  r->state = STATE_PENDING;
  r->timeToLive = ttl;
  memset(r->errorMsg, 0, MAX_RESPONSE_LEN);
  r->next = NULL;
  return r;
}

// Add a new Request node to the beginning of the linked list
void addToReqList(struct Request **requestList, struct Request *newRequest) {
  if (*requestList == NULL) {
    *requestList = newRequest;
    newRequest->next = NULL;
  } else {
    struct Request *current = *requestList;
    while (current->next != NULL) {
      current = current->next;
    }
    current->next = newRequest;
    newRequest->next = NULL;
  }
}

// Find a request in the request list by ticket value
struct Request *findRequestByTicket(struct Request *head, int ticket) {
  struct Request *curr = head;
  while (curr != NULL) {
    if (curr->ticket == ticket) {
      return curr;
    }
    curr = curr->next;
  }
  return NULL;
}

// Find a request in the request list by string ticket value, returns NULL if
// not found
struct Request *findRequestByStringTicket(struct Request *head, char *ticket) {
  char *endptr;
  long intTicket = strtol(ticket, &endptr, 10);  // Convert ticket to integer

  if (*endptr != '\0') {
    // Conversion failed, handle error (e.g. invalid input)
    return NULL;
  }

  return findRequestByTicket(head,
                             intTicket);  // Call integer version of function
}

// Remove and free response in list with ticket matching idToDelete
int deleteFromReqList(struct Request *head, int t) {
  struct Request *prev = NULL;
  struct Request *curr = head;
  while (curr != NULL) {
    if (curr->ticket == t) {
      if (prev == NULL) {
        head = curr->next;
      } else {
        prev->next = curr->next;
      }
      free(curr);
      curr = NULL;
      return 0;
    }
    prev = curr;
    curr = curr->next;
  }
  // Request with matching ticket value of t not found
  return -1;
}

void printList(struct Request *head) {
  struct Request *p = head;
  while (p != NULL) {
    printf("%d -> ", p->ticket);
    p = p->next;
  }
}