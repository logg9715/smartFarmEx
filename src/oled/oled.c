#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include "oled/font5x7.h"
#include "oled/oled.h"

/*
OLED 초기화 cmd 보내는 함수
*/
int oled_cmd_send(int fd, uint8_t* cmd_list, int len)
{
    if(len > 30) 
    {
        printf("too long cmd_list\n");
        return -2;
    }

    uint8_t buff[31] = {0x00};
    memcpy(&buff[1], &cmd_list[0], sizeof(cmd_list[0]) * len);

    int res = write(fd, buff, sizeof(uint8_t) * (len + 1));
    if(res != len + 1)
    {
        perror("oled_cmd_send > write");
        return -1;
    }
    return 0;
}

/*
화면 픽셀 데이터 보내는 함수
*/
int oled_data_send(int fd, oled_fd_t *oled)
{
    uint8_t buff[1025] = {0x40};
    memcpy(&buff[1], &(oled->buff[0]), sizeof(oled->buff) / sizeof(uint8_t));

    int res = write(fd, buff, sizeof(buff));
    if(res != sizeof(buff))
    {
        perror("oled_data_send > write");
        return -1;
    }
    return 0;
}

/*
화면 픽셀버퍼 픽셀 값 1개 지정
*/
void set_oled_data_buff(oled_fd_t *oled, int x, int y, int on)
{
    int list_idx = (y / 8) * 128 + x;
    int buff_idx = y % 8;
    if(on)
        oled->buff[list_idx] |= (1 << buff_idx);
    else
        oled->buff[list_idx] &= (0 << buff_idx);
}


/*
픽셀버퍼에 문자 하나 표시하느 함수
*/
void set_oled_data_charbuff(oled_fd_t *oled, int x, int y, char c)
{
    if (c < 32 || c > 126)
        c = '?';

    const uint8_t *glyph = font5x7[(uint8_t)c];

    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            if (glyph[col] & (1 << row))
                set_oled_data_buff(oled, x + col, y + row, 1);
        }
    }
}

/*
픽셀버퍼에 문자열 표시하는 함수
좌표 지정하면, 가로쓰기로 이어서 씀
*/
void set_oled_data_strbuff(oled_fd_t *oled, int x, int y, const char *s)
{
    while (*s) {
        set_oled_data_charbuff(oled, x, y, *s);
        x += 6;
        s++;
    }
}

