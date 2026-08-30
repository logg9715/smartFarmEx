#ifndef OLED_H
#define OLED_H

typedef struct oled_fb
{
    uint8_t buff[1024];
} oled_fd_t;

int oled_cmd_send(int fd, uint8_t *cmd_list, int len);
int oled_data_send(int fd, oled_fd_t *oled);
void set_oled_data_buff(oled_fd_t *oled, int x, int y, int on);
void set_oled_data_charbuff(oled_fd_t *oled, int x, int y, char c);
void set_oled_data_strbuff(oled_fd_t *oled, int x, int y, const char *s);

#endif