/*
manager.c
*/

#include "manager.h"

#include <dirent.h>
#include <stdio.h>
#include <unistd.h>

#include "color.h"
#include "constants.h"
#include "host.h"
#include "net.h"

void man_print_command_prompt(int curr_host) {
  usleep(200000);
  /* Display command options */
  colorPrint(BOLD_CYAN, "\nCommands (Current host ID = %d):\n", curr_host);
  colorPrint(CYAN, "   (s) Display host's state\n");
  colorPrint(CYAN, "   (m) Set host's main directory\n");
  colorPrint(CYAN, "   (h) Display all hosts\n");
  colorPrint(CYAN, "   (c) Change host\n");
  colorPrint(CYAN, "   (p) Ping a host\n");
  colorPrint(CYAN, "   (u) Upload a file to a host\n");
  colorPrint(CYAN, "   (d) Download a file from a host\n");
  colorPrint(CYAN, "   (q) Quit\n");
  colorPrint(CYAN, "   Enter Command: ");
}

/* Get the user command */
char man_get_user_cmd(int curr_host) {
  char cmd;
  while (1) {
    do {
      cmd = getchar();
    } while (cmd == ' ' || cmd == '\n'); /* get rid of junk from stdin */

    /* Ensure that the command is valid */
    switch (cmd) {
      case 's':
      case 'm':
      case 'h':
      case 'c':
      case 'p':
      case 'u':
      case 'd':
      case 'q':
        return cmd;
      default:
        colorPrint(BOLD_RED, "%c is not a valid command\n", cmd);
        man_print_command_prompt(curr_host);
    }
  }
}

/* Change the current host */
void change_host(struct Man_port_at_man *list,
                 struct Man_port_at_man **curr_host) {
  int new_host_id;

  // display_host(list, *curr_host);
  colorPrint(BOLD_CYAN, "Enter new host: ");
  scanf("%d", &new_host_id);
  printf("\n");

  /* Find the port of the new host, and then set it as the curr_host */
  struct Man_port_at_man *p;
  for (p = list; p != NULL; p = p->next) {
    if (p->host_id == new_host_id) {
      *curr_host = p;
      break;
    }
  }
}

/* Display the hosts on the consosle */
void display_host(struct Man_port_at_man *list,
                  struct Man_port_at_man *curr_host) {
  struct Man_port_at_man *p;

  colorPrint(CYAN, "\nHost list:\n");
  for (p = list; p != NULL; p = p->next) {
    colorPrint(CYAN, "   Host id = %d ", p->host_id);
    if (p->host_id == curr_host->host_id) {
      colorPrint(GREEN, "<- connected");
    }
    printf("\n");
  }
}

/*
 * Send command to the host for it's state.  The command
 * is a single character 's'
 *
 * Wait for reply from host, which should be the host's state.
 * Then display on the console.
 */
void display_host_state(struct Man_port_at_man *curr_host) {
  char msg[MAX_MSG_LENGTH];
  char reply[MAX_MSG_LENGTH];
  char dir[MAX_FILENAME_LENGTH];
  int host_id;
  int n;

  msg[0] = 's';
  write(curr_host->send_fd, msg, 1);

  n = 0;
  while (n <= 0) {
    usleep(LOOP_SLEEP_TIME_US);
    n = read(curr_host->recv_fd, reply, MAX_MSG_LENGTH);
  }
  reply[n] = '\0';
  sscanf(reply, "%s %d", dir, &host_id);
  colorPrint(CYAN, "Host %d state: \n", host_id);
  colorPrint(CYAN, "    Directory = %s\n", dir);
}

void set_host_dir(struct Man_port_at_man *curr_host) {
  char name[MAX_FILENAME_LENGTH];
  char msg[MAX_MSG_LENGTH];
  int n;

  colorPrint(CYAN, "Enter directory name: ");
  scanf("%s", name);
  n = snprintf(msg, MAX_MSG_LENGTH, "m %s", name);
  write(curr_host->send_fd, msg, n);
}

/*
 * Command host to send a ping to the host with id "curr_host"
 *
 * User is queried for the id of the host to ping.
 *
 * A command message is sent to the current host.
 *    The message starrts with 'p' followed by the id
 *    of the host to ping.
 *
 * Wiat for a reply
 */

void ping(struct Man_port_at_man *curr_host) {
  char msg[MAX_MSG_LENGTH];
  char reply[MAX_MSG_LENGTH];
  int host_to_ping;
  int n;

  colorPrint(CYAN, "Enter id of host to ping: ");
  scanf("%d", &host_to_ping);
  n = snprintf(msg, MAX_MSG_LENGTH, "p %d", host_to_ping);

  write(curr_host->send_fd, msg, n);

  n = 0;
  while (n <= 0) {
    usleep(LOOP_SLEEP_TIME_US);
    n = read(curr_host->recv_fd, reply, MAX_MSG_LENGTH);
  }
  reply[n] = '\0';
  printf("%s\n", reply);
}

/*
 * Command host to send a file to another host.
 *
 * User is queried for the
 *    - name of the file to transfer;
 *        the file is in the current directory 'dir'
 *    - id of the host to ping.
 *
 * A command message is sent to the current host.
 *    The message starrts with 'u' followed by the
 *    -  id of the destination host
 *    -  name of file to transfer
 */
