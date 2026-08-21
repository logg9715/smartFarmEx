#ifndef EPOLL_LOOP_H
#define EPOLL_LOOP_H

int epoll_loop(int signal_fd);
int ready_inotify(void);
int print_inotify(int fd);

#endif
