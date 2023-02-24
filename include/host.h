/*
  host.h
*/

#pragma once

#include <stdio.h>

#include "common.h"

#define TENMILLISEC 10000 /* 10 millisecond sleep */

struct file_buf {
  char name[HOST_MAX_FILE_NAME_LENGTH];
  int name_length;
  char buffer[HOST_MAX_FILE_BUFFER + 1];
  int head;
  int tail;
  int occ;
  FILE *fd;
};

int is_valid_directory(const char *path);

void host_main(int host_id);
