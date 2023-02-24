/*
semaphore.h
*/

#pragma once

#include <stddef.h>

typedef struct {
  int value;
} binary_semaphore;

extern binary_semaphore console_print_access;

void sem_wait(binary_semaphore *semaphore);
void sem_signal(binary_semaphore *semaphore);

int create_shared_memory(size_t size);
void *attach_shared_memory(int shmid);
void detach_shared_memory(void *ptr);
void destroy_shared_memory(int shmid);