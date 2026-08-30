#ifndef UART_H
#define UART_H

#include "frame/frame_sht30.h"
#include "uart/uart_parser.h"
#include "epoll_loop.h"

typedef struct 
{
    frame_parser_t frame_parser;
    frame_sht30_t frame_sht30;
} epoll_uart_ctx_t;

int start_uart();
int read_uart_sht30(epoll_event_handle_t *);
int read_uart_sht30(epoll_event_handle_t *handle);

#endif