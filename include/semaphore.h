/*
semaphore.h
*/

#pragma once

#include <stdio.h>
#include <time.h>

typedef struct {
  int value;
} binary_semaphore;

extern binary_semaphore console_print_access;

void sem_wait(binary_semaphore *semaphore);
void sem_signal(binary_semaphore *semaphore);