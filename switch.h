/*
    switch.h
*/

#pragma once

#include <stdlib.h>

#include "main.h"
#include "net.h"

// Forward declaration of struct netport to appease the linter
struct netport;

void switch_main(int switch_id);