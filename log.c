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

#define container_of(ptr, type, member)                                        \
  ({                                                                           \
    void *__mptr = (void *)(ptr);                                              \
    ((type *)(__mptr - offsetof(type, member)));                               \
  })

typedef struct {
  int index_cb;
  int index_file;
  log_LogFn fn;
  int fd;
  int level;
  char name[MAX_CB_NAME];
} Callback;

typedef struct {
  void *lock_handle;                 // lock HANDLE
  log_LockFn lock;                   // lock method
  int level;                         // log level
  Callback callbacks[MAX_CALLBACKS]; // callbacks
  int max_files;                     // max files
  long max_file_size;                // max file size (default is 2GB)
  char out_path[MAX_FILE_PATH];      // file output path
  char out_name[MAX_FILE_NAME]; // the file output name is 'out_name-%d.log'
} Log;

static Log *L = NULL;
static const char *level_strings[] = {"TRACE", "DEBUG", "INFO",
                                      "WARN",  "ERROR", "FATAL"};

int open_index_file(int index) {
  char filename[MAX_FILE_NAME + MAX_FILE_NAME + 32] = {0};
  snprintf(filename, sizeof(filename), "%s/%s-%d.log", L->out_path, L->out_name,
           index);

  // Use open function to open the file and return the file descriptor
  return open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
}

#ifdef LOG_USE_COLOR
static const char *level_colors[] = {"\x1b[94m", "\x1b[36m", "\x1b[32m",
                                     "\x1b[33m", "\x1b[31m", "\x1b[35m"};
#endif

static char log_buffer[65535];
static void stdout_callback(log_Event *ev) {
  char buf[16];
  buf[strftime(buf, sizeof(buf), "%H:%M:%S", ev->time)] = '\0';

#ifdef LOG_USE_COLOR
  snprintf(log_buffer, sizeof(log_buffer),
           "%s %s%-5s\x1b[0m \x1b[90m%s:%d:\x1b[0m ", buf,
           level_colors[ev->level], level_strings[ev->level], ev->file,
           ev->line);
#else
  snprintf(log_buffer, sizeof(log_buffer), "%s %-5s %s:%d: ", buf,
           level_strings[ev->level], ev->file, ev->line);
#endif

  vsnprintf(log_buffer + strlen(log_buffer),
            sizeof(log_buffer) - strlen(log_buffer), ev->fmt, ev->ap);
  write(ev->fd, log_buffer, strlen(log_buffer));
  write(ev->fd, "\n", 1);
  fsync(ev->fd);
}

