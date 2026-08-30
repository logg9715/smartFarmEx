#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include "util/util_log.h"
#include "uart/uart_parser.h"
#include "uart/uart.h"
#include "config.h"
#include "frame/frame_sht30.h"
#include "epoll_loop.h"

#include "oled/oled.h"

#include "util/util_time.h"

#define BUFFSIZE 512

int start_uart(void)
{
    int fd;
    struct termios tio;

    fd = open(UART_DEVICE, O_RDWR | O_NOCTTY | O_CLOEXEC);
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

int read_uart_sht30(epoll_event_handle_t *handle)
{
    printf("UART!!!!!\n");
    char buff[BUFFSIZE];
    int fd = handle->fd;
    frame_parser_t *parser = &((epoll_uart_ctx_t *)handle->ctx)->frame_parser;
    frame_sht30_t *sht30 = &((epoll_uart_ctx_t *)handle->ctx)->frame_sht30;

    ssize_t res = read(fd, buff, sizeof(buff));
    if(res == -1)
    {
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
        if(parser_feed(parser, sht30, (uint8_t *)buff, res) == 0)
        {   // CASE : 프레임의 모든 데이터가 다 들어온 경우
            // calc_sht30(sht30);
            char tmp[64] = {0};

            char timestamp[TM_BUFF_LEN] = {0};
            get_now_time(timestamp, sizeof(timestamp));

            snprintf(tmp, sizeof(tmp), "[UART] Temp : %d.%02d C, Humi : %d.%02d %%",
                (int)(sht30->temp/100), (int)(sht30->temp%100), (int)(sht30->humi/100), (int)(sht30->humi%100));
            printf("\n%s\n", tmp);

            // 디버깅용
            FILE *fp = fopen("/tmp/farmd_status", "w");
            if (fp) {
                fprintf(fp, "%s %s\n", timestamp, tmp);
                fclose(fp);
            }
            return 0;
        }
        else
        {
            return 1;
        }
    }
}

// static void calc_sht30(frame_sht30_t *sht30)
// {
//     sht30->raw_temp = (uint16_t)(-45 + 175 * ((float)sht30->raw_temp / 65535.0));
//     sht30->raw_humi = (uint16_t)(100 * ((float)sht30->raw_humi / 65535.0));
// }