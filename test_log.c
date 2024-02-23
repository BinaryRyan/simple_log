#include "log.h"
#include <stdbool.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>

static pthread_mutex_t mutex;

void p_lock(bool lock, void *udata)
{
    if (lock)
    {
        pthread_mutex_lock(udata);
        printf("PD:%d lock:%p\n", getpid(), udata);
    }
    else
    {
        pthread_mutex_unlock(udata);
        printf("PD:%d unlock:%p\n", getpid(), udata);
    }
}

int main()
{
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);

    if (pthread_mutex_init(&mutex, &mutex_attr) != 0)
    {
        perror("init pthread mutex error:");
        return -1;
    }

    log_set_lock(p_lock, &mutex);
    int log_level = 0; // trace
    log_init_default(NULL, "simple_log", 1, 1024, 10, false, false);

    log_debug("this is parent process first time print information.");
    pid_t pid = fork();
    switch (pid)
    {
        case -1: perror("fork failed"); return 1;
        case 0:
            for (int i = 0; i < 3; i++)
            {
                log_debug("This is the child process (PID: %d):%d", getpid(),
                          i);
            }
            break;
        default:
            for (int i = 0; i < 3; i++)
            {
                log_debug("This is the parent process (PID: %d):%d", getpid(),
                          i);
            }
            break;
    }

    wait(NULL);
    return 0;
}