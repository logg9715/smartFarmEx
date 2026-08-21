#include <time.h>

#include "util/util_time.h"

const char *get_current_time(void)
{
    static char buff[40];
    time_t now;
    struct tm *now_tm;

    now = time(NULL);
    now_tm = localtime(&now);
    strftime(buff, sizeof(buff), "%Y-%m-%d %H:%M:%S", now_tm);
    return buff;
}
