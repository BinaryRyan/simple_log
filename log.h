#ifndef VD_LOG_H
#define VD_LOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#define LOG_VERSION "0.1.0"

typedef struct
{
    va_list ap;
    const char *fmt;  // fmt
    const char *file; // call file name
    struct tm *time;  // log time
    void *udata;      // log out file handle
    int line;         // line number
    int level;        // log level
} log_Event;

typedef struct
{
    FILE *file;
    char *name;
} file_handle;

typedef void (*log_LogFn)(log_Event *ev);
typedef void (*log_LockFn)(bool lock, void *udata);

enum LOG_LEVEL
{
    LOG_TRACE,
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL,
    LOG_MAX_LEVEL = LOG_FATAL
};

#define log_trace(...) log_log(LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define log_debug(...) log_log(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define log_info(...) log_log(LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define log_warn(...) log_log(LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define log_error(...) log_log(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define log_fatal(...) log_log(LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)

const char *log_level_string(int level);
void log_set_lock(log_LockFn fn, void *udata);
void log_set_level(int level);
void log_set_quiet(bool enable);

// return callback index
int log_add_callback(log_LogFn fn, void *udata, int level, int index);
// remove callback with index
int log_remove_callback(int index);
// add file log to
int log_add_fp(FILE *fp, int level);

void log_log(int level, const char *file, int line, const char *fmt, ...);
void log_control_callback(log_Event *ev);
int log_init_default(const char *out_path, const char *out_name_pattern,
                     int level, long max_file_size, int max_files, bool async,
                     bool quiet);

#endif