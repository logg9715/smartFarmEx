#ifndef EPOLL_H
#define EPOLL_H
#include <sys/epoll.h>
#endif

#ifndef INOTIFY_H
#define INOTIFY_H
#include <sys/inotify.h>
#endif

#ifndef ERROR_H
#define ERROR_H
#include <error.h>
#endif

#ifndef STRING_H
#define STRING_H
#include <string.h>
#endif

#ifndef STDIO_H
#define STDIO_H
#include <stdio.h>
#endif

#include "epoll_loop.h"

int epoll_loop(void)
{
    const int TIMEOUT = 5000;

    int fd;
    struct epoll_event ep_event;
    struct epoll_event event_list[10];
    int inotify_fd;
    int ret;
    int event_list_index;

    // epoll 생성
    if ((fd = epoll_create1(0)) == -1)
    {
        perror("error_epoll_create\n");
        goto error_std;
    }

    if((inotify_fd = ready_inotify()) == -1)
    {
        perror("error_ready_inotity\n");
        goto error_std;
    }

    // epoll에 이벤트 등록
    memset(&ep_event, 0, sizeof(ep_event));
    ep_event.events = EPOLLIN;
    ep_event.data.fd = inotify_fd;
    if(epoll_ctl(fd, EPOLL_CTL_ADD, inotify_fd, &ep_event) == -1)
    {
        perror("error_epoll_ctl\n");
        goto error_fd_inotify_fd;
    }

    while (1)
    {
        printf("waiting...\n");

        memset(&event_list, 0, sizeof(event_list));
        ret = epoll_wait(fd, &event_list, sizeof(event_list)/sizeof(struct epoll_event), TIMEOUT);

        if(ret == -1)	// CASE : epoll_wait 에러 발생한 경우
		{
			perror("error epoll_wait : -1");
            goto error_fd_inotify_fd;
		}
		else if (ret == 0)	// CASE : timeout이 발생한 경우
		{
			printf("timeout\n");
		}
        else if (ret > 0)
        {
            // ==========================================================================================
            for(event_list_index = 0; event_list_index < ret; event_list_index++)
            {
                if(event_list[event_list_index].data.fd == inotify_fd)
                {
                    print_inotify(event_list[event_list_index].data.fd);
                }
            }
            // ==========================================================================================
        }
        printf("=======================\n");
    }


    close(inotify_fd);
    close(fd);
    return 0;

error_fd_inotify_fd : 
    close(inotify_fd);
error_std : 
    close(fd);
    return -1;
}


int ready_inotify(void)
{
    int fd;
    int wd;

    if((fd = inotify_init()) == -1) 
    {
        perror("error_inotify_init\n");
        goto error_std;
    }

    if ((wd = inotify_add_watch(fd, ".", IN_CREATE | IN_DELETE)) == -1)
    {
        perror("error_inotify_add_watch\n");
        goto error_std;
    }

    return fd;

error_std : 
    close(fd);
    return -1;
}

int print_inotify(int fd)
{
    struct inotify_event *event;
    char buff[4096];
    ssize_t ret;
    size_t event_size;

    ret = read(fd, buff, sizeof(buff));
    if(ret == -1)
    {
        perror("Error read_inotify read()\n");
        return -1;
    }

    event = (struct inotify_event *)buff;
    while(ret > 0)
    {
        if(event->mask & IN_CREATE)
        {
            printf("File Created : %s\n",event->name);
        }
        if(event->mask & IN_DELETE)
        {
            printf("File Deleted : %s\n",event->name);
        }
        event_size = sizeof(struct inotify_event) + event->len;
        ret -= event_size;
        event = (struct inotify_event *)((char *)event + event_size);
    }
    return 0;
}