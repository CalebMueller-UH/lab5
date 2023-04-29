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

void periodicControlPacketSender(int id, struct Net_port **node_port_array,
                                 int node_port_array_size, int localRootID,
                                 int localRootDist, int localParentID,
                                 int *localPortTree, const char nodeType);

void switch_main(int switch_id);
