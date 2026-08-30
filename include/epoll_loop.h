#ifndef EPOLL_LOOP_H
#define EPOLL_LOOP_H

/*
    epoll 이벤트 처리용 함수 
    fd에 감시하는 대상 fd, func에 이벤트 처리 함수, ctx는 기타 데이터 처리용
*/
typedef struct epoll_event_handle epoll_event_handle_t;
struct epoll_event_handle
{
    int fd;
    int (*func)(epoll_event_handle_t *self);
    void *ctx;
};

int epoll_loop(const int signal_fd);
int finish_loop(epoll_event_handle_t *handle_t);

#endif