int file_upload(struct Man_port_at_man *curr_host) {
  int n;
  int host_id;
  char name[MAX_FILENAME_LENGTH];
  char msg[MAX_MSG_LENGTH];

  colorPrint(CYAN, "Enter file name to upload: ");
  scanf("%s", name);
  colorPrint(CYAN, "Enter host id of destination:  ");
  scanf("%d", &host_id);

  n = snprintf(msg, MAX_MSG_LENGTH, "u %d %s", host_id, name);
  write(curr_host->send_fd, msg, n);
  usleep(LOOP_SLEEP_TIME_US);

  char reply[MAX_MSG_LENGTH];
  n = 0;
  while (n <= 0) {
    usleep(LOOP_SLEEP_TIME_US);
    n = read(curr_host->recv_fd, reply, MAX_MSG_LENGTH);
  }

  reply[n] = '\0';
  printf("%s\n", reply);
}

/*
 * Command host to download a file to another host.
 *
 * User is queried for the
 *    - name of the file to transfer;
 *        the file is in the current directory 'dir'
 *    - id of the host to ping.
 *
 * A command message is sent to the current host.
 *    The message starrts with 'u' followed by the
 *    -  id of the destination host
 *    -  name of file to transfer
 */
int file_download(struct Man_port_at_man *curr_host) {
  int n;
  int host_id;
  char name[MAX_FILENAME_LENGTH];
  char msg[MAX_MSG_LENGTH];

  colorPrint(CYAN, "Enter file name to download: ");
  scanf("%s", name);
  colorPrint(CYAN, "Enter host id that has this file:  ");
  scanf("%d", &host_id);

  n = snprintf(msg, MAX_MSG_LENGTH, "d %d %s", host_id, name);
  write(curr_host->send_fd, msg, n);
  usleep(LOOP_SLEEP_TIME_US);

  char reply[MAX_MSG_LENGTH];
  n = 0;
  while (n <= 0) {
    usleep(LOOP_SLEEP_TIME_US);
    n = read(curr_host->recv_fd, reply, MAX_MSG_LENGTH);
  }

  reply[n] = '\0';
  printf("%s\n", reply);
}

int isValidDirectory(const char *path) {
  DIR *hostDirectory = opendir(path);
  if (hostDirectory) {
    closedir(hostDirectory);
    return 1;
  } else {
    return 0;
  }
}

int fileExists(const char *path) {
  if (access(path, F_OK) != -1) {
    // File exists
    return 1;
  } else {
    // File does not exist or cannot be accessed for some reason
    return 0;
  }
}

/*
Function reads a manager port command using read(), removes the 1st non-space
character and stores it in c. The rest of the message is copied to msg, with a
null terminator added at the end. The function returns the number of bytes read.
*/
int get_man_command(struct Man_port_at_host *port, char msg[], char *c) {
  int n;
  int i;
  int portNum;

  n = read(port->recv_fd, msg, MAX_MSG_LENGTH); /* Get command from manager */
  if (n > 0) { /* Remove the first char from "msg" */
    for (i = 0; msg[i] == ' ' && i < n; i++)
      ;
    *c = msg[i];
    i++;
    for (; msg[i] == ' ' && i < n; i++)
      ;
    for (portNum = 0; portNum + i < n; portNum++) {
      msg[portNum] = msg[portNum + i];
    }
    msg[portNum] = '\0';
  }
  return n;
}

/* Send back state of the host to the manager as a text message */
void reply_display_host_state(struct Man_port_at_host *port,
                              char hostDirectory[], int dir_valid,
                              int host_id) {
  int n;
  char reply_msg[MAX_MSG_LENGTH];

  if (isValidDirectory(hostDirectory)) {
    n = snprintf(reply_msg, MAX_MSG_LENGTH, "%s %d", hostDirectory, host_id);
  } else {
    n = snprintf(reply_msg, MAX_MSG_LENGTH, "\033[1;31mNone %d\033[0m",
                 host_id);
  }

  write(port->send_fd, reply_msg, n);
}

/*****************************
 * Main loop of the manager  *
 *****************************/
void man_main() {
  // State
  struct Man_port_at_man *host_list;
  struct Man_port_at_man *curr_host = NULL;

  host_list = net_get_man_ports_at_man_list();
  curr_host = host_list;

  char cmd; /* Command entered by user */

  while (1) {
    man_print_command_prompt(curr_host->host_id);

    /* Get a command from the user */
    cmd = man_get_user_cmd(curr_host->host_id);

    /* Execute the command */
    switch (cmd) {
      case 's': /* Display the current host's state */
        display_host_state(curr_host);
        break;
      case 'm': /* Set host directory */
        set_host_dir(curr_host);
        break;
      case 'h': /* Display all hosts connected to manager */
        display_host(host_list, curr_host);
        break;
      case 'c': /* Change the current host */
        change_host(host_list, &curr_host);
        break;
      case 'p': /* Ping a host from the current host */
        ping(curr_host);
        break;
      case 'u': /* Upload a file from the current host
                   to another host */
        file_upload(curr_host);
        break;
      case 'd': /* Download a file from a host */
        file_download(curr_host);
        break;
      case 'q': /* Quit */
        return;
      default:
        colorPrint(BOLD_RED, "\nInvalid command entered: %c\n\n", cmd);
    }
  }
}
