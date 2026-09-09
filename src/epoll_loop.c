#include <sys/epoll.h>
#include <sys/inotify.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "epoll_loop.h"
#include "sig_handler.h"
#include "util/util_time.h"
#include "util/util_log.h"
#include "uart/uart.h"
#include "webserver/web_server.h"
#include "oled/oled.h"

#define TIMEOUT 5000
#define MAX_EVENTS 10

static int g_is_working = 1;
static epoll_event_handle_t *g_ep_event_handler_list[MAX_EVENTS]; // 동적할당된 epoll_event_handle_t들 회수용 배열 
static int g_ep_event_handler_list_cnt = 0;

int epoll_loop(const int signal_fd)
{
    int return_code = -1;
    int epoll_fd = -1;
    int uart_fd = -1;
    int oled_fd = -1;
    int oled_timer_fd = -1;
    int web_listen_fd = -1, web_timer_fd = -1;
    struct epoll_event event_list[MAX_EVENTS];
    int ret;
    int event_list_index;

    // epoll 생성
    if ((epoll_fd = epoll_create1(0)) == -1) 
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "epoll_loop > epoll_create1");
        goto clear; 
    }

    // epoll에 이벤트 등록
    // 1 -- 시그널 이벤트
    if(epoll_add(epoll_fd, signal_fd, finish_loop, NULL, g_ep_event_handler_list, &g_ep_event_handler_list_cnt, MAX_EVENTS) == NULL)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "epoll_loop > epoll_add1");
        goto clear; 
    }
    
    // 2 -- UART stm32 수신 이벤트
    uart_fd = start_uart();
    epoll_uart_ctx_t uart_ctx = {0};
    if(uart_fd == -1)
        goto clear;
    if(epoll_add(epoll_fd, uart_fd, read_uart_stm32, &uart_ctx, g_ep_event_handler_list, &g_ep_event_handler_list_cnt, MAX_EVENTS) == NULL)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "epoll_loop > epoll_add2");
        goto clear; 
    }

    // 3 -- web 서버 이벤트
    ret = ready_webserver(&web_listen_fd, &web_timer_fd);
    if(ret == -1)
        goto clear;
    web_timer_ctx_t timer_ctx = {.epoll_fd = epoll_fd};
    if(epoll_add(epoll_fd, web_timer_fd, read_web_timer, &timer_ctx, g_ep_event_handler_list, &g_ep_event_handler_list_cnt, MAX_EVENTS) == NULL)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "epoll_loop > epoll_add3");
        goto clear;
    }
    web_accept_ctx_t web_accept_ctx = {.epoll_fd = epoll_fd};
    if(epoll_add(epoll_fd, web_listen_fd, web_accept, &web_accept_ctx, g_ep_event_handler_list, &g_ep_event_handler_list_cnt, MAX_EVENTS) == NULL)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "epoll_loop > epoll_add3-1");
        goto clear;
    }

    // 4 -- OLED 타이머 이벤트
    ret = ready_oled(&oled_fd, &oled_timer_fd);
    if(ret == -1)
        goto clear;
    clear_oled_display(oled_fd);
    oled_ctx_t oled_ctx = {.oled_fd = oled_fd};
    if(epoll_add(epoll_fd, oled_timer_fd, oled_handle, &oled_ctx, g_ep_event_handler_list, &g_ep_event_handler_list_cnt, MAX_EVENTS) == NULL)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "epoll_loop > epoll_add4");
        goto clear;
    }

    // wait 루프
    while (g_is_working)
    {
        log_write(LL_DEBUG, LC_NOT_WRITE | LC_SHOW_PRINTF, "waiting...\n");

        // sigaction을 사용할 경우 while과 epoll_wait의 블로킹이 되기 이전에 시그널이 들어온 경우,
        // 블로킹이 종료되고 루프가 돌아야 시그널이 반응함
        // sigfd로 핸들러를 만들고, 시그널 이벤트도 epoll에게 등록(구독)시키면 그 전에 들어온 시그널도 바로 감지할 수 있다. 
        ret = epoll_wait(epoll_fd, event_list, sizeof(event_list)/sizeof(struct epoll_event), TIMEOUT);
        if (ret == -1)	// CASE : epoll_wait 에러 발생한 경우
		{
            if(errno == EINTR)  // ctrl_z 시그널 중단처리
            {
                log_write(LL_INFO, 0, "SIGTSTP");
                log_flush();
                continue;
            }
            log_write(LL_ERROR, LC_SHOW_PERROR, "epoll_loop > epoll_wait");
            goto clear;
		}
		else if (ret == 0)	// CASE : timeout이 발생한 경우
		{
            log_write(LL_DEBUG, LC_NOT_WRITE | LC_SHOW_PRINTF, "timeout\n");
		}
        else if (ret > 0)   // CASE : 이벤트가 감지된 경우
        {
            // ==========================================================================================
            for(event_list_index = 0; event_list_index < ret; event_list_index++)
            {
                epoll_event_handle_t *e = event_list[event_list_index].data.ptr;
                int res = e->func(e);
                if(res == -1)
                    goto clear;
            }
            // ==========================================================================================
        }
        // DEBUG ----------------------------------------------
        char timestamp[TM_BUFF_LEN];
        get_now_time(timestamp, sizeof(timestamp));
        char loop_end_buff[128] = {0};
        snprintf(loop_end_buff, sizeof(loop_end_buff), "=========== %s ============", timestamp);
        log_write(LL_DEBUG, LC_NOT_WRITE | LC_SHOW_PRINTF, loop_end_buff);
        // ----------------------------------------------------
    }
    log_write(LL_DEBUG, LC_NOT_WRITE | LC_SHOW_PRINTF, "closing process...\n");
    return_code = 0;
