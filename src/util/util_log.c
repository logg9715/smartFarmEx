#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>

#include "config.h"
#include "util/util_time.h"
#include "util/util_log.h"

static FILE *g_log;
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;
static enum log_level g_show_level = LOG_LEVEL;

static char *get_log_level_str(enum log_level);
static void copy_errno_str(int err, char *buf, size_t size);

// TODO : logrotate 사용하는 방법으로 수정 예정
// epoll에 signalfd로 로그파일 경로 바뀐 것 처리도 같이 해야함.

/*
    로그파일 open하는 함수. 경로가 없다면 프로세스 종료, 파일만 없다면 생성.
*/
int log_open(const char *path)
{
    g_log = fopen(path, "a");
    if(!g_log)
    {
        perror("Error log_open > fopen");
        return -1;
    }
    // 이스케이프 문자를 만날떄마다 write실행
    setvbuf(g_log, NULL, _IOLBF, 0);
    return 0;
}

/*
    로그 표시레벨 설정 (설정 안하면 기본값 : 전부 표시)
    설정값보다 레벨이 미만인 로그는 표시 안 함
*/
void log_set_level(const enum log_level lvl) 
{
    g_show_level = lvl; 
}

/*
    로그 작성 함수. 먼저 log_open을 안 했다면, 파일로 로그 출력이 안됨.
    log_flag는 |연산자로 중첩 가능 
*/
void log_write(const enum log_level ll, const enum log_flag lc, const char *context)
{
    int err = errno;
    const int use_errno = (lc & LC_SHOW_PERROR) ? 1 : 0;
    char timestamp[40], ebuff[64];
    timestamp[0] = '\0';
    ebuff[0] = '\0';

    if(g_show_level > ll) 
        return;

    if(get_now_time(timestamp, sizeof(timestamp)) == -1)
        timestamp[0] = '\0';

    if(use_errno)
        copy_errno_str(err, ebuff, sizeof(ebuff));

    if(lc & LC_SHOW_PERROR)
        fprintf(stderr, "[%s] (%s) %s | %s\n", timestamp, get_log_level_str(ll), context, ebuff);
    
    if(lc & LC_SHOW_PRINTF)
        printf("%s\n", context);

    if(!(lc & LC_NOT_WRITE))
    {
        pthread_mutex_lock(&g_log_lock);
        if(g_log) 
        {
            if(use_errno)
                fprintf(g_log, "[%s] (%s) %s | %s\n", timestamp, get_log_level_str(ll), context, ebuff);
            else
                fprintf(g_log, "[%s] (%s) %s \n", timestamp, get_log_level_str(ll), context);
        }
        pthread_mutex_unlock(&g_log_lock);
    }
    // errno 상태 복구
    // errno=스레드별로 가져서, 락 필요 없을듯
    errno = err;
}

static void copy_errno_str(int err, char *buf, size_t size)
{
#if defined(__GLIBC__) && defined(_GNU_SOURCE)
    const char *msg = strerror_r(err, buf, size);
    if(msg != buf)
    {
        strncpy(buf, msg, size - 1);
        buf[size - 1] = '\0';
    }
#else
    if(strerror_r(err, buf, size) != 0)
        snprintf(buf, size, "errno %d", err);
#endif
}

/*
    로그 fd 정리하는 함수
*/
void log_close(void)
{
    pthread_mutex_lock(&g_log_lock);
    if(g_log)
    {
        fclose(g_log);
        g_log = NULL;
    }
    pthread_mutex_unlock(&g_log_lock); 
}

/*
    로그 버퍼 비우는 함수
    종료, fork하기 전에 실행
*/
void log_flush(void)
{
    pthread_mutex_lock(&g_log_lock);
    if(g_log)
        fflush(g_log);
    pthread_mutex_unlock(&g_log_lock); 
}

/*
    로그 레벨 => 문자열 이름으로 바꾸는 함수
*/
static char *get_log_level_str(enum log_level lvl)
{
    switch (lvl) {
    case LL_DEBUG:
        return "DEBUG";
    case LL_INFO:     
        return "INFO";
    case LL_WARN:
        return "WARNING";
    case LL_ERROR:     
        return "ERROR";
    case LL_CRITICAL: 
        return "CRITICAL_ERROR";
    default:
        return "";
    }
}