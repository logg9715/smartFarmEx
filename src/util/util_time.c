#include <time.h>

#include "util/util_time.h"

int get_now_time(char *out, size_t out_size)
{
    time_t now;
    struct tm *now_tm;

    now = time(NULL);
    now_tm = localtime(&now);
    return strftime(out, out_size, "%Y-%m-%d %H:%M:%S", now_tm);
}
