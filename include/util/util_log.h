#ifndef UTIL_LOG_H
#define UTIL_LOG_H

enum log_level {
    LL_DEBUG    = 0,
    LL_INFO     = 5,
    LL_WARN     = 10,
    LL_ERROR    = 11,
    LL_CRITICAL = 99,
    LL_ANYWAY   = 100,
};

enum log_flag {
    LC_SHOW_PERROR = 1 << 0,    // perror로 표시(errno 같이 표시됨)
    LC_SHOW_PRINTF = 1 << 1,    // printf로 표시
    LC_NOT_WRITE   = 1 << 2,    // 로그 파일에 기록 안함
};

void log_set_level(const enum log_level); 
int log_open(const char *);
int log_reopen(const char *);
void log_write(const enum log_level, const enum log_flag, const char *);
void log_close(void);
int stat_log_open(const char *);
int stat_log_reopen(const char *);
void stat_log_write(const enum log_level, const enum log_flag, const char *);
void stat_log_close(void);
void log_flush(void);

#endif