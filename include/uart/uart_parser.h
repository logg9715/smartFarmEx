#ifndef UART_PARSER_H
#define UART_PARSER_H

#include "frame/frame_sht30.h"

typedef struct 
{
    uint8_t buff[128];
    size_t len;
} frame_parser_t;


int parser_parse(frame_parser_t *p, frame_sht30_t *sht30);
int parser_feed(frame_parser_t *p, frame_sht30_t *sht30, const uint8_t *data, size_t len);

#endif