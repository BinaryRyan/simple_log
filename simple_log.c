#include "log.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main()
{
    sm_log_init(NULL, "simple_log", 1, 10, 10, true);

    for (int i = 0; i < 9999999; i++)
    {
        usleep(100);
        log_debug("This is the parent process (PID: %d):%d", getpid(), i);
    }

    sm_log_uninit();
    return 0;
}
