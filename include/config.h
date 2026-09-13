#ifndef CONFIG_H
#define CONFIG_H

// log
#define LOG_PATH "/var/log/farmd/common.log"
#define STAT_LOG_PATH "/var/log/farmd/ps_stat.log"
#define LOG_LEVEL LL_INFO

// uart
#define UART_DEVICE "/dev/serial0"
#define UART_SPEED B115200
#define WATER_MAX_SEC 30 
#define UART_CHECK_TIMEOUT 10
#define UART_CHECK_INTERVAL 5

// webserver
#define WEBPORT 8080
#define VIEW_FILE_PATH "/home/raspi/sv/farmd/html/"
#define EXPIRE_INTERVAL_SEC 5   // 세션 만료 검사 주기
#define SESSION_TIMEOUT_SEC (30 * 60)   // 세션 유지시간(초)

// stat_log
#define STAT_LOGGER_INTERVAL 60 //초

#endif