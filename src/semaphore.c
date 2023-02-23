/*
semaphore.c
*/

#include "semaphore.h"

#define TIMEOUT_MS 200

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
}

void sem_signal(binary_semaphore *semaphore) { semaphore->value = 1; }

int create_shared_memory(size_t size) {
  int shmid = shmget(SEM_KEY, size, IPC_CREAT | 0666);
  if (shmid < 0) {
    perror("shmget");
    exit(1);
  }
  return shmid;
}

void *attach_shared_memory(int shmid) {
  void *ptr = shmat(shmid, NULL, 0);
  if (ptr == (void *)-1) {
    perror("shmat");
    exit(1);
  }
  return ptr;
}

void detach_shared_memory(void *ptr) {
  if (shmdt(ptr) < 0) {
    perror("shmdt");
    exit(1);
  }
}

void destroy_shared_memory(int shmid) {
  if (shmctl(shmid, IPC_RMID, NULL) < 0) {
    perror("shmctl");
    exit(1);
  }
}