/*
request.h
*/

#pragma once

typedef enum { PING_REQ, UPLOAD_REQ, DOWNLOAD_REQ } requestType;
typedef enum { PENDING, COMPLETE } requestState;

/* Used to track multiple requests of different types*/
struct Request {
  int ticket;
  requestType type;
  requestState state;
  int timeToLive;
  struct Request *next;
};

struct Request *createRequest(int req_type, int ttl);

// Add a new Request node to the beginning of the linked list
void addToReqList(struct Request *list, struct Request *add);

// Find a request in the request list by ticket value
struct Request *findRequestByTicket(struct Request *head, int ticket);

// Find a request in the request list by string ticket value
struct Request *findRequestByStringTicket(struct Request *head, char *ticket);

// Remove and free response in list with id matching idToDelete
void deleteFromReqList(struct Request **list, int idToDelete);

void tickRequestList(struct Request *list);