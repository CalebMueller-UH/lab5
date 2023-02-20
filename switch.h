/*
    switch.h
*/

#pragma once

#include <stdlib.h>

#include "job.h"
#include "main.h"
#include "net.h"

#define MAX_NUM_ROUTES 100

// Forward declaration
struct netport;

void switch_main(int switch_id);