#ifndef UART_PARSER_H
#define UART_PARSER_H

#include "frame/frame_stm32.h"

typedef struct 
{
    uint8_t buff[128];
    size_t len;
} frame_parser_t;


int parser_parse(frame_parser_t *, frame_stm32_t *);
int parser_feed(frame_parser_t *, frame_stm32_t *, const uint8_t *, size_t );

#endif