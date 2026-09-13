#include <sys/timerfd.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>

#include "stat_logger.h"
#include "util/util_log.h"
#include "epoll_loop.h"
#include "config.h"

static proc_stat_t g_max_val = {.oppend_fd_cnt=0, .using_data_bytes=0, .using_memory_bytes=0};

// 프로세스 상태 로그 기록 타이머
int ready_stat_log_timer(int *fd_out)
{
    int fd = -1;
    fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if(fd < 0)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "ready_stat_log_timer > timerfd_create");
        goto err;
    }
    struct itimerspec its =
    {
        .it_interval = { .tv_sec = STAT_LOGGER_INTERVAL },   // 인터벌
        .it_value    = { .tv_sec = 0, .tv_nsec=1},   // 초 뒤에 발화
    };
    if (timerfd_settime(fd, 0, &its, NULL) < 0)   // 타이머 시작
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "ready_stat_log_timer > timerfd_settime");
        goto err;
    }

    *fd_out = fd;
    // 설정 초기화
    return 0;
err:
    *fd_out = -1;
    if(fd >= 0)
        close(fd);
    return EP_CRITICAL_ERR;
}

int handle_stat_log_timer(epoll_event_handle_t *handle)
{
    uint64_t exp;
    read(handle->fd, &exp, sizeof(exp));
    int res;
    proc_stat_t self;
    res = get_fd_stat(&self);
    if(res == -1)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "handle_stat_log_timer > get_fd_stat");
        return EP_CRITICAL_ERR;
    }
    res = get_memory_stat(&self);
    if(res == -1)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "handle_stat_log_timer > get_memory_stat");
        return EP_CRITICAL_ERR;
    }
    char buff[1024] ={0};
    snprintf(buff, sizeof(buff), "using memeory:%"PRIu64"/%"PRIu64"(kb) | using data memory:%"PRIu64"/%"PRIu64"(kb) | oppend fd:%d/%d",
        self.using_memory_bytes/1024, g_max_val.using_memory_bytes/1024,
        self.using_data_bytes/1024, g_max_val.using_data_bytes/1024,
        self.oppend_fd_cnt, g_max_val.oppend_fd_cnt);
    stat_log_write(LL_ANYWAY, 0, buff);
    return 0;
}

// 열린 fd갯수 검사
int get_fd_stat(proc_stat_t *proc_stat)
{
    DIR *d = opendir("/proc/self/fd");
    int cnt = 0;
    struct dirent *e;

    if(!d)
        return -1;
    
    while ((e=readdir(d)) != NULL)
    {
        cnt++;
    }
    closedir(d);
    proc_stat->oppend_fd_cnt = cnt - 3; // /. /.. opendir 갯수 뺴기 
    if(g_max_val.oppend_fd_cnt < cnt - 3)
        g_max_val.oppend_fd_cnt = cnt - 3;
    return 0;
}

int get_memory_stat(proc_stat_t *proc_stat)
{
    int res;
    unsigned long  resident = 0, data = 0;
    FILE *statm = fopen("/proc/self/statm", "r");
    if(statm == NULL)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "get_memory_stat > fopen");
        return -1;
    }

    res = fscanf(statm, "%*ld %ld %*ld %*ld %*ld %ld %*ld", &resident, &data);
    if(res != 2)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "get_memory_stat > fscanf");
        return -1;
    }
    fclose(statm);
    
    uint64_t page_size = sysconf(_SC_PAGE_SIZE);
    proc_stat->using_memory_bytes = (uint64_t)resident * page_size;
    if(g_max_val.using_memory_bytes < (uint64_t)resident * page_size)
        g_max_val.using_memory_bytes = (uint64_t)resident * page_size;
    proc_stat->using_data_bytes = (uint64_t)data * page_size;
    if(g_max_val.using_data_bytes < (uint64_t)data * page_size)
        g_max_val.using_data_bytes = (uint64_t)data * page_size;
    return 0;
}