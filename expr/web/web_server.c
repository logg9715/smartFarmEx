/* 웹 서버 테스트 코드 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 9090
#define BACKLOG 8
#define RBUF_SIZE 4096
/*
todo : 
1. 웹서버 close 수명 늘리기
2. 요청자 정보 보관하게 하기 (요청자, 명령내용, 타임아웃)
3. epoll 프로세스에는 명령 보냈다가 완료되면 완료보고 따로 받기
4. 제어프로세스 <-> stm32간 uart는 회선 1개로 가능, 다만 프로토콜 만들 필요성 있음.
5. 웹도 epoll구조로 개편 예정.
    ㄴ 작업 완료됬다는 알림을 실시간으로 받고 클라이언트에게 push해주기 위해서는 epoll이 적합

*/

// 응답 헤더
static const char response[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Content-Length: 46\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<html><body><h1>this is web server test</h1></body></html>";

int main(void)
{
    int listen_fd = -1;
    int ret = EXIT_FAILURE;

    // SIG_PIPE로 정리되는것 방지
    signal(SIGPIPE, SIG_IGN);

    // (ipv4사용, TCP연결사용, 0=프로토콜 기본값 사용)
    // SOCK_CLOEXEC << - 자식 프로세스면 서버fd정리 플래그, 필요하면 사용
    listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(listen_fd < 0)
    {
        perror("Error main > socket");
        goto error_std;
    }

    int opt = 1;
    // 서버 재시작할떄 TIME WAIT 문제 안생기고 bind되게하는 소켓 옵션
    if(setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("Error main > setsocketopt");
        goto error_std;
    }

    struct sockaddr_in addr = 
    {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(PORT),
    };
    if(bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) // 포트 바인드
    {
        perror("Error main > bind");
        goto error_std;
    }
    if(listen(listen_fd, BACKLOG) < 0) // BACKLOG만큼 대기열 허용
    {
        perror("Error main > listen");
        goto error_std;
    }

    printf("========= listening on port %d =========\n", PORT);

    // 굳이 스레드로 응답대기열 늘릴 필요 없을 듯 하다.
    while(1) 
    {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);

        int conn_fd = accept(listen_fd, (struct sockaddr *)&cli, &cli_len); 
        if (conn_fd < 0) 
        {
            if (errno == EINTR)
                continue;
            perror("accept");
            goto error_std;
        }

        // 클라이언트 정보 표출용 
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));  // 
        printf("=====> conn from %s:%u\n", ip, ntohs(cli.sin_port));

        // request정보 읽기
        char rbuf[RBUF_SIZE];
        ssize_t n = read(conn_fd, rbuf, sizeof(rbuf) - 1);
        if (n > 0) 
        {
            rbuf[n] = '\0';
            printf("---- request (%zd bytes) ----\n%s\n", n, rbuf);
        }

        // 응답
        ssize_t w = write(conn_fd, response, sizeof(response) - 1);
        if (w < 0)
            perror("write");

        close(conn_fd);
    }

error_std:
    if(listen_fd >= 0)
        close(listen_fd);
    return ret;
}
