#ifndef UTIL_LOG_H
#define UTIL_LOG_H

enum log_level {
    LL_DEBUG    = 0,
    LL_INFO     = 5,
    LL_WARN     = 10,
    LL_ERROR    = 11,
    LL_CRITICAL = 99,
};

enum log_flag {
    LC_SHOW_PERROR = 1 << 0,
    LC_SHOW_PRINTF = 1 << 1,
    LC_NOT_WRITE   = 1 << 2,
};

int log_open(const char *);
void log_set_level(enum log_level); 
void log_write(const enum log_level, const enum log_flag, const char *);
void log_close(void);
void log_flush(void);

#endif