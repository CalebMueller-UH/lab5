/*
  host.h
*/

#pragma once

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

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

enum host_job_type {
  JOB_SEND_PKT_ALL_PORTS,
  JOB_PING_SEND_REQ,
  JOB_PING_SEND_REPLY,
  JOB_PING_WAIT_FOR_REPLY,
  JOB_FILE_UPLOAD_SEND,
  JOB_FILE_UPLOAD_RECV_START,
  JOB_FILE_UPLOAD_RECV_END
};

struct host_job {
  enum host_job_type type;
  struct packet *packet;
  int in_port_index;
  int out_port_index;
  char fname_download[100];
  char fname_upload[100];
  int ping_timer;
  int file_upload_dst;
  struct host_job *next;
};

struct job_queue {
  struct host_job *head;
  struct host_job *tail;
  int occ;
};

void host_main(int host_id);
