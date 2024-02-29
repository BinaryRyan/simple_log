这是一个进程安全级别的日志库,更改自[这里](https://github.com/rxi/log.c),另外借鉴了sdplog的rotate方式。请注意，这个库异步信号不安全！！！
### 构建[Build]

```bash
make
```

### 特性[Feature]

1. 进程/线程 安全

2. 可配置单个日志文件大小

3. 日志文件滚动

### 案例[Example]

```C
#include "log.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

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
```
