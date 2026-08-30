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
#include <linux/limits.h>

#define PORT 80
#define BACKLOG 8
#define RBUF_SIZE 4096
#define FILE_PATH "./html/"
#define VIEW_COUNT 10
#define MAX_SESSIONS 64


/*
todo : 
1. 웹서버 close 수명 늘리기
2. 요청자 정보 보관하게 하기 (요청자, 명령내용, 타임아웃)
3. epoll 프로세스에는 명령 보냈다가 완료되면 완료보고 따로 받기
4. 제어프로세스 <-> stm32간 uart는 회선 1개로 가능, 다만 프로토콜 만들 필요성 있음.
5. 웹도 epoll구조로 개편 예정.
    ㄴ 작업 완료됬다는 알림을 실시간으로 받고 클라이언트에게 push해주기 위해서는 epoll이 적합
*/

typedef struct http_req_cus
{
    char method[8];
    char path[256];
    char *body;
    size_t body_len;
    char cookie_sid[64];
} http_req_cus_t;

typedef struct session_cus{
    char     sid[65];       // 세션 ID (hex 문자열)
    char     user[32];
    time_t   created;
    time_t   last_active;   // 타임아웃 계산용 (TODO 2번이랑 연결됨)
    int      in_use;
} session_cus_t;

typedef struct web_view {
    char path[PATH_MAX];
    char *body;
    size_t body_len;
} web_view_t;

web_view_t views[VIEW_COUNT] = {};
static session_cus_t g_sessions[MAX_SESSIONS];

int parse_request(const char *raw, http_req_cus_t *req)
{
    if (raw == NULL || req == NULL) return -1;

    if(sscanf(raw, "%7s /%255s", req->method, req->path) != 2)
        return -1;

    char *body = strstr(raw, "\r\n\r\n");
    if(body != NULL)
    {
        req->body = body + 4;
        req->body_len = strlen(body + 4);
    }
    
    return 0;
}

int build_response(char *buff, size_t buff_size, int status, const char *status_text, const char *content_type, const char *body)
{
    size_t body_len = strlen(body);
    int res = snprintf(buff, buff_size, 
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        status, status_text, content_type, body_len, body
    );
    return res;
}

int open_view_file(web_view_t *view)
{
    char path_buff[PATH_MAX]; // <linux/limits.h>
    snprintf(path_buff, sizeof(path_buff), "%s%s", FILE_PATH, view->path);
    FILE *fp = fopen(path_buff, "rb");
    if(fp == NULL)
    {
        perror("File not exist Or Error");
        goto error_std;
    }

    // 파일길이 확인
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if(size < 0)
    {
        perror("fseek");
        goto error_std;
    }

    char *buff = malloc(size + 1);
    if(!buff)
    {
        perror("malloc");
        goto error_std;
    }

    size_t n = fread(buff, 1, size, fp);
    buff[n] = '\0';
    view->body = buff;
    view->body_len = n;

    fclose(fp);
    return 0;
error_std:
    fclose(fp);
    return -1;
}



int main(void)
{
    int listen_fd = -1;
    int ret = EXIT_FAILURE;
    char response_buff[8192];

    // ================================================================= VIEW
    const char *VIEW_NAMES[] = 
    {
        "main.html", "hello.html"
    };
    int view_names_len = sizeof(VIEW_NAMES) / sizeof(VIEW_NAMES[0]);
    if(view_names_len > VIEW_COUNT)
    {
        printf("too many view files\n");
        return -1;
    }
    for(int i = 0; i < view_names_len; i++)
    {
        strcpy(views[i].path, VIEW_NAMES[i]);

        int res = open_view_file(&views[i]);
        if(res)
            memset(&views[i], 0, sizeof(views[i]));
        else printf("opened View >======>> %s\n", views[i].path);

    }

    // =================================================================

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
            http_req_cus_t req_info;
            rbuf[n] = '\0';
            parse_request(rbuf, &req_info);
            printf("======================================================\n%s | %s\n", req_info.method, req_info.path);
            if(req_info.body)
                printf("%s\n", req_info.body);    

            // # 응답
            for(int i =0; i < VIEW_COUNT; i++)
            {
                // 존재하는 view에 대한 요청을 한 경우
                if(views[i].body && !strcmp(views[i].path, req_info.path))
                {
                    // ----------------------------------------------------------------------
                    int response_len;
                    if((response_len = build_response(response_buff, sizeof(response_buff), 200, "OK", "text/html; charset=utf-8", views[i].body)) <= 0)
                    {
                        perror("build_response");
                        close(conn_fd);
                        continue;
                    }
                    else
                    {
                        ssize_t w = write(conn_fd, response_buff, response_len);
                        if (w < 0) perror("write");
                    }
                    // ----------------------------------------------------------------------
                    break;
                }
            }
        }
        close(conn_fd);
    }

    for(int i =0; i < VIEW_COUNT; i++)
    {
        if(views[i].body)
            free(views[i].body);
    }

error_std:
    if(listen_fd >= 0)
        close(listen_fd);
    return ret;
}
