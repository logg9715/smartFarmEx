#ifndef OLED_H
#define OLED_H

#include "epoll_loop.h"

typedef struct oled_fb
{
    uint8_t buff[1024];
} oled_fb_t;

typedef struct { 
    int oled_fd; 
} oled_ctx_t;

int oled_cmd_send(int fd, uint8_t *, int);
int oled_data_send(int fd, oled_fb_t *);
void set_oled_data_buff(oled_fb_t *, int, int, int);
void set_oled_data_charbuff(oled_fb_t *, int, int, char);
void set_oled_data_strbuff(oled_fb_t *, int, int, const char *);
int ready_oled(int *, int *);
void clear_oled_display(int);
int oled_handle(epoll_event_handle_t *);

#endif