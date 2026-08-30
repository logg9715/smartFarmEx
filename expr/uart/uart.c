#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

// ======================================================================
typedef struct 
{
    uint8_t buff[128];
    size_t len;
} frame_parser_t;

void parser_feed(frame_parser_t *p, const uint8_t *data, size_t len)
{
    if (sizeof(p->buff) - p->len < len)
    {
        // memset(p->buff, 0, sizeof(p->buff));
        p->len = 0;
    }
    memcpy(&p->buff[p->len], data, len);
    p->len += len;
}


// ======================================================================

int main(void)
{
    struct termios tio;
    ssize_t res;
    char buff[512];
    char buff_dbug[64];
    frame_parser_t parser = {0};

    int fd = open("/dev/serial0", O_RDWR | O_NOCTTY | O_CLOEXEC);
    if(fd == -1)
    {
        perror("open");
        goto err_1;
    }

    if(tcgetattr(fd, &tio))
    {
        perror("tcgetattr");
        goto err;
    }
    cfmakeraw(&tio);

    if(cfsetispeed(&tio, B115200))
    {
        perror("cfsetispeed");
        goto err;
    }
    if(cfsetospeed(&tio, B115200))
    {
        perror("cfsetospeed");
        goto err;
    }

    if(tcsetattr(fd, TCSANOW, &tio))
    {
        perror("tcsetattr");
        goto err;
    }

    while(1)
    {
        res = read(fd, buff, sizeof(buff));
        // parser_feed(&parser, (uint8_t*)buff, res);
        snprintf(buff_dbug, sizeof(buff_dbug)," [n=%zd]\n", res);
        write(STDOUT_FILENO, buff, res);
        write(STDOUT_FILENO, buff_dbug, strlen(buff_dbug)); // <- 확인해보니 이 환경에서는 16bytes씩 쪼개서 넘어오는듯 
    }

    close(fd);
    return 0;
err:
    close(fd);
err_1:
    return -1;
}