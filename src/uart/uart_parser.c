#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include "uart/uart_parser.h"
#include "frame/frame_stm32.h"

int parser_parse(frame_parser_t *p, frame_stm32_t *stm32)
{
    size_t stx = 0, len = 0, frame_len = 0;

    while(stx < p->len)
    {
        if(p->buff[stx] == 0x02) break;
        stx++;
    }

    if(stx == p->len)
    {
        p->len = 0;
        return -1;        
    }

    if(stx > 0)    // stx앞으로 당기기
    {
        p->len -= stx;
        memmove(&(p->buff[0]), &(p->buff[stx]), p->len);
        stx = 0;
    }

    if(stx + 2 > p->len) 
        return -1;

    len = p->buff[stx+1];
    frame_len = len + 5;
    if(stx + frame_len > p->len)
        return -1;

    // # 소비하는 영역
    memcpy(stm32, &(p->buff[stx + 3]), len);
    
    // 정리 처리
    if(stx + frame_len == p->len)
    {
        p->len = 0;
    } 
    else 
    {
        p->len -= frame_len;
        memmove(&(p->buff[0]), &(p->buff[frame_len]), p->len);
    }
    
    return 0;
}

int parser_feed(frame_parser_t *p, frame_stm32_t *stm32, const uint8_t *data, size_t len)
{
    if (sizeof(p->buff) - p->len < len)
    {
        p->len = 0;
    }
    memcpy(&p->buff[p->len], data, len);
    p->len += len;

    int res = parser_parse(p, stm32);    //  반환값 일부러 안씀 나중에 처리예정
    return res;
}
