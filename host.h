/*
  host.h
*/

#pragma once

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "job.h"
#include "main.h"
#include "man.h"
#include "net.h"
#include "packet.h"

#define MAX_FILE_BUFFER 1000
#define MAX_MSG_LENGTH 100
#define MAX_DIR_NAME 100
#define MAX_FILE_NAME_LENGTH 100
#define PKT_PAYLOAD_MAX 100
#define TENMILLISEC 10000 /* 10 millisecond sleep */

/* Types of packets */
struct file_buf {
  char name[MAX_FILE_NAME_LENGTH];
  int name_length;
  char buffer[MAX_FILE_BUFFER + 1];
  int head;
  int tail;
  int occ;
  FILE *fd;
};

int is_valid_directory(const char *path);

void host_main(int host_id);
