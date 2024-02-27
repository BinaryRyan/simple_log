#include "log.h"
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>

#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

void p_lock(bool lock, void *udata) {
  if (lock) {
    pthread_mutex_lock(udata);
    sleep(1);
    return;
  }
  pthread_mutex_unlock(udata);
  // lock ? pthread_mutex_lock(udata) : pthread_mutex_unlock(udata);
}

int main() {
  size_t shared_size = sizeof(pthread_mutex_t);
  key_t key = 0x9911;

  int shmid = shmget(key, shared_size, IPC_CREAT | 0666);
  if (shmid == -1) {
    perror("cannot get shared memory:");
    return -1;
  }

  pthread_mutex_t *mutex = (pthread_mutex_t *)shmat(shmid, NULL, 0);
  pthread_mutexattr_t mutex_attr;
  pthread_mutexattr_init(&mutex_attr);

  pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);

#ifdef __USE_XOPEN2K
  pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST);
#endif

  pthread_mutex_init(mutex, &mutex_attr);

  int log_level = 0; // trace
  log_init_default(NULL, "simple_log", 1, 1024, 10, false, false);
  log_set_lock(p_lock, mutex);

  log_debug("this is parent process first time print information.");
  pid_t pid = fork();
  switch (pid) {
  case -1:
    perror("fork failed");
    return -1;
  case 0:
    for (int i = 0; i < 3; i++) {
      log_debug("This is the child process (PID: %d):%d", getpid(), i);
    }
    break;
  default:
    for (int i = 0; i < 3; i++) {
      log_debug("This is the parent process (PID: %d):%d", getpid(), i);
    }
    break;
  }

  wait(NULL);

  pthread_mutex_destroy(mutex);
  pthread_mutexattr_destroy(&mutex_attr);

  /* Detach and Remove shared memory segment*/
  shmdt(mutex);
  shmctl(shmid, IPC_RMID, NULL);

  return 0;
}