/*
request.c
*/

#include "request.h"

#include <stdio.h>
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
void addToReqList(struct Request **head, struct Request *add) {
  printf("added\n");
  add->next = (*head);
  (*head) = add;
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