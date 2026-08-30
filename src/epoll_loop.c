#include <sys/epoll.h>
#include <sys/inotify.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "epoll_loop.h"
#include "sig_handler.h"
#include "util/util_time.h"
#include "util/util_log.h"

#define TIMEOUT 5000
#define MAX_EVENTS 10

static int g_is_working = 1;

int epoll_loop(const int signal_fd)
{
    int fd;
    struct epoll_event ep_event;
    struct epoll_event event_list[MAX_EVENTS];
    int inotify_fd;
    int ret;
    int event_list_index;
    char timestamp[40];

    // epoll 생성
    if ((fd = epoll_create1(0)) == -1) 
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "epoll_loop > epoll_create1");
        goto error_std; 
    }

    // epoll에 이벤트 등록
    if((inotify_fd = ready_inotify()) == -1) 
    { 
        goto error_fd; 
    }
    
    // 1 -- inotify 이벤트
    epoll_event_handle_t handle_inotify = 
    {
        .fd = inotify_fd,
        .func = print_inotify,
    };
    ep_event.events = EPOLLIN;
    ep_event.data.ptr = &handle_inotify;
    if(epoll_ctl(fd, EPOLL_CTL_ADD, handle_inotify.fd, &ep_event) == -1) 
    { 
        log_write(LL_ERROR, LC_SHOW_PERROR, "epoll_loop > epoll_ctl");
        goto error_fd_inotify_fd;
    }

    // 2 -- 시그널 이벤트
    epoll_event_handle_t handle_signal = 
    {
        .fd = signal_fd,
        .func = finish_loop,
    };
    ep_event.events = EPOLLIN;
    ep_event.data.ptr = &handle_signal;
    if(epoll_ctl(fd, EPOLL_CTL_ADD, handle_signal.fd, &ep_event) == -1) 
    { 
        log_write(LL_ERROR, LC_SHOW_PERROR, "epoll_loop > epoll_ctl");
        goto error_fd_inotify_fd; 
    }

    while (g_is_working)
    {
        log_write(LL_DEBUG, LC_NOT_WRITE | LC_SHOW_PRINTF, "waiting...\n");

        // sigaction을 사용할 경우 while과 epoll_wait의 블로킹이 되기 이전에 시그널이 들어온 경우,
        // 블로킹이 종료되고 루프가 돌아야 시그널이 반응함
        // sigfd로 핸들러를 만들고, 시그널 이벤트도 epoll에게 등록(구독)시키면 그 전에 들어온 시그널도 바로 감지할 수 있다. 
        ret = epoll_wait(fd, event_list, sizeof(event_list)/sizeof(struct epoll_event), TIMEOUT);
        if (ret == -1)	// CASE : epoll_wait 에러 발생한 경우
		{
            if(errno == EINTR)  // ctrl_z 시그널 중단처리
            {
                log_write(LL_INFO, 0, "SIGTSTP");
                log_flush();
                continue;
            }
            log_write(LL_ERROR, LC_SHOW_PERROR, "epoll_loop > epoll_wait");
            goto error_fd_inotify_fd;
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
                    goto error_fd_inotify_fd;
            }
            // ==========================================================================================
        }
        // DEBUG ----------------------------------------------
        get_now_time(timestamp, sizeof(timestamp));
        char loop_end_buff[128] = {0};
        snprintf(loop_end_buff, sizeof(loop_end_buff), "=========== %s ============", timestamp);
        log_write(LL_DEBUG, LC_NOT_WRITE | LC_SHOW_PRINTF, loop_end_buff);
        // ----------------------------------------------------
    }
    log_write(LL_DEBUG, LC_NOT_WRITE | LC_SHOW_PRINTF, "closing process...\n");

    close(inotify_fd);
    close(fd);
    return 0;

error_fd_inotify_fd : 
    close(inotify_fd);
error_fd : 
    close(fd);
error_std : 
    return -1;
}

int finish_loop(epoll_event_handle_t *handle_t)
{
    int res = check_signal_term((int)handle_t->fd);
    if(res == 1) 
    {
        g_is_working = 0; 
        printf("detected SIGTERM\n");
    }
    else if (res == -1) 
        return -1;
    
    return 0;
}

int ready_inotify(void)
{
    int fd;
    int wd;

    fd = inotify_init();
    if(fd == -1) { 
        log_write(LL_ERROR, LC_SHOW_PERROR, "ready_inotify > inotify_init");
        goto error_std; 
    }

    wd = inotify_add_watch(fd, ".", IN_CREATE | IN_DELETE);
    if (wd == -1) 
    { 
        log_write(LL_ERROR, LC_SHOW_PERROR, "ready_inotify > inotify_add_watch");
        goto error_fd; 
    }
    return fd;

error_fd : 
    close(fd);
error_std : 
    return -1;
}

int print_inotify(epoll_event_handle_t *handle_t)
{
    int fd = handle_t->fd;
    struct inotify_event *event;
    char buff[4096];
    ssize_t ret;
    size_t event_size;

    ret = read(fd, buff, sizeof(buff));
    if(ret == -1) 
    { 
        log_write(LL_ERROR, LC_SHOW_PERROR, "print_inotify > read_inotify");
        return -1; 
    }

    event = (struct inotify_event *)buff;
    while(ret > 0)
    {
        if(event->mask & IN_CREATE)
            printf("File Created : %s\n",event->name);
        if(event->mask & IN_DELETE)
            printf("File Deleted : %s\n",event->name);
        event_size = sizeof(struct inotify_event) + event->len;
        ret -= event_size;
        event = (struct inotify_event *)((char *)event + event_size);
    }
    return 0;
}

