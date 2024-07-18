#include "log.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdatomic.h>

#define LOG_DEBUG_TIME_COUNT 100000
#define CPU_CORE 8
volatile atomic_ulong count = ATOMIC_VAR_INIT(CPU_CORE);

void* test_log(void* data)
{
    struct timespec start_time, end_time;
    double total_time = 0.0;
    double max_time = 0.0;
    double min_time = 0.0;

    pthread_t thid = pthread_self();
    for (int i = 0; i < LOG_DEBUG_TIME_COUNT; i++)
    {
        clock_gettime(CLOCK_MONOTONIC, &start_time);

        log_debug("th id (%d):%d", thid, i);

        clock_gettime(CLOCK_MONOTONIC, &end_time);

        double elapsed_time = (end_time.tv_sec - start_time.tv_sec) * 1.0e9 +
                              (end_time.tv_nsec - start_time.tv_nsec);
        total_time += elapsed_time;

        if (i == 0)
        {
            min_time = elapsed_time;
            max_time = elapsed_time;
        }
        else
        {
            if (elapsed_time < min_time) min_time = elapsed_time;
            if (elapsed_time > max_time) max_time = elapsed_time;
        }
    }

    double average_time = total_time / LOG_DEBUG_TIME_COUNT * 1000000.0;
    printf("Max Time: %lf million seconds\n", max_time / 1000000.0);
    printf("Min Time: %lf million seconds\n", min_time / 1000000.0);
    printf("Average Time: %lf million seconds\n", average_time);
    atomic_fetch_sub(&count, 1);
    return NULL;
}

int main()
{
    sm_log_init(NULL, "simple_log", 1, 10, 10, true);

    pthread_t threads[CPU_CORE];

    // proc num
    for (int i = 0; i < CPU_CORE; i++)
    {
        pthread_create(&threads[i], NULL, test_log, NULL);
        pthread_detach(threads[i]);
    }

    while (atomic_load(&count) > 0)
    {
        sleep(1);
    }

    sm_log_uninit();
    return 0;
}
