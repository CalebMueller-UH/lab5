// net.h

#pragma once

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#define _GNU_SOURCE
#include <fcntl.h>

int net_init();

struct man_port_at_man *net_get_man_ports_at_man_list();
struct man_port_at_host *net_get_host_port(int host_id);

struct net_node *net_get_node_list();
struct net_port *net_get_port_list(int host_id);
