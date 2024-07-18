#ifndef SIMPLE_LOG_H
#define SIMPLE_LOG_H

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
    FILE *handle;     // log out file handle
    int line;         // line number
    int level;        // log level
} log_Event;

/*user custom file name structure*/
typedef struct
{
    int index;
} file_pattern;

typedef void (*log_LogFn)(log_Event *ev);
typedef void (*log_LockFn)(bool lock, void *udata);

typedef struct
{
    int index_cb;
    log_LogFn fn;
    FILE *handle;
    int level;
    char name[32];
} Callback;

int rotating_file_sink(Callback *cb);

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

void log_log(int level, const char *file, int line, const char *fmt, ...);
const char *log_level_string(int level);
int sm_log_init(const char *out_path, const char *out_name_pattern, int level,
                long max_file_size, int max_files, bool quiet);
void sm_log_uninit();

#endif