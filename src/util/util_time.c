#include <time.h>
#include <stddef.h>

#include "util/util_time.h"

int get_now_time(char *out, const size_t out_size)
{
    time_t now;
    struct tm now_tm;

    if(out == NULL || out_size == 0)
        return -1;

    out[0] = '\0';
    now = time(NULL);
    if(localtime_r(&now, &now_tm) == NULL)  // note 스레드로 돌리니까 값 바뀌는 문제 확인. localtime_r로 수정 완료
        return -1;
    if(strftime(out, out_size, "%Y-%m-%d %H:%M:%S", &now_tm) == 0)
    {
        out[0] = '\0';
        return -1;
    }
    else 
        return 0;
}