static void file_callback(log_Event *ev) {
  char buf[64];

  buf[strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", ev->time)] = '\0';
  snprintf(log_buffer, sizeof(log_buffer), "%s %-5s %s:%d: ", buf,
           level_strings[ev->level], ev->file, ev->line);
  vsnprintf(log_buffer + strlen(log_buffer),
            sizeof(log_buffer) - strlen(log_buffer), ev->fmt, ev->ap);
  write(ev->fd, log_buffer, strlen(log_buffer));
  write(ev->fd, "\n", 1);
  fsync(ev->fd);
}

int log_add_callback(log_LogFn fn, int fd, int level, const char *cb_name,
                     file_pattern *fpt) {
  if (!cb_name) {
    errno = EINVAL;
    perror("cb name cannot be null");
  }

  for (int i = 0; i < MAX_CALLBACKS; i++) {
    if (!L->callbacks[i].fn) {
      u_int8_t cb_name_len = strlen(cb_name) + 1;
      if (cb_name_len > MAX_CB_NAME) {
        return -1;
        errno = ENAMETOOLONG;
        perror("cb name is too long");
      }

      if (fpt) {
        L->callbacks[i].index_file = fpt->index;
      }

      L->callbacks[i].fn = fn;
      L->callbacks[i].index_cb = i;
      L->callbacks[i].level = level;
      L->callbacks[i].fd = fd;
      memcpy(L->callbacks[i].name, cb_name, cb_name_len);

      return i;
    }
  }

  return -1;
}

const char *log_level_string(int level) { return level_strings[level]; }

void log_set_lock(log_LockFn fn, void *udata) {
  L->lock = fn;
  L->lock_handle = udata;
}

/*We agree that callbacks with a file_index greater than 0 are considered
 * working callbacks, otherwise they are control callbacks*/
Callback *find_cb_from_udata(const char *name) {
  for (int i = 0; i < MAX_CALLBACKS; i++) {
    if (strcmp(L->callbacks[i].name, name) == 0) {
      return &L->callbacks[i];
    }
  }

  return NULL;
}

int log_remove_callback(int index) {
  if (L->callbacks[index].fn) {
    L->callbacks[index] = (Callback){
        index, -1, NULL, -1, 0, "",
    };
    return index;
  }

  return -1;
}

void log_control_callback(log_Event *ev) {
  int fd = ev->fd;
  int size = 0;

  struct stat st;
  if (fstat(fd, &st) != 0) {
    return perror("cannot fstat file");
  }

  size = st.st_size;

  if (size > 0) // TODO: test from L->max_file_size
  {
    // 找到关联的callback
    Callback *cb_ = find_cb_from_udata("file");
    if (!cb_) {
      return;
    }

    printf("remove origin file:%d %d\n", cb_->index_file, fd);

    /*close origin file handle*/
    close(fd);
    int next_f_index = ++cb_->index_file;

    /*remove this file callback and add a new one*/
    log_remove_callback(cb_->index_cb);

    fd = open_index_file(next_f_index);
    if (fd < 0) {
      perror("cannot open log file:");
      return;
    }

    printf("create new file:%d %d\n", next_f_index, fd);

    log_add_callback(file_callback, fd, L->level, "file",
                     &(file_pattern){next_f_index});
  }
}

int log_init_default(const char *out_path, const char *out_name_pattern,
                     int level, long max_file_size, int max_files, bool async,
                     bool quiet) {
  // TODO:replace with file path
  key_t key_l = 0x9914;
  int size_l = sizeof(Log);
  int shmid_l = shmget(key_l, size_l, IPC_CREAT | 0666);
  if (shmid_l == -1) {
    perror("cannot get shared memory");
    return -1;
  }

  L = (Log *)shmat(shmid_l, NULL, 0);
  memset(L, 0, size_l); // clean the memory

  if (out_path) {
    memcpy(L->out_path, out_path, strlen(out_path) + 1);

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

  memcpy(L->out_name, out_name_pattern, strlen(out_name_pattern) + 1);

  if (level < 0 || level > LOG_MAX_LEVEL) {
    errno = EINVAL;
    perror("invalid log level");
    return -1;
  }

  L->level = level;
  L->max_files = max_files;
  L->max_file_size = max_file_size;

  if (strlen(L->out_path) > 0) {
    struct stat info;
    if (stat(out_path, &info) != 0 || !S_ISDIR(info.st_mode)) {
      perror("invalid output path or output path does not exits:");
      return -1;
    }
  } else {
    // default to .
    memcpy(L->out_path, ".", 2);
  }

  int file = open_index_file(1);
  if (file < 0) {
    perror("cannot open log file:");
    return -1;
  }

  /*TODO:generate callback name*/
  if (!quiet)
    log_add_callback(stdout_callback, STDOUT_FILENO, L->level, "stderr", NULL);
  log_add_callback(file_callback, file, L->level, "file", &(file_pattern){1});
  log_add_callback(log_control_callback, file, level, "ctl_file", NULL);
  return 0;
}

static inline void init_event(log_Event *ev, int fd) {
  if (!ev->time) {
    time_t t = time(NULL);
    ev->time = localtime(&t);
  }

  ev->fd = fd;
}

void log_log(int level, const char *file, int line, const char *fmt, ...) {
  log_Event ev = {
      .fmt = fmt,
      .file = file,
      .line = line,
      .level = level,
  };

  if (L->lock_handle)
    L->lock(true, L->lock_handle);

  for (int i = 0; i < MAX_CALLBACKS && L->callbacks[i].fn; i++) {
    Callback *cb = &L->callbacks[i];
    if (level >= cb->level) {
      init_event(&ev, cb->fd);
      va_start(ev.ap, fmt);
      cb->fn(&ev);
      va_end(ev.ap);
    }
  }

  if (L->lock_handle)
    L->lock(false, L->lock_handle);
}
