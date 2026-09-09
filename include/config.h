#ifndef CONFIG_H
#define CONFIG_H

// util_log
#define LOG_PATH "log/log.txt"
#define LOG_LEVEL LL_ERROR

// uart
#define UART_DEVICE "/dev/serial0"
#define UART_SPEED B115200
#define WATER_MAX_SEC 30 

// webserver
#define WEBPORT 8080
#define VIEW_FILE_PATH "./html/"
#define EXPIRE_INTERVAL_SEC 5   // 세션 만료 검사 주기
#define SESSION_TIMEOUT_SEC (30 * 60)   // 세션 유지시간(초)


#endif