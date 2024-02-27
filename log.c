/*
 * author: Ryan
 * time: 2024/2/22 16:31:45
 * TODO: 实现异步队列日志提交
 */

#include "log.h"
#include "stdbool.h"
#include <bits/pthreadtypes.h>
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_CALLBACKS 32
#define MAX_FILE_PATH 255
#define MAX_FILE_NAME 128
#define MAX_CB_NAME 32
#define MAX_FILE_AB_PATH MAX_FILE_NAME + MAX_FILE_NAME + 32
#define CTL_CB_NAME "ctl_file"
#define DST_CB_BANE "file"

#define container_of(ptr, type, member)                                        \
  ({                                                                           \
    void *__mptr = (void *)(ptr);                                              \
    ((type *)(__mptr - offsetof(type, member)));                               \
  })

typedef struct {
  int index_cb;
  int index_file;
  log_LogFn fn;
  FILE *handle;
  int level;
  char name[MAX_CB_NAME];
} Callback;

struct {
  void *lock_handle;                 // lock HANDLE
  log_LockFn lock;                   // lock method
  int level;                         // log level
  Callback callbacks[MAX_CALLBACKS]; // callbacks
  int max_files;                     // max files
  long max_file_size;                // max file size (default is 2GB)
  char out_path[MAX_FILE_PATH];      // file output path
  char out_name[MAX_FILE_NAME]; // the file output name is 'out_name-%d.log'
} L;

static const char *level_strings[] = {"TRACE", "DEBUG", "INFO",
                                      "WARN",  "ERROR", "FATAL"};

char *calc_filename(int index) {
  static char filename[MAX_FILE_AB_PATH] = {0};
  snprintf(filename, sizeof(filename), "%s/%s-%d.log", L.out_path, L.out_name,
           index);

  return filename;
}

#ifdef LOG_USE_COLOR
static const char *level_colors[] = {"\x1b[94m", "\x1b[36m", "\x1b[32m",
                                     "\x1b[33m", "\x1b[31m", "\x1b[35m"};
#endif

static char log_buffer[65535];
static void stdout_callback(log_Event *ev) {
  Callback *cb = container_of(ev->handle, Callback, handle);

  char buf[16];
  buf[strftime(buf, sizeof(buf), "%H:%M:%S", ev->time)] = '\0';
#ifdef LOG_USE_COLOR
  fprintf(ev->udata, "%s %s%-5s\x1b[0m \x1b[90m%s:%d:\x1b[0m ", buf,
          level_colors[ev->level], level_strings[ev->level], ev->file,
          ev->line);
#else
  fprintf(cb->handle, "%s %-5s %s:%d: ", buf, level_strings[ev->level],
          ev->file, ev->line);
#endif
  vfprintf(cb->handle, ev->fmt, ev->ap);
  fprintf(cb->handle, "\n");
  fflush(cb->handle);
}

