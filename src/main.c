#include "epoll_loop.h"
#include "sig_handler.h"
#include <stdio.h>

int main(int argc, char const *argv[])
{
    int signal_fd;

    if((signal_fd = set_signal_handler()) == -1) return -1;
    else printf("signal_fd Success\n");

    epoll_loop(signal_fd);
    return 0;
}


