/*
    switch.h
*/

#pragma once

#include <stdbool.h>

#define MAX_NUM_ROUTES 100

// Forward declarations
struct  net_port;
struct  Job;

struct  tableEntry {
  bool isValid;
  int id;
};

int searchRoutingTableForValidID(struct  tableEntry *rt, int id);

void broadcastToAllButSender(struct  Job *job,
                             struct  tableEntry *rt,
                             struct  net_port **port_array,
                             int port_array_size);

void switch_main(int switch_id);
