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


int parser_parse(frame_parser_t *p)
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

    char tmp[128];
    memcpy(tmp, &(p->buff[stx + 3]), len);
    tmp[len] = '\0';

    // # 소비하는 영역
    printf(">>> %s\n", tmp);
    
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

int parser_feed(frame_parser_t *p, const uint8_t *data, size_t len)
{
    if (sizeof(p->buff) - p->len < len)
    {
        p->len = 0;
    }
    memcpy(&p->buff[p->len], data, len);
    p->len += len;

    int res = parser_parse(p);    //  반환값 일부러 안씀 나중에 처리예정
    return res;
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
        if(parser_feed(&parser, (uint8_t*)buff, res) == 0)
            printf("--------------\n");
    }

    close(fd);
    return 0;
err:
    close(fd);
err_1:
    return -1;
}