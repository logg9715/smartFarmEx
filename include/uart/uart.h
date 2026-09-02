#ifndef UART_H
#define UART_H

#include "frame/frame_stm32.h"
#include "uart/uart_parser.h"
#include "epoll_loop.h"

typedef struct 
{
    frame_parser_t frame_parser;
    frame_stm32_t frame_stm32;
} epoll_uart_ctx_t;

int start_uart();
int read_uart_stm32(epoll_event_handle_t *);

#endif