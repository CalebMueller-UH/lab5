/*
    switch.h
*/

#pragma once

// Forward declarations
struct Net_port;
struct Job;

struct TableEntry {
  int id;
  struct TableEntry *next;
};

long long current_time_ms();

void controlPacketSender_endpoint(int nodeId, struct Net_port **node_port_array,
                                  int node_port_array_size);

void switch_main(int switch_id);
