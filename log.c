/*
 * author: Ryan
 * time: 2024/2/22 16:31:45
 * TODO: 实现异步队列日志提交
 */
#include "log.h"
#include <bits/pthreadtypes.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdatomic.h>
#include <unistd.h>

#define MAX_CALLBACKS 32
#define MAX_FILE_PATH 255
#define MAX_FILE_NAME 128

#define container_of(ptr, type, member)              \
    ({                                               \
        void *__mptr = (void *)(ptr);                \
        ((type *)(__mptr - offsetof(type, member))); \
    })

typedef struct
{
    int index_cb;
    int index_file;
    log_LogFn fn;
    void *udata;
    int level;
} Callback;

static struct
{
    void *lock_handle;                 // lock HANDLE
    log_LockFn lock;                   // lock method
    int level;                         // log level
    bool quiet;                        // output mode
    Callback callbacks[MAX_CALLBACKS]; // callbacks
    int max_files;                     // max files
    long max_file_size;                // max file size (default is 2GB)
    char out_path[MAX_FILE_PATH];      // file output path
    char out_name[MAX_FILE_NAME]; // the file output name is 'out_name-%d.log'
} L = {0};

static const char *level_strings[] = {"TRACE", "DEBUG", "INFO",
                                      "WARN",  "ERROR", "FATAL"};

FILE *open_index_file(int index)
{
    char filename[MAX_FILE_NAME + MAX_FILE_NAME + 32] = {0};
    snprintf(filename, sizeof(filename), "%s/%s-%d.log", L.out_path, L.out_name,
             index);

    return fopen(filename, "a"); // append is thread safe
}

#ifdef LOG_USE_COLOR
static const char *level_colors[] = {"\x1b[94m", "\x1b[36m", "\x1b[32m",
                                     "\x1b[33m", "\x1b[31m", "\x1b[35m"};
#endif

static void stdout_callback(log_Event *ev)
{
    char buf[16];
    buf[strftime(buf, sizeof(buf), "%H:%M:%S", ev->time)] = '\0';
#ifdef LOG_USE_COLOR
    fprintf(cb->udata, "%s %s%-5s\x1b[0m \x1b[90m%s:%d:\x1b[0m ", buf,
            level_colors[ev->level], level_strings[ev->level], ev->file,
            ev->line);
#else
    fprintf(ev->udata, "%s %-5s %s:%d: ", buf, level_strings[ev->level],
            ev->file, ev->line);
#endif
    vfprintf(ev->udata, ev->fmt, ev->ap);
    fprintf(ev->udata, "\n");
    fflush(ev->udata);
}

