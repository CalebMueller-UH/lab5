/*
  manager.h
*/

#pragma once

#include "constants.h"
/*
manager.c
*/

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "color.h"
#include "host.h"
#include "main.h"
#include "manager.h"
#include "net.h"
#include "semaphore.h"

struct Man_port_at_host { /* Port located at the man */
  int host_id;
  int send_fd;
  int recv_fd;
  struct Man_port_at_host *next;
};

struct Man_port_at_man { /* Port located at the host */
  int host_id;
  int send_fd;
  int recv_fd;
  struct Man_port_at_man *next;
};

void man_print_command_prompt(int curr_host);

/* Get the user command */
char man_get_user_cmd(int curr_host);

/* Change the current host */
void change_host(struct Man_port_at_man *list,
                 struct Man_port_at_man **curr_host);

void display_host(struct Man_port_at_man *list,
                  struct Man_port_at_man *curr_host);

void display_host_state(struct Man_port_at_man *curr_host);

void set_host_dir(struct Man_port_at_man *curr_host);

void ping(struct Man_port_at_man *curr_host);

int file_upload(struct Man_port_at_man *curr_host);

int file_download(struct Man_port_at_man *curr_host);

int isValidDirectory(const char *path);

int isValidFile(const char *path);

/*
Function reads a manager port command using read(), removes the 1st non-space
character and stores it in c. The rest of the message is copied to msg, with a
null terminator added at the end. The function returns the number of bytes read.
*/
int get_man_command(struct Man_port_at_host *port, char msg[], char *c);

/* Send back state of the host to the manager as a text message */
void reply_display_host_state(struct Man_port_at_host *port,
                              char hostDirectory[], int dir_valid, int host_id);

// Main loop of the manager
void man_main();
