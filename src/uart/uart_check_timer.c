#include <time.h>
#include <sys/timerfd.h>
#include <stdint.h>
#include <unistd.h>

#include "uart/uart_check_timer.h"
#include "util/util_time.h"
#include "config.h"
#include "util/util_log.h"
#include "epoll_loop.h"

static int g_uart_stat = UART_CHECK_TIMEOUT;
static time_t g_last_tm = 0;

int handle_uart_check_timer(epoll_event_handle_t *handle)
{
    uint64_t exp;
    read(handle->fd, &exp, sizeof(exp));   // 타이머 만료 비우기

    int res = check_last_uart();
    g_uart_stat = res;

    return res;
}

void update_uart_check_timer(void)
{
    g_last_tm = get_now_monotime();
}

// distance : 초 단위
int check_last_uart(void)
{
    time_t now_tm = get_now_monotime();

    if(now_tm - g_last_tm > UART_CHECK_TIMEOUT)
        return UART_CONN_TIMOUT;
    else 
        return UART_CONN_OK;
}

int ready_uart_timout_timer(int *fd_out)
{
    int fd = -1;
    fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if(fd < 0)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "ready_oled > timerfd_create");
        goto err;
    }
    struct itimerspec its =
    {
        .it_interval = { .tv_sec = UART_CHECK_INTERVAL },   // 인터벌
        .it_value    = { .tv_sec = UART_CHECK_INTERVAL },   // 초 뒤에 발화
    };
    if (timerfd_settime(fd, 0, &its, NULL) < 0)   // 타이머 시작
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "ready_uart_timout_timer > timerfd_settime");
        goto err;
    }

    g_last_tm = get_now_monotime();
    *fd_out = fd;
    // 설정 초기화
    return 0;
err:
    *fd_out = -1;
    if(fd >= 0)
        close(fd);
    return EP_CRITICAL_ERR;
}

int get_uart_stat(void)
{
    return g_uart_stat;
}