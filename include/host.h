/*
  host.h
*/

#pragma once

#include <stdio.h>

#include "common.h"

#define TENMILLISEC 10000 /* 10 millisecond sleep */

struct  File_buf {
  char name[MAX_FILENAME_LENGTH];
  int name_length;
  char buffer[HOST_MAX_FILE_BUFFER + 1];
  int head;
  int tail;
  int occ;
  FILE *fd;
};

int isValidDirectory(const char *path);

void host_main(int host_id);
