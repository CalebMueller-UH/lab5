/*
    switch.h
*/

#pragma once

#include <stdlib.h>

#include "main.h"
#include "net.h"

// Forward declaration of struct netport to appease the linter
struct netport;

enum switch_job_type {
  SJOB_SEND_PKT_ALL_PORTS,
  SJOB_PING_SEND_REQ,
  SJOB_PING_SEND_REPLY,
  SJOB_PING_WAIT_FOR_REPLY,
  SJOB_FILE_UPLOAD_SEND,
  SJOB_FILE_UPLOAD_RECV_START,
  SJOB_FILE_UPLOAD_RECV_END
};

struct switch_job {
  enum switch_job_type type;
  struct packet *packet;
  int in_port_index;
  int out_port_index;
  char fname_download[100];
  char fname_upload[100];
  int ping_timer;
  int file_upload_dst;
  struct switch_job *next;
};

void switch_main(int switch_id);