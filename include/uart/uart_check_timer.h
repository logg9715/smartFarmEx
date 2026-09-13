#ifndef UART_CHECK_TIMER_H
#define UART_CHECK_TIMER_H

#include "util/util_time.h"
#include "config.h"
#include "epoll_loop.h"

#define UART_CONN_OK 1
#define UART_CONN_TIMOUT 0

int handle_uart_check_timer(epoll_event_handle_t *);
int check_last_uart(void);
void update_uart_check_timer(void);
int ready_uart_timout_timer(int *);
int get_uart_stat(void);

#endif