/*
    nameServer.h
*/
#pragma once

#include "constants.h"

#define MAX_NAME_LEN MAX_RESPONSE_LEN

// Forward declarations
struct Net_port;
struct Job;

void init_nametable(char **nametable);

void name_server_main(int switch_id);