clear :
    web_conn_close_all(epoll_fd);
    free_handlers(g_ep_event_handler_list, g_ep_event_handler_list_cnt);
    g_ep_event_handler_list_cnt = 0;

    if(uart_fd >= 0) close(uart_fd);
    if(web_listen_fd >= 0) close(web_listen_fd);
    if(web_timer_fd >= 0) close(web_timer_fd);
    if(oled_fd >= 0)
    {
        clear_oled_display(oled_fd);
        close(oled_fd);
    } 
    if(epoll_fd >= 0) close(epoll_fd);
    return return_code;
}

int finish_loop(epoll_event_handle_t *handle)
{
    int res = check_signal_term((int)handle->fd);
    if(res == 1) 
    {
        g_is_working = 0; 
        printf("detected SIGTERM\n");
    }
    else if (res == -1) 
        return -1;
    
    return 0;
}

// epoll에 이벤트 등록
epoll_event_handle_t *epoll_add(int epoll_fd, int event_fd, int (*func)(epoll_event_handle_t *), void *ctx, epoll_event_handle_t *list[], int *list_cnt, const int list_cnt_max)
{
    epoll_event_handle_t *handler = calloc(1, sizeof(*handler));
    if(!handler) 
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "epoll_add > calloc");
        goto err;
    }
    handler->fd = event_fd;
    handler->func = func;
    handler->ctx = ctx;

    struct epoll_event epev = {0};
    epev.events = EPOLLIN;
    epev.data.ptr = handler;
    
    // 회수 공간 확인
    if(*list_cnt >= list_cnt_max)
    {
        log_write(LL_ERROR, LC_SHOW_PRINTF, "epoll_add > too many events\n");
        goto err;
    }

    // ep등록
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_fd, &epev) == -1) 
    { 
        log_write(LL_ERROR, LC_SHOW_PERROR, "epoll_add > epoll_ctl");
        goto err;
    }

    // 회수 목록에 등록
    list[(*list_cnt)++] = handler;
    return handler;
err:
    if(handler)
        free(handler);
    return NULL;
}

// 사용중인 핸들러 free
void free_handlers(epoll_event_handle_t *list[], int list_cnt)
{
    for(int i = 0; i < list_cnt; i++)
        free(list[i]);
}