static void file_callback(log_Event *ev) {
  Callback *cb = container_of(ev->handle, Callback, handle);

  printf("write to file handle:%p\n", cb->handle);
  char buf[64];
  buf[strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", ev->time)] = '\0';
  fprintf(cb->handle, "%s %-5s %s:%d: ", buf, level_strings[ev->level],
          ev->file, ev->line);
  vfprintf(cb->handle, ev->fmt, ev->ap);
  fprintf(cb->handle, "\n");
  fflush(cb->handle);
}

int log_add_callback(log_LogFn fn, FILE *handle, int level, const char *cb_name,
                     file_pattern *fpt) {
  if (!cb_name) {
    errno = EINVAL;
    perror("cb name cannot be null");
  }

  for (int i = 0; i < MAX_CALLBACKS; i++) {
    if (!L.callbacks[i].fn) {
      u_int8_t cb_name_len = strlen(cb_name) + 1;
      if (cb_name_len > MAX_CB_NAME) {
        return -1;
        errno = ENAMETOOLONG;
        perror("cb name is too long");
      }

      if (fpt) {
        L.callbacks[i].index_file = fpt->index;
      }

      L.callbacks[i].fn = fn;
      L.callbacks[i].index_cb = i;
      L.callbacks[i].level = level;
      L.callbacks[i].handle = handle;
      memcpy(L.callbacks[i].name, cb_name, cb_name_len);

      return i;
    }
  }

  return -1;
}

const char *log_level_string(int level) { return level_strings[level]; }

void log_set_lock(log_LockFn fn, void *udata) {
  L.lock = fn;
  L.lock_handle = udata;
}

/*We agree that callbacks with a file_index greater than 0 are considered
 * working callbacks, otherwise they are control callbacks*/
Callback *find_cb_(const char *name) {
  for (int i = 0; i < MAX_CALLBACKS; i++) {
    if (strcmp(L.callbacks[i].name, name) == 0) {
      return &L.callbacks[i];
    }
  }

  return NULL;
}

int log_remove_callback(int index) {
  if (L.callbacks[index].fn) {
    L.callbacks[index] = (Callback){
        index, -1, NULL, NULL, 0, "",
    };
    return index;
  }

  return -1;
}

FILE *reopen() {
  char *name = calc_filename(1);
  FILE *handle = NULL;

  if (!(handle = fopen(name, "wb"))) {
    perror("cannot open file in wb mode");
    return NULL;
  }

  fclose(handle);

  if (!(handle = fopen(name, "ab+"))) {
    perror("cannot open file in ab mode");
    return NULL;
  }

  printf("reopen:%p\n", handle);
  return handle;
}

// Rotate files:
// log.txt -> log.1.txt
// log.1.txt -> log.2.txt
// log.2.txt -> log.3.txt
// log.3.txt -> delete
int rotating_file_sink(Callback *cb) {
  for (int i = L.max_files; i > 0; --i) {
    char src[MAX_FILE_AB_PATH] = {0};
    char *_src = calc_filename(i - 1);
    memcpy(src, _src, strlen(_src));
    if (access(src, F_OK) == -1) {
      continue;
    }

    char *target = calc_filename(i);
    if (access(target, F_OK) == 0) {
      unlink(target);
    }

    printf("rename from:%s to:%s\n", src, target);
    if (rename(src, target) == -1) {
      perror("rename error");
      return -1;
    }
  }

  fclose(cb->handle);

  Callback *dst_cb = find_cb_(DST_CB_BANE);
  if (!(dst_cb->handle = cb->handle = reopen())) {
    perror("reopen error");
    return -1;
  }

  printf("reset cb handle to:%p\n", cb->handle);
  return 0;
}

void log_control_callback(log_Event *ev) {
  Callback *cb = container_of(ev->handle, Callback, handle);

  long size = ftell(cb->handle);
  printf("tell size:%lu from %p\n", size, cb->handle);
  if (size > 0) // L.max_file_size
  {
    rotating_file_sink(cb);
  }
}

int log_init_default(const char *out_path, const char *out_name_pattern,
                     int level, long max_file_size, int max_files, bool async,
                     bool quiet) {
  if (out_path) {
    memcpy(L.out_path, out_path, strlen(out_path) + 1);

    if (strlen(out_path) > MAX_FILE_PATH - 1) {
      errno = ENAMETOOLONG;
      perror("out_path is too long");
      return -1;
    }
  }

  if (!out_name_pattern) {
    errno = EINVAL;
    perror("invalid out_name_pattern");
    return -1;
  }

  if (strlen(out_name_pattern) > MAX_FILE_NAME - 1) {
    errno = ENAMETOOLONG;
    perror("out_name_pattern is too long");
    return -1;
  }

  memcpy(L.out_name, out_name_pattern, strlen(out_name_pattern) + 1);

  if (level < 0 || level > LOG_MAX_LEVEL) {
    errno = EINVAL;
    perror("invalid log level");
    return -1;
  }

  L.level = level;
  L.max_files = max_files;
  L.max_file_size = max_file_size;

  if (strlen(L.out_path) > 0) {
    struct stat info;
    if (stat(out_path, &info) != 0 || !S_ISDIR(info.st_mode)) {
      perror("invalid output path or output path does not exits:");
      return -1;
    }
  } else {
    // default to .
    memcpy(L.out_path, ".", 2);
  }

  FILE *file = reopen();
  if (file < 0) {
    perror("cannot open log file:");
    return -1;
  }

  // TODO:generate callback name
  log_add_callback(log_control_callback, file, level, CTL_CB_NAME, NULL);
  if (!quiet)
    log_add_callback(stdout_callback, stderr, L.level, "stderr", NULL);
  log_add_callback(file_callback, file, L.level, DST_CB_BANE,
                   &(file_pattern){1});
  return 0;
}

static inline void init_event(log_Event *ev, void *handle) {
  if (!ev->time) {
    time_t t = time(NULL);
    ev->time = localtime(&t);
  }

  ev->handle = handle;
}

void log_log(int level, const char *file, int line, const char *fmt, ...) {
  log_Event ev = {
      .fmt = fmt,
      .file = file,
      .line = line,
      .level = level,
  };

  if (L.lock_handle)
    L.lock(true, L.lock_handle);

  for (int i = 0; i < MAX_CALLBACKS && L.callbacks[i].fn; i++) {
    Callback *cb = &L.callbacks[i];
    if (level >= cb->level) {
      init_event(&ev, &(cb->handle));
      va_start(ev.ap, fmt);
      cb->fn(&ev);
      va_end(ev.ap);
    }
  }

  if (L.lock_handle)
    L.lock(false, L.lock_handle);
}