static void file_callback(log_Event *ev)
{
    Callback *cb = container_of(ev->udata, Callback, udata);
    if (!cb)
    {
        perror("cannot retrieve callback from udata:");
        return;
    }

    char buf[64];
    buf[strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", ev->time)] = '\0';
    fprintf(cb->udata, "%s %-5s %s:%d: ", buf, level_strings[ev->level],
            ev->file, ev->line);
    vfprintf(cb->udata, ev->fmt, ev->ap);
    fprintf(cb->udata, "\n");
    fflush(cb->udata);
}

static void lock(void)
{
    if (L.lock)
    {
        L.lock(true, L.lock_handle);
    }
}

static void unlock(void)
{
    if (L.lock)
    {
        L.lock(false, L.lock_handle);
    }
}

const char *log_level_string(int level) { return level_strings[level]; }

void log_set_lock(log_LockFn fn, void *udata)
{
    L.lock = fn;
    L.lock_handle = udata;
}

void log_set_level(int level) { L.level = level; }

void log_set_quiet(bool enable) { L.quiet = enable; }

Callback *find_cb_from_udata(void *udata)
{
    for (int i = 0; i < MAX_CALLBACKS; i++)
    {
        if (L.callbacks[i].udata == udata)
        {
            return &L.callbacks[i];
        }
    }

    return NULL;
}

int log_add_callback(log_LogFn fn, void *udata, int level, int f_index)
{
    Callback *fcb = NULL;
    if ((fcb = find_cb_from_udata(udata)) && f_index > 0 && fcb->index_file > 0)
    {
        errno = EEXIST;
        perror("adding duplicate elements is not allowed.");
        return -1;
    }

    for (int i = 0; i < MAX_CALLBACKS; i++)
    {
        if (!L.callbacks[i].fn)
        {
            L.callbacks[i] = (Callback){i, f_index, fn, udata, level};
            return i;
        }
    }

    return -1;
}

int log_remove_callback(int index)
{
    if (L.callbacks[index].fn)
    {
        L.callbacks[index] = (Callback){index, -1, NULL, NULL, 0};
        return index;
    }

    return -1;
}

/*always begain with number 1*/
int log_add_fp(FILE *fp, int level)
{
    return log_add_callback(file_callback, fp, level, 1);
}

void log_control_callback(log_Event *ev)
{
    Callback *cb = container_of(ev->udata, Callback, udata);
    if (!cb || (cb != &L.callbacks[cb->index_cb]))
    {
        perror("cannot retrieve callback from udata:");
        return;
    }

    FILE *file = NULL;
    if ((file = cb->udata) == stderr || !file)
    {
        return;
    }

    // we do not handle log write events
    if (cb->index_file > 0)
    {
        return;
    }

    // checkout log file size
    long pos = ftell(file);
    fseek(file, 0, SEEK_END);

    long size = ftell(file);
    fseek(file, pos, SEEK_SET); // move back

    if (size > 0) // TODO: test from L.max_file_size
    {
        // 找到关联的callback
        Callback *cb_ = find_cb_from_udata(cb->udata);
        if (!cb_)
        {
            return;
        }

        /*close origin file handle*/
        fclose(file);
        file = NULL;

        int next_f_index = ++cb_->index_file;

        /*remove this file callback and add a new one*/
        log_remove_callback(cb_->index_cb);

        file = open_index_file(next_f_index);
        if (!file)
        {
            perror("cannot open log file:");
            return;
        }

        /*update control udata*/
        cb->udata = file;
        log_add_callback(file_callback, file, L.level, next_f_index);
    }
}

int log_init_default(const char *out_path, const char *out_name_pattern,
                     int level, long max_file_size, int max_files, bool async,
                     bool quiet)
{
    if (out_path)
    {
        memcpy(L.out_path, out_path, strlen(out_path) + 1);

        if (strlen(out_path) > MAX_FILE_PATH - 1)
        {
            errno = ENAMETOOLONG;
            perror("out_path is too long:");
            return -1;
        }
    }

    if (!out_name_pattern)
    {
        errno = EINVAL;
        perror("invalid out_name_pattern:");
        return -1;
    }

    if (strlen(out_name_pattern) > MAX_FILE_NAME - 1)
    {
        errno = ENAMETOOLONG;
        perror("out_name_pattern is too long:");
        return -1;
    }

    memcpy(L.out_name, out_name_pattern, strlen(out_name_pattern) + 1);

    if (level < 0 || level > LOG_MAX_LEVEL)
    {
        perror("invalid log level:");
        return -1;
    }

    L.quiet = quiet;
    L.level = level;
    L.max_files = max_files;
    L.max_file_size = max_file_size;

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

    FILE *file = open_index_file(1);
    if (!file)
    {
        perror("cannot open log file:");
        return -1;
    }

    log_add_fp(file, level); // write to file
    log_add_callback(log_control_callback, file, level, -1);
    return 0;
}

static void init_event(log_Event *ev, void *udata)
{
    if (!ev->time)
    {
        time_t t = time(NULL);
        ev->time = localtime(&t);
    }
    ev->udata = udata;
}

void log_log(int level, const char *file, int line, const char *fmt, ...)
{
    log_Event ev = {
        .fmt = fmt,
        .file = file,
        .line = line,
        .level = level,
    };

    lock();

    if (!L.quiet && level >= L.level)
    {
        init_event(&ev, stderr);
        va_start(ev.ap, fmt);
        stdout_callback(&ev);
        va_end(ev.ap);
    }

    for (int i = 0; i < MAX_CALLBACKS && L.callbacks[i].fn; i++)
    {
        Callback *cb = &L.callbacks[i];
        if (level >= cb->level)
        {
            init_event(&ev, &(cb->udata));
            va_start(ev.ap, fmt);
            cb->fn(&ev);
            va_end(ev.ap);
        }
    }

    unlock();
}