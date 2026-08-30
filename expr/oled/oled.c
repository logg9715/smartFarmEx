#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include "font5x7.h"

typedef struct oled_fb
{
    uint8_t buff[1024];
} oled_fd_t;

int oled_cmd_send(int fd, uint8_t *cmd_list, int len);
int oled_data_send(int fd, oled_fd_t *oled);
void set_oled_data_buff(oled_fd_t *oled, int x, int y, int on);
void set_oled_data_charbuff(oled_fd_t *oled, int x, int y, char c);
void set_oled_data_strbuff(oled_fd_t *oled, int x, int y, const char *s);

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

void set_oled_data_strbuff(oled_fd_t *oled, int x, int y, const char *s)
{
    while (*s) {
        set_oled_data_charbuff(oled, x, y, *s);
        x += 6;
        s++;
    }
}

void set_oled_data_buff(oled_fd_t *oled, int x, int y, int on)
{
    int list_idx = (y / 8) * 128 + x;
    int buff_idx = y % 8;
    if(on)
        oled->buff[list_idx] |= (1 << buff_idx);
    else
        oled->buff[list_idx] &= (0 << buff_idx);
}

int main(void)
{
    int res;
    
    int fd = open("/dev/i2c-1", O_RDWR);
    if(fd < 0)
    {
        perror("open");
        return -1;
    }

    if((res = ioctl(fd, I2C_SLAVE, 0x3C)) == -1)
    {
        perror("ioctl");
        goto err;
    }

    uint8_t init_seq[] = {
        0xAE,       // 화면 끄기(설정 중 깜빡임 방지)
        0xD5, 0x80, // 클럭 분주비/오실레이터 주파수
        0xA8, 0x3F, // 멀티플렉스 비율: 64줄
        0xD3, 0x00, // 세로 표시 오프셋: 없음
        0x40,       // 표시 시작 라인: 0
        0x8D, 0x14, // 차지펌프 활성화(패널 구동 전압 생성)
        0x20, 0x00, // 어드레싱 모드: 수평
        0xA1,       // 세그먼트 리맵: 좌우 방향
        0xC8,       // COM 스캔 방향: 상하 방향
        0xDA, 0x12, // COM 핀 하드웨어 구성(128x64 패널 배선)
        0x81, 0x7F, // 콘트라스트: 중간값
        0xD9, 0xF1, // 프리차지 기간
        0xDB, 0x40, // VCOMH 전압 레벨
        0xA4,       // GDDRAM 내용 표시 모드(0xA5 테스트 모드 해제)
        0xA6,       // 일반 표시(0xA7: 반전)
        0xAF,       // 화면 켜기
    };

    // 설정 초기화
    oled_cmd_send(fd, init_seq, sizeof(init_seq));

    // 화면 초기화
    oled_fd_t oled = {.buff = {0}};
    oled_data_send(fd, &oled);

    for (int i = 0; i < 64; i++)
        set_oled_data_buff(&oled, i, i, 1);
    
    for (int i = 0; i < 64; i++)
        set_oled_data_buff(&oled, 63 - i, i, 1);

    oled_data_send(fd, &oled);

    sleep(1);


    {
        int x = 0, y = 0;
        int dx = 1, dy = 1;

        while (1) {
            memset(oled.buff, 0, sizeof(oled.buff));
            set_oled_data_strbuff(&oled, x, y, "HELLO");
            oled_data_send(fd, &oled);

            x += dx;
            y += dy;

            if (x <= 0 || x >= 128 - 5 * 6)
                dx = -dx;
            if (y <= 0 || y >= 64 - 7)
                dy = -dy;

            usleep(33000);
        }
    }

    sleep(3);
    // ============ END ================
    uint8_t end_seq[] = {0xA4, 0xAE};
    oled_cmd_send(fd, end_seq, sizeof(end_seq));
    // =================================

    
    close(fd);
    return 0;

err:
    close(fd);
    return -1;
}