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

void switch_main(int switch_id);
