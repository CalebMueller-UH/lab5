/*
request.h
*/

#pragma once

typedef enum RequestType {
  PING_REQ,
  UPLOAD_REQ,
  DOWNLOAD_REQ
}

/* Used to track multiple requests of different types*/
struct Request {
  int id;
  int req_type;
  int timeToLive;
  struct Request *next;
};

struct Request *createRequest(int id, int req_type, int ttl);

// Add a new Request node to the beginning of the linked list
void addToReqList(struct Request *list, struct Request *add);

// Remove and free response in list with id matching idToDelete
void deleteFromReqList(struct Request **list, int idToDelete);