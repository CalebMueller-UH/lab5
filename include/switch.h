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
  struct TableEntry *next;
};

void switch_main(int switch_id);
