/*
semaphore.c
*/

#include "semaphore.h"

#define TIMEOUT_MS 2000

binary_semaphore console_print_access = {.value = 1};

void sem_wait(binary_semaphore *semaphore) {
  time_t start_time = time(NULL);
  while (semaphore->value <= 0) {
    time_t current_time = time(NULL);
    if (current_time - start_time >= TIMEOUT_MS / 1000) {
      sem_signal(semaphore);
      start_time = current_time;
    }
  }
  semaphore->value = 0;
  printf("sem_wait: %d\n", semaphore->value);
}

void sem_signal(binary_semaphore *semaphore) {
  semaphore->value = 1;
  printf("sem_signal: %d\n", semaphore->value);
}