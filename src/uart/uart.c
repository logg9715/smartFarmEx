#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include "util/util_log.h"
#include "uart/uart_parser.h"
#include "uart/uart.h"
#include "config.h"
#include "frame/frame_stm32.h"
#include "epoll_loop.h"
#include "oled/oled.h"
#include "util/util_time.h"

#define BUFFSIZE 512

static frame_stm32_t g_frame_stm32 = {0};
static char g_stm32_last_rcv_tm[TM_BUFF_LEN]; /* 마지막 데이터 수신시간 */

int start_uart(void)
{
    int fd;
    struct termios tio;

    fd = open(UART_DEVICE, O_RDWR | O_NOCTTY | O_CLOEXEC | O_NONBLOCK);
    if(fd == -1)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "start_uart > open");
        goto error;
    }

    if(tcgetattr(fd, &tio))
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "start_uart > tcgetattr");
        goto error_fd;
    }
    cfmakeraw(&tio);

    if(cfsetispeed(&tio, UART_SPEED))
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "start_uart > cfsetispeed");
        goto error_fd;
    }
    if(cfsetospeed(&tio, UART_SPEED))
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "start_uart > cfsetospeed");
        goto error_fd;
    }

    if(tcsetattr(fd, TCSANOW, &tio))
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "start_uart > tcsetattr");
        goto error_fd;
    }

    return fd;
error_fd:
    close(fd);
error:
    return -1;
}

int read_uart_stm32(epoll_event_handle_t *handle)
{
    char buff[BUFFSIZE];
    int fd = handle->fd;
    frame_parser_t *parser = &((epoll_uart_ctx_t *)handle->ctx)->frame_parser;
    frame_stm32_t *stm32 = &((epoll_uart_ctx_t *)handle->ctx)->frame_stm32;

    ssize_t res = read(fd, buff, sizeof(buff));
    if(res == -1)
    {
        if (errno == EAGAIN || errno == EINTR)
            return 0; /* 일시적. 다음 이벤트에서 다시 */

        log_write(LL_ERROR, LC_SHOW_PERROR, "read_uart > read");
        return -1;
    }
    else if (res == 0)
    {
        log_write(LL_DEBUG, LC_SHOW_PRINTF, "read_uart > read is empty : 0");
        return 1;
    }
    else
    {
        if(parser_feed(parser, stm32, (uint8_t *)buff, res) == 0)
        {   // CASE : 프레임의 모든 데이터가 다 들어온 경우
            g_frame_stm32 = *stm32;
            get_now_time(g_stm32_last_rcv_tm, sizeof(g_stm32_last_rcv_tm));
            // --------------디버깅용--------------
            // calc_sht30(stm32);
            char tmp[64] = {0};
            snprintf(tmp, sizeof(tmp), "\n[UART] Temp : %d.%02d C, Humi : %d.%02d %%, light : %d\n",
                (int)(stm32->temp/100), (int)(stm32->temp%100), (int)(stm32->humi/100), (int)(stm32->humi%100), (int)(stm32->light));
            log_write(LL_DEBUG, LC_SHOW_PRINTF | LC_NOT_WRITE, tmp);

            FILE *fp = fopen("/tmp/farmd_status", "w");
            if (fp) {
                char timestamp[TM_BUFF_LEN] = {0};
                get_now_time(timestamp, sizeof(timestamp));
                fprintf(fp, "%s %s\n", timestamp, tmp);
                fclose(fp);
            }
            // ------------------------------------
            return 0;
        }
        else
        {
            return 1;
        }
    }
}

void *get_stm32_value(frame_stm32_t *out) {*out = g_frame_stm32;}
void get_stm32_last_rcv_tm(char out[]) {strcpy(out, g_stm32_last_rcv_tm);}

// static void calc_sht30(frame_stm32_t *stm32)
// {
//     stm32->raw_temp = (uint16_t)(-45 + 175 * ((float)stm32->raw_temp / 65535.0));
//     stm32->raw_humi = (uint16_t)(100 * ((float)stm32->raw_humi / 65535.0));
// }