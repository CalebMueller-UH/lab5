/*
  main.c
*/

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include "color.h"
#include "constants.h"
#include "host.h"
#include "manager.h"
#include "net.h"
#include "switch.h"

#define BCAST_ADDR 100
#define STRING_MAX 100

void main(int argc, char **argv) {
  pid_t pid; /* Process id */
  int k = 0;
  int status;
  struct Net_node *node_list;
  struct Net_node *p_node;

  /*
   * Read network configuration file, which specifies
   *   - nodes, creates a list of nodes
   *   - links, creates/implements the links, e.g., using pipes or sockets
   */

  if (argc > 1) {
    // ./net367 called with argument -> net_init with provided arg
    if (net_init(argv[1]) != 0) {
      fprintf(stderr, "Error initializing network at net_init(%s)\n", argv[1]);
      return;
    }
  } else {
    // ./net367 not called with argument -> net_init with NULL
    if (net_init(NULL) != 0) {
      fprintf(stderr, "Error initializing network at net_init(NULL)\n");
      return;
    }
  }

  node_list = net_get_node_list(); /* Returns the list of nodes */

  /* Create nodes, which are child processes */
  for (p_node = node_list; p_node != NULL; p_node = p_node->next) {
    pid = fork();
    if (pid == -1) {
      fprintf(stderr, "Error: main.c: node_list fork() failed\n");
      return;
    } else if (pid == 0) { /* The child process, which is a node  */
      if (p_node->type == HOST) {
        /* Execute host routine */
        host_main(p_node->id);
      } else if (p_node->type = SWITCH) {
        /* Execute switch routine */
        switch_main(p_node->id);
      }
      return;
    }
  }

  /*
   * Parent process: Execute manager routine.
   */
  man_main();

  /*
   * We reach here if the user quits the manager.
   * The following will terminate all the children processes.
   */

  kill(0, SIGKILL); /* Kill all processes */
}
