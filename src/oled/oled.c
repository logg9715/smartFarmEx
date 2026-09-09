#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <sys/timerfd.h>

#include "oled/font5x7.h"
#include "oled/oled.h"
#include "epoll_loop.h"
#include "util/util_log.h"
#include "uart/uart.h"
#include "frame/frame_stm32.h"
#include "util/util_time.h"

#define OLED_REFRESH_TM 1

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
int oled_data_send(int fd, oled_fb_t *oled)
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
void set_oled_data_buff(oled_fb_t *oled, int x, int y, int on)
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
void set_oled_data_charbuff(oled_fb_t *oled, int x, int y, char c)
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
void set_oled_data_strbuff(oled_fb_t *oled, int x, int y, const char *s)
{
    while (*s) {
        set_oled_data_charbuff(oled, x, y, *s);
        x += 6;
        s++;
    }
}

int ready_oled(int *oled_fd_out, int *oled_timer_fd_out)
{
    int res;
    int fd = -1, timer_fd = -1;
    
    fd = open("/dev/i2c-1", O_RDWR);
    if(fd < 0)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "ready_oled > open");
        goto err;
    }

    if((res = ioctl(fd, I2C_SLAVE, 0x3C)) == -1)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "ready_oled > ioctl");
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

    // ======================================================== 타이머

    timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if(timer_fd < 0)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "ready_oled > timerfd_create");
        goto err;
    }
    struct itimerspec its =
    {
        .it_interval = { .tv_sec = OLED_REFRESH_TM },   // 10초 인터벌
        .it_value    = { .tv_sec = OLED_REFRESH_TM },   // 10초 뒤에 발화
    };
    if (timerfd_settime(timer_fd, 0, &its, NULL) < 0)   // 타이머 시작
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "ready_oled > timerfd_settime");
        goto err;
    }

    // 설정 초기화
    oled_cmd_send(fd, init_seq, sizeof(init_seq));
    *oled_fd_out = fd;
    *oled_timer_fd_out = timer_fd;
    return 0;
err:
    if(fd >= 0)
        close(fd);
    if(timer_fd >= 0)
        close(timer_fd);
    return -1;
}

void clear_oled_display(int fd)
{
    oled_fb_t oled = {.buff = {0}};
    oled_data_send(fd, &oled);
}

int oled_handle(epoll_event_handle_t *handle)
{
    uint64_t exp;
    read(handle->fd, &exp, sizeof(exp));   // 타이머 만료 비우기

    oled_ctx_t *ctx = handle->ctx;
    int oled_fd = ctx->oled_fd;

    char res[64] = {0};
    oled_fb_t fb = {.buff = {0}};
    frame_stm32_t st = {0};
    get_stm32_value(&st);
    char timestamp[TM_BUFF_LEN] = {0};
    get_stm32_last_rcv_tm(timestamp);
    
    snprintf(res, sizeof(res), "TEMP:%d.%02d HUMI:%d.%02d", st.temp / 100, st.temp % 100, st.humi / 100, st.humi % 100);
    set_oled_data_strbuff(&fb, 0, 0, res);

    snprintf(res, sizeof(res), "LIGHT:%d", st.light);
    set_oled_data_strbuff(&fb, 0, 16, res);

    set_oled_data_strbuff(&fb, 0, 32, timestamp);

    oled_data_send(ctx->oled_fd, &fb);
    return 0;
}