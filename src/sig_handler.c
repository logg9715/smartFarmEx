#include <string.h>
#include <sys/signalfd.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#include "sig_handler.h"
#include "util/util_log.h"

// 프로그램 종료/다시로드를 위한 시그널 핸들러
int set_signal_handler(void)
{
    int fd;
    sigset_t mask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGHUP);
    
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "set_signal_handler > sigprocmask");
        goto error_std;
    }

    if((fd = signalfd(-1, &mask, 0)) == -1)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "set_signal_handler > signalfd");
        goto error_std;
    }

    return fd;

error_std :
    return -1;
}

/*
return : SIGTERM=1, SIGHUP=2 그외 = 0, 에러 = -1 
*/
int get_signal_type(const int fd)
{
    struct signalfd_siginfo fdsi;
    ssize_t res;
    res = read(fd, &fdsi, sizeof(fdsi));
    if(res != sizeof(fdsi))
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "get_signal_type > read");
        return -1;
    }

    if(fdsi.ssi_signo == SIGTERM) 
        return 1;
    else if (fdsi.ssi_signo == SIGHUP)
        return 2;
    else 
        return 0;
}

void close_signal_handler(int fd)
{
    close(fd);
}