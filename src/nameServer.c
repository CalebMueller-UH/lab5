/*
    nameServer.h
*/

#include "nameServer.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "color.h"
#include "constants.h"
#include "job.h"
#include "net.h"
#include "packet.h"

#define MAX_NUM_NAMES 255

// create static dns number (100)

// Used for searchRoutingTableForValidID when port is unknown
#define UNKNOWN -1

// Searches the routing table for a matching valid TableEntry matching id
// Returns routing table index of valid id, or -1 if unsuccessful
//  **Note that routing table index is the port
// int searchRoutingTableForValidID(struct TableEntry **rt, int id, int port)
// {
// #ifdef DEBUG
//   char portMsg[100];
//   if (port == UNKNOWN)
//   {
//     snprintf(portMsg, 100, "unknown port");
//   }
//   else
//   {
//     snprintf(portMsg, 100, "port%d", port);
//   }
//   colorPrint(BLUE, "Searching routing table for host%d on %s\n", id, portMsg);
// #endif

//   if (port == UNKNOWN)
//   {
//     // Port was not given...
//     // Scan through the indices (ports) of the routing table
//     for (int i = 0; i < MAX_NUM_ROUTES; i++)
//     {
//       // If there are entries for that port
//       if (rt[i] != NULL)
//       {
//         struct TableEntry *t = rt[i];
//         while (t != NULL)
//         {
//           if (t->id == id)
//           {
// #ifdef DEBUG
//             colorPrint(BLUE, "\tFound host%d on port%d\n", id, i);
// #endif
//             return i;
//           }
//           t = t->next;
//         }
//       }
//     }
//   }
//   else
//   {
//     // Port was specified...
//     // Look in the routing table at that port for a matching id
//     if (rt[port] != NULL)
//     {
//       struct TableEntry *t = rt[port];
//       while (t != NULL)
//       {
//         if (t->id == id)
//         {
//           return port;
//         }
//         t = t->next;
//       }
//     }
//   }

// #ifdef DEBUG
//   colorPrint(BLUE, "\tUnable to find host%d in routing table\n", id);
// #endif

//   return -1;
// }

// void addToRoutingTable(struct TableEntry **rt, int id, int port)
// {
// #ifdef DEBUG
//   colorPrint(BLUE, "\tAdding host%d to routing table on port%d\n", id, port);
// #endif
//   struct TableEntry *newEntry =
//       malloc(sizeof(struct TableEntry)); // Memory allocation added here
//   newEntry->id = id;
//   newEntry->next = rt[port];
//   rt[port] = newEntry; // Assignment changed to address of new entry instead
//                        // of its value
// }

// /*
// This function takes a job struct , a table entry, an array of Net_port
// pointers and the size of the array as parameters. It then iterates through the
// port_array and sends the job's packet to each port in the array, except for the
// port that corresponds to the job->packet->src
// */
// void broadcastToAllButSender(struct Job *job, struct TableEntry **rt,
//                              struct Net_port **port_array,
//                              int port_array_size)
// {
//   int senderPort = searchRoutingTableForValidID(rt, job->packet->src, UNKNOWN);

//   if (senderPort < 0)
//   {
//     fprintf(
//         stderr,
//         "Error: broadcastToAllButSender unable to find value for senderPort\n");
//     return;
//   }

//   // Iterate through all connected ports and broadcast packet
//   // to all except senderPort
//   for (int i = 0; i < port_array_size; i++)
//   {
//     if (i != senderPort)
//     {
//       packet_send(port_array[i], job->packet);
//     }
//   }
// }

