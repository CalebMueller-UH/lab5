/*
    switch.h
*/

#pragma once

#include <stdbool.h>
#include <stdlib.h>

#include "job.h"
#include "main.h"
#include "net.h"

#define MAX_NUM_ROUTES 100

// Forward declaration
struct netport;

struct tableEntry {
  bool isValid;
  int id;
};

int searchRoutingTableForValidID(struct tableEntry* rt, int id);

void switch_main(int switch_id);
