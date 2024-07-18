/*
 * author: Ryan
 * time: 2024/2/22 16:31:45
 * TODO: 实现异步队列日志提交
 */
#include "log.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdatomic.h>

#define MAX_CALLBACKS 32
#define MAX_FILE_PATH 256
#define MAX_FILE_NAME 128

#define CTL_CB_NAME "rotate_log_file"
#define DST_CB_BANE "output_log_file"

static const char *level_strings[] = {"TRACE", "DEBUG", "INFO",
                                      "WARN",  "ERROR", "FATAL"};

const char *log_level_string(int level) { return level_strings[level]; }

#define container_of(ptr, type, member) \
    (type *)((char *)(ptr)-offsetof(type, member))

struct
{
    pthread_mutex_t mutex;             // mutex
    int level;                         // log level
    Callback callbacks[MAX_CALLBACKS]; // callbacks
    int ncallbacks;                    // callback count
    int max_files;                     // max files (default is 10)

    atomic_ulong max_file_size;   // max file size (default is 10MB)
    atomic_ulong writed_size;     // writed size
    char out_path[MAX_FILE_PATH]; // file output path
    char out_name[MAX_FILE_NAME]; // the file output name is 'out_name-%d.log'
} L = {0};

void cacl_filename(int index, char *file_path, int size)
{
    memset(file_path, 0, size - 1);
    snprintf(file_path, size - 1, "%s/%s-%d.log", L.out_path, L.out_name,
             index);
}

FILE *reopen(bool clear)
{
    char file_path[MAX_FILE_PATH];
    FILE *handle = NULL;

    cacl_filename(1, file_path, sizeof(file_path));
    if (clear)
    {
        if (!(handle = fopen(file_path, "wb")))
        {
            perror("cannot open file in ab mode");
            return NULL;
        }

        fclose(handle);
    }

    if (!(handle = fopen(file_path, "ab")))
    {
        perror("cannot open file in ab mode");
        return NULL;
    }

    return handle;
}

/*We agree that callbacks with a file_index greater than 0 are considered
 * working callbacks, otherwise they are control callbacks*/
Callback *find_cb_(const char *name)
{
    for (int i = 0; i < MAX_CALLBACKS; i++)
    {
        if (strcmp(L.callbacks[i].name, name) == 0)
        {
            return &L.callbacks[i];
        }
    }

    return NULL;
}

void write_callback(log_Event *ev)
{
    Callback *cb = container_of(ev->handle, Callback, handle);
    unsigned long bytes_written = 0;

    char buf[64] = {0};
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", ev->time);

    bytes_written += fprintf(cb->handle, "%s %-5s %s:%d: ", buf,
                             level_strings[ev->level], ev->file, ev->line);
    bytes_written += vfprintf(cb->handle, ev->fmt, ev->ap);
    bytes_written += fprintf(cb->handle, "\n");

    atomic_fetch_add(&L.writed_size, bytes_written);
    fflush(cb->handle);
}

int log_add_callback(log_LogFn fn, FILE *handle, int level, const char *cb_name)
{
    if (find_cb_(cb_name))
    {
        errno = EEXIST;
        return -1;
    }

    if (!cb_name || strlen(cb_name) > MAX_FILE_NAME - 1)
    {
        errno = EINVAL;
        return -1;
    }

    Callback *cb = &L.callbacks[L.ncallbacks];
    cb->fn = fn;
    cb->index_cb = L.ncallbacks;
    cb->level = level;
    cb->handle = handle;
    strcpy(cb->name, cb_name);
    L.ncallbacks++;

    return 0;
}

// Rotate files:
// log.txt -> log.1.txt
// log.1.txt -> log.2.txt
// log.2.txt -> log.3.txt
// log.3.txt -> delete
int rotating_file_sink(Callback *cb)
{
    char file_path[MAX_FILE_PATH];
    char file_path_dst[MAX_FILE_PATH];
    cacl_filename(L.max_files, file_path, sizeof(file_path));

    if (access(file_path, F_OK) != -1)
    {
        unlink(file_path);
    }

    for (int i = L.max_files; i > 0; --i)
    {
        cacl_filename(i, file_path, sizeof(file_path));
        if (access(file_path, F_OK) == 0)
        {
            cacl_filename(i + 1, file_path_dst, sizeof(file_path));
            rename(file_path, file_path_dst);
        }
    }

    /*reopen file*/
    Callback *dst_cb = find_cb_(DST_CB_BANE);
    fclose(dst_cb->handle);
    dst_cb->handle = reopen(true);
    return 0;
}

void log_control_callback(log_Event *ev)
{
    Callback *cb = container_of(ev->handle, Callback, handle);

    if (L.writed_size > L.max_file_size)
    {
        rotating_file_sink(cb);
        atomic_store(&L.writed_size, 0);
    }
}

int sm_log_init(const char *out_path, const char *out_name_pattern, int level,
                long max_file_size, int max_files, bool quiet)
{
    if (out_path)
    {
        memcpy(L.out_path, out_path, strlen(out_path) + 1);

        if (strlen(out_path) > MAX_FILE_PATH - 1)
        {
            errno = ENAMETOOLONG;
            perror("out_path is too long");
            return -1;
        }
    }

    if (!out_name_pattern)
    {
        errno = EINVAL;
        perror("invalid out_name_pattern");
        return -1;
    }

    if (strlen(out_name_pattern) > MAX_FILE_NAME - 1)
    {
        errno = ENAMETOOLONG;
        perror("out_name_pattern is too long");
        return -1;
    }

    memcpy(L.out_name, out_name_pattern, strlen(out_name_pattern) + 1);

    if (level < 0 || level > LOG_MAX_LEVEL)
    {
        errno = EINVAL;
        perror("invalid log level");
        return -1;
    }

    L.level = level;
    L.max_files = max_files;
    L.max_file_size = ATOMIC_VAR_INIT(max_file_size * 1024 * 1024);

    if (strlen(L.out_path) > 0)
    {
        struct stat info;
        if (stat(out_path, &info) != 0 || !S_ISDIR(info.st_mode))
        {
            perror("invalid output path or output path does not exits:");
            return -1;
        }
    }
    else
    {
        // default to .
        memcpy(L.out_path, ".", 2);
    }

    FILE *file = reopen(true);

    pthread_mutex_init(&L.mutex, NULL);

    if (!quiet)
    {
        log_add_callback(write_callback, stdout, level, "stdout");
    }

    log_add_callback(write_callback, file, level, DST_CB_BANE);
    log_add_callback(log_control_callback, NULL, level, CTL_CB_NAME);
    return 0;
}

void sm_log_uninit()
{
    pthread_mutex_destroy(&L.mutex);

    Callback *dst_cb = find_cb_(DST_CB_BANE);
    fclose(dst_cb->handle);
}

static inline void init_event(log_Event *ev, void *handle)
{
    if (!ev->time)
    {
        time_t t = time(NULL);
        ev->time = localtime(&t);
    }

    ev->handle = (FILE *)handle;
}

void log_log(int level, const char *file, int line, const char *fmt, ...)
{
    log_Event ev = {
        .fmt = fmt,
        .file = file,
        .line = line,
        .level = level,
    };

    pthread_mutex_lock(&L.mutex);
    for (int i = 0; i < MAX_CALLBACKS && L.callbacks[i].fn; i++)
    {
        Callback *cb = &L.callbacks[i];
        if (level >= cb->level)
        {
            init_event(&ev, &(cb->handle));
            va_start(ev.ap, fmt);
            cb->fn(&ev);
            va_end(ev.ap);
        }
    }
    pthread_mutex_unlock(&L.mutex);
}
