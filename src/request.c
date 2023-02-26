/*
request.c
*/

#include "request.h"

#include <stdlib.h>
#include <time.h>

struct Request *createRequest(int req_type, int ttl) {
  struct Request *r = (struct Request *)malloc(sizeof(struct Request));
  time_t t = time(NULL);  // Get Unix epoch time
  r->ticket = ((int)t) % 10000;
  r->type = req_type;
  r->state = PENDING;
  r->timeToLive = ttl;
  r->next = NULL;
  return r;
}

// Add a new Request node to the beginning of the linked list
void addToReqList(struct Request *list, struct Request *add) {
  add->next = list;
  list = add;
}

// Find a request in the request list by ticket value
struct Request *findRequestByTicket(struct Request *head, int ticket) {
  struct Request *current = head;
  while (current != NULL) {
    if (current->ticket == ticket) {
      return current;
    }
    current = current->next;
  }
  return NULL;
}

// Find a request in the request list by string ticket value
struct Request *findRequestByStringTicket(struct Request *head, char *ticket) {
  int intTicket = atoi(ticket);  // Convert ticket to integer
  return findRequestByTicket(head,
                             intTicket);  // Call integer version of function
}

// Remove and free response in list with ticket matching idToDelete
void deleteFromReqList(struct Request **list, int idToDelete) {
  struct Request *prev = NULL;
  struct Request *curr = *list;
  while (curr != NULL && curr->ticket != idToDelete) {
    prev = curr;
    curr = curr->next;
  }
  if (curr == NULL) {
    return;  // element not found in list
  }
  if (prev == NULL) {
    *list = curr->next;
  } else {
    prev->next = curr->next;
  }
  curr->next = NULL;
  free(curr);
}

void tickRequestList(struct Request *list) {
  struct Request *h;
  for (h = list; h != NULL; h = h->next) {
    h->timeToLive--;
  }
}