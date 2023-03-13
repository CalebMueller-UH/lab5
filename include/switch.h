/*
    switch.h
*/

#pragma once

#include <stdbool.h>

// Forward declarations
struct Net_port;
struct Job;

struct TableEntry {
  bool isValid;
  int id;
};

int searchRoutingTableForValidID(struct TableEntry *rt, int id);

void broadcastToAllButSender(struct Job *job, struct TableEntry *rt,
                             struct Net_port **port_array, int port_array_size);

void switch_main(int switch_id);
