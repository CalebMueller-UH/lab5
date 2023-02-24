/*
  manager.h
*/

#pragma once

#include "common.h"

/*
 *  The next two structs are ports used to transfer commands
 *  and replies between the manager and hosts
 */

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

void display_host(struct Man_port_at_man *list,
                  struct Man_port_at_man *curr_host);

void change_host(struct Man_port_at_man *list,
                 struct Man_port_at_man **curr_host);

void display_host(struct Man_port_at_man *list,
                  struct Man_port_at_man *curr_host);

void display_host_state(struct Man_port_at_man *curr_host);

void set_host_dir(struct Man_port_at_man *curr_host);

void man_print_command_prompt(int curr_host);

char man_get_user_cmd(int curr_host);

/*
 * Main loop for the manager.
 */
void man_main();