void name_server_main(int name_id)
{
  ////// Initialize state of switch ////// //entry point

  // Initialize node_port_array
  struct Net_port *node_port_list;
  struct Net_port **node_port_array; // Array of pointers to node ports
  int node_port_array_size;          // Number of node ports
  node_port_list = net_get_port_list(name_id);

  /*  Count the number of network link ports */
  node_port_array_size = 0;
  for (struct Net_port *p = node_port_list; p != NULL; p = p->next)
  {
    node_port_array_size++;
  }

  /* Create memory space for the array */
  node_port_array = (struct Net_port **)malloc(node_port_array_size *
                                               sizeof(struct Net_port *));

  /* Load ports into the array */
  {
    struct Net_port *p = node_port_list;
    for (int portNum = 0; portNum < node_port_array_size; portNum++)
    {
      node_port_array[portNum] = p;
      p = p->next;
    }
  }

  /* Initialize the job queue */
  struct JobQueue name_q;
  job_queue_init(&name_q);

  ////// Initialize Name Table //////
  char nameTable[MAX_NUM_NAMES][MAX_NAME_LEN];
  for (int i = 0; i < MAX_NUM_NAMES; i++)
  {
    memset(nameTable[i], 0, MAX_NAME_LEN);
  }
  // struct TableEntry **routingTable = (struct TableEntry **)malloc(
  //     sizeof(struct TableEntry *) * MAX_NUM_ROUTES);
  // for (int i = 0; i < MAX_NUM_ROUTES; i++) {
  //   routingTable[i] = NULL;
  // }

  while (1)
  {
    ////////////////////////////////////////////////////////////////////////////
    ////////////////////////////// PACKET HANDLER //////////////////////////////

    for (int portNum = 0; portNum < node_port_array_size; portNum++)
    {
      struct Packet *received_packet =
          (struct Packet *)malloc(sizeof(struct Packet));
      int n = packet_recv(node_port_array[portNum], received_packet);
      if (n > 0)
      {
#ifdef DEBUG
        colorPrint(
            YELLOW,
            "DEBUG: id:%d switch_main: Switch received packet on port:%d "
            "src:%d dst:%d\n",
            name_id, portNum, received_packet->src, received_packet->dst);
#endif

        struct Job *nsJob = job_create_empty();
        nsJob->packet = received_packet;

        // switch statement that differeniates from registration, and query
        switch (received_packet->type)
        {
        case PKT_DNS_REGISTRATION:
          nsJob->type = JOB_DNS_REGISTER;
        case PKT_DNS_QUERY:
          nsJob->type = JOB_DNS_QUERY;
        }
        nsJob->state = JOB_PENDING_STATE;
        job_enqueue(name_id, &name_q, nsJob);
      }
      else
      {
        // Nothing to receive on port, so discard malloc'd packet
        free(received_packet);
        received_packet = NULL;
      }
    }

    ////////////////////////////// PACKET HANDLER //////////////////////////////
    ////////////////////////////////////////////////////////////////////////////
    // -------------------------------------------------------------------------
    ////////////////////////////////////////////////////////////////////////////
    //////////////////////////////// JOB HANDLER ///////////////////////////////

    if (job_queue_length(&name_q) > 0)
    {
      /* Get a new job from the job queue */
      struct Job *job_from_queue = job_dequeue(name_id, &name_q);

      //////////// EXECUTE FETCHED JOB ////////////
      switch (job_from_queue->type)
      {
      case JOB_DNS_REGISTER:
        break;
      case JOB_DNS_QUERY:
        break;
      }

      free(job_from_queue->packet);
      job_from_queue->packet = NULL;
      free(job_from_queue);
      job_from_queue = NULL;
    }

    //////////////////////////////// JOB HANDLER ///////////////////////////////
    ////////////////////////////////////////////////////////////////////////////

    /* The host goes to sleep for 10 ms */
    usleep(LOOP_SLEEP_TIME_US);
  } /* End of while loop */

  /* Free dynamically allocated memory */
  for (int i = 0; i < node_port_array_size; i++)
  {
    free(node_port_array[i]);
    node_port_array[i] = NULL;
  }
  free(node_port_array);
  node_port_array = NULL;
  free(nameTable);
  //  nameTable = NULL;
}
