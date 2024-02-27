#include "log.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main()
{
    sm_log_init(NULL, "simple_log", 1, 1, 10, false, true);

    log_debug("this is parent process first time print information.");
    pid_t pid = fork();
    switch (pid)
    {
        case -1: perror("fork failed"); return -1;
        case 0:
            for (int i = 0; i < 90000; i++)
            {
                log_debug("This is the child process (PID: %d):%d", getpid(),
                          i);
            }
            break;
        default:
            for (int i = 0; i < 90000; i++)
            {
                log_debug("This is the parent process (PID: %d):%d", getpid(),
                          i);
            }
            break;
    }

    wait(NULL);
    sm_log_uninit();
    return 0;
}
