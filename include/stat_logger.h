#include "epoll_loop.h"

typedef struct 
{
    int oppend_fd_cnt;
    uint64_t using_memory_bytes;
    uint64_t using_data_bytes;
} proc_stat_t;

int ready_stat_log_timer(int *);
int handle_stat_log_timer(epoll_event_handle_t *);
int get_fd_stat(proc_stat_t *);
int get_memory_stat(proc_stat_t *);