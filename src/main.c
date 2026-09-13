#include "epoll_loop.h"
#include "sig_handler.h"
#include "util/util_log.h"
#include "config.h"
#include <stdlib.h>

int main(int argc, char const *argv[])
{
    int signal_fd;
    // # --- ready process --- 
    // log
    log_set_level(LOG_LEVEL);   // 로그 레벨 설정
    if(log_open(LOG_PATH) == -1)
        return EXIT_FAILURE;
    if(stat_log_open(STAT_LOG_PATH) == -1)
        return EXIT_FAILURE;
    log_write(LL_INFO, LC_SHOW_PRINTF, "log ready");

    // signal handler
    if((signal_fd = set_signal_handler()) == -1) 
        return EXIT_FAILURE;

    // # --- loop process ---
    log_write(LL_INFO, LC_SHOW_PRINTF, "loop start");
    epoll_loop(signal_fd);

    // # --- close process ---
    if(signal_fd)
        close_signal_handler(signal_fd);

    log_write(LL_INFO, LC_SHOW_PRINTF, "end Process");
    log_flush();
    log_close();
    stat_log_close();

    return 0;
}


