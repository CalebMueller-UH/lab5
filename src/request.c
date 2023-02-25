/*
request.c
*/

#include "request.h"

int CURRENT_REQUEST_TICKET = 0;

struct Request *createRequest(int ticket, int req_type, int ttl) {
  struct Request *r = (struct Request *)malloc(sizeof(struct Request));
  r->ticket = ticket;
  r->req_type = req_type;
  r->timeToLive = ttl;
  r->next = NULL;
  return r;
}

// Add a new Request node to the beginning of the linked list
void addToReqList(struct Request *list, struct Request *add) {
  add->next = list;
  list = add;
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
