/*
  host.h
*/

#pragma once

#include <stdio.h>

#include "constants.h"

#define LOOP_SLEEP_TIME_MS 10000 /* 10 millisecond sleep */

int isValidDirectory(const char *path);

void host_main(int host_id);
