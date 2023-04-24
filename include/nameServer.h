/*
    nameServer.h
*/
#pragma once

#include "constants.h"

// Forward declarations
struct Net_port;
struct Job;

void init_nametable(char **nametable);

void name_server_main(int switch_id);