/* 웹 서버 테스트 코드 - epoll 버전 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/limits.h>
#include <sys/random.h>
#include <time.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>

#define PORT 8080
#define BACKLOG 8
#define RBUF_SIZE 4096
#define FILE_PATH "./html/"
#define VIEW_COUNT 10
#define MAX_SESSIONS 64
#define MAX_EVENTS 32
#define EXPIRE_INTERVAL_SEC 5   // 세션 만료 검사 주기
#define SESSION_TIMEOUT_SEC (30 * 60)   // 세션 유지시간(초)

/*
todo :
1. 웹서버 close 수명 늘리기 (keep-alive) - 추후
2. 요청자 정보 보관 (요청자, 명령내용, 타임아웃)
3. SSE push - 다음 단계
4. farmd 합체 시 web_init / web_handle_event / web_push_sensor 경계로 분리
5. [완료] epoll 구조 개편
*/

// ==================== fd 상태 구조체 ====================
// 모든 fd는 conn_t로 감싸서 epoll_event.data.ptr에 등록한다.
// 이벤트가 오면 type으로 분기 - farmd에 합류할 때도 같은 패턴 사용.

typedef enum
{
    FD_LISTEN,
    FD_SIGNAL,
    FD_TIMER,
    FD_HTTP
} fd_type_t;

typedef struct conn
{
    fd_type_t type;
    int       fd;
    char      rbuf[RBUF_SIZE];   // 수신 누적 버퍼
    size_t    total;             // 지금까지 누적된 바이트 수
} conn_t;

typedef struct http_req_cus
{
    char method[8];
    char path[256];
    char *body;
    size_t body_len;
    char cookie_sid[65];
} http_req_cus_t;

typedef struct session_cus
{
    char     sid[65];       // 세션 ID (hex 문자열)
    char     user[32];
    time_t   created;
    time_t   last_active;
    int      in_use;
} session_cus_t;

typedef struct web_view
{
    char path[PATH_MAX];
    char *body;
    size_t body_len;
} web_view_t;

web_view_t views[VIEW_COUNT] = {};
static session_cus_t g_sessions[MAX_SESSIONS];
static int g_running = 1;

// 프로토타입
int build_response_ex(char *buff, size_t buff_size, int status, const char *status_text, const char *content_type, const char *extra_headers, const char *body);

// ==================== 세션 / 파서 (기존 그대로) ====================

// 65바이트 버퍼에 64자 hex sid 생성. 성공 0
int gen_sid(char *out)
{
    unsigned char rnd[32];
    if (getrandom(rnd, sizeof(rnd), 0) != (ssize_t)sizeof(rnd))
        return -1;
    for (int i = 0; i < 32; i++)
        sprintf(out + i * 2, "%02x", rnd[i]);
    return 0;
}

// 리다이렉트 명령
void send_redirect(int fd, char *buff, size_t buff_size, const char *location, const char *extra_headers)
{
    char headers[256];
    snprintf(headers, sizeof(headers), "Location: %s\r\n%s", location, (extra_headers ? extra_headers : ""));

    int len = build_response_ex(buff, buff_size, 302, "Found", "text/plain", headers, "");
    if (len > 0)
        write(fd, buff, len);
}

// 로그아웃 시 호출. 해당 sid의 세션을 소거
void session_destroy(const char *sid)
{
    if (sid == NULL || sid[0] == '\0')
        return;

    for (int i = 0; i < MAX_SESSIONS; i++)
    {
        if (g_sessions[i].in_use && strcmp(g_sessions[i].sid, sid) == 0)
        {
            memset(&g_sessions[i], 0, sizeof(g_sessions[i]));
            return;
        }
    }
}

// body에서 key의 값을 추출. 성공 0, 없으면 -1
int parse_form_value(const char *body, const char *key, char *out, size_t out_size)
{
    if (body == NULL || key == NULL || out == NULL || out_size == 0)
        return -1;
    out[0] = '\0';

    size_t key_len = strlen(key);
    const char *p = body;

    while (*p)
    {
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=')
        {
            p += key_len + 1;
            size_t i = 0;
            while (p[i] && p[i] != '&' && i < out_size - 1)
            {
                out[i] = p[i];
                i++;
            }
            out[i] = '\0';
            return 0;
        }
        while (*p && *p != '&') p++;
        if (*p == '&') p++;
    }
    return -1;
}

int build_response_ex(char *buff, size_t buff_size, int status, const char *status_text, const char *content_type, const char *extra_headers, const char *body)
{
    return snprintf(buff, buff_size,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "%s"
        "\r\n"
        "%s",
        status, status_text, content_type, strlen(body),
        extra_headers ? extra_headers : "", body);
}

// 새 세션 등록
session_cus_t *session_create(const char *user)
{
    for (int i = 0; i < MAX_SESSIONS; i++)
    {
        if (g_sessions[i].in_use)
            continue;

        session_cus_t *s = &g_sessions[i];
        memset(s, 0, sizeof(*s));

        if (gen_sid(s->sid) < 0)
            return NULL;

        snprintf(s->user, sizeof(s->user), "%s", user);
        s->created = time(NULL);
        s->last_active = s->created;
        s->in_use  = 1;
        return s;
    }
    return NULL;
}

session_cus_t *session_find(const char *sid)
{
    if (sid == NULL || sid[0] == '\0')
        return NULL;

    for (int i = 0; i < MAX_SESSIONS; i++)
    {
        if (!g_sessions[i].in_use)
            continue;
        if (strcmp(g_sessions[i].sid, sid) == 0)
        {
            g_sessions[i].last_active = time(NULL);
            return &g_sessions[i];
        }
    }
    return NULL;
}

// 세션 만료체크
void session_expire_check(void)
{
    time_t now = time(NULL);
    for (int i = 0; i < MAX_SESSIONS; i++)
    {
        if (!g_sessions[i].in_use)
            continue;
        if (now - g_sessions[i].last_active > SESSION_TIMEOUT_SEC)
        {
            memset(&g_sessions[i], 0, sizeof(g_sessions[i]));
        }
    }
}

int parse_cookie_sid(const char *raw, char *out, size_t out_size)
{
    if (raw == NULL || out == NULL || out_size == 0) return -1;
    out[0] = '\0';

    const char *line = strstr(raw, "\r\nCookie:");
    if (line == NULL) return -1;
    line += 9;

    const char *line_end = strstr(line, "\r\n");
    if (line_end == NULL) return -1;

    const char *p = line;
    while ((p = strstr(p, "sid=")) != NULL && p < line_end)
    {
        char prev = *(p - 1);
        if (prev == ' ' || prev == ';' || prev == ':')
            break;
        p += 4;
    }
    if (p == NULL || p >= line_end) return -1;
    p += 4;

    size_t i = 0;
    while (p + i < line_end && p[i] != ';' && i < out_size - 1)
    {
        out[i] = p[i];
        i++;
    }
    out[i] = '\0';

    return (i > 0) ? 0 : -1;
}

int parse_request(const char *raw, http_req_cus_t *req)
{
    if (raw == NULL || req == NULL) return -1;

    memset(req, 0, sizeof(*req));

    if (sscanf(raw, "%7s /%255s", req->method, req->path) != 2)
        return -1;

    char *body = strstr(raw, "\r\n\r\n");
    if (body != NULL)
    {
        req->body = body + 4;
        req->body_len = strlen(body + 4);
    }

    parse_cookie_sid(raw, req->cookie_sid, sizeof(req->cookie_sid));

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
    char path_buff[PATH_MAX];
    snprintf(path_buff, sizeof(path_buff), "%s%s", FILE_PATH, view->path);
    FILE *fp = fopen(path_buff, "rb");
    if (fp == NULL)
    {
        perror("File not exist Or Error");
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size < 0)
    {
        perror("fseek");
        goto error_std;
    }

    char *buff = malloc(size + 1);
    if (!buff)
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

// ==================== epoll 유틸 ====================

// conn 생성 + epoll 등록. 실패 시 NULL
static conn_t *conn_add(int epfd, int fd, fd_type_t type)
{
    conn_t *c = calloc(1, sizeof(conn_t));
    if (c == NULL)
        return NULL;
    c->type = type;
    c->fd   = fd;

    struct epoll_event ev = {0};
    ev.events   = EPOLLIN;
    ev.data.ptr = c;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0)
    {
        perror("epoll_ctl ADD");
        free(c);
        return NULL;
    }
    return c;
}

// epoll 해제 + fd close + 메모리 해제
static void conn_close(int epfd, conn_t *c)
{
    epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
    close(c->fd);
    free(c);
}

// 요청 완성 판정: 1 완성, 0 미완, -1 버퍼 초과
static int is_request_complete(const char *buf, size_t total)
{
    const char *hdr_end = strstr(buf, "\r\n\r\n");
    if (hdr_end == NULL)
        return (total >= RBUF_SIZE - 1) ? -1 : 0;

    // 길이 변환
    size_t content_len = 0;
    const char *cl = strstr(buf, "\r\nContent-Length:");
    if (cl != NULL)
        content_len = (size_t)strtoul(cl + 17, NULL, 10);

    size_t body_received = total - (size_t)(hdr_end + 4 - buf);
    return (body_received >= content_len) ? 1 : 0;
}
 
// ==================== HTTP 요청 처리 (라우팅) ====================
// 완성된 요청 하나를 처리하고 응답을 쓴다. 연결 close는 호출부 담당.

static void handle_http_request(conn_t *c)
{
    char response_buff[8192];   // todo 동적할당
    http_req_cus_t req_info;

    printf("---- RAW REQUEST ----\n%s\n---------------------\n", c->rbuf);

    if (parse_request(c->rbuf, &req_info))
        return;

    printf("=====================\n%s | %s | sid=[%s]\n", req_info.method, req_info.path, req_info.cookie_sid);
    if (req_info.body)
        printf("%s\n", req_info.body);

    // ---- [비인증 허용] POST /login ----
    if (strcmp(req_info.method, "POST") == 0 && strcmp(req_info.path, "login") == 0)
    {
        char id[32] = {0};
        char pw[32] = {0};

        if (req_info.body == NULL ||
            parse_form_value(req_info.body, "id", id, sizeof(id)) < 0 ||
            parse_form_value(req_info.body, "pw", pw, sizeof(pw)) < 0)
        {
            build_response(response_buff, sizeof(response_buff), 400, "Bad Request", "text/plain", "missing id or pw");
            write(c->fd, response_buff, strlen(response_buff));
            return;
        }

        // 계정 검증 (테스트 단계: 하드코딩)
        if (strcmp(id, "admin") != 0 || strcmp(pw, "farm1234") != 0)
        {
            build_response(response_buff, sizeof(response_buff), 401, "Unauthorized", "text/plain", "login failed");
            write(c->fd, response_buff, strlen(response_buff));
            return;
        }

        session_cus_t *s = session_create(id);
        if (s == NULL)
        {
            build_response(response_buff, sizeof(response_buff), 500, "Internal Server Error", "text/plain", "session full");
            write(c->fd, response_buff, strlen(response_buff));
            return;
        }

        // 쿠키로 sid 전달
        char cookie_hdr[160];
        snprintf(cookie_hdr, sizeof(cookie_hdr), "Set-Cookie: sid=%s; HttpOnly; Path=/\r\n", s->sid);

        send_redirect(c->fd, response_buff, sizeof(response_buff), "/main.html", cookie_hdr);   // 메인페이지로 리다이렉트
        printf("[LOGIN] user=%s sid=%.8s...\n", s->user, s->sid);
        return;
    }

    // ---- 인증 게이트 ----
    session_cus_t *sess = session_find(req_info.cookie_sid);
    if (sess == NULL && strcmp(req_info.path, "login.html") != 0)
    {
        send_redirect(c->fd, response_buff, sizeof(response_buff), "/login.html", NULL);    // 미인증자 로그인 페이지로 다시 보냄
        return;
    }

    // ---- [인증 필요] GET /logout ----
    if (strcmp(req_info.method, "GET") == 0 && strcmp(req_info.path, "logout") == 0)
    {
        printf("[LOGOUT] user=%s\n", sess->user);
        session_destroy(req_info.cookie_sid);

        send_redirect(c->fd, response_buff, sizeof(response_buff), "/login.html", "Set-Cookie: sid=; Max-Age=0; Path=/\r\n");
        return;
    }

    // ---- [인증 필요] view 서빙 ----
    for (int i = 0; i < VIEW_COUNT; i++)
    {
        if (views[i].body && !strcmp(views[i].path, req_info.path))
        {
            int response_len = build_response(response_buff, sizeof(response_buff), 200, "OK", "text/html; charset=utf-8", views[i].body);
            if (response_len <= 0)
            {
                perror("build_response");
                return;
            }
            ssize_t w = write(c->fd, response_buff, response_len);
            if (w < 0) perror("write");
            return;
        }
    }

    // ---- 매칭 실패: 404 ----
    build_response(response_buff, sizeof(response_buff), 404, "Not Found", "text/plain", "not found");
    write(c->fd, response_buff, strlen(response_buff));
}

// ==================== HTTP fd 이벤트 처리 ====================
// EAGAIN까지 읽어 누적하고, 요청이 완성되면 처리 후 연결 종료.
static void handle_http_event(int epfd, conn_t *c)
{
    while (1)
    {
        ssize_t n = read(c->fd, c->rbuf + c->total, sizeof(c->rbuf) - 1 - c->total);
        if (n > 0)
        {
            c->total += n;
            c->rbuf[c->total] = '\0';
            continue;
        }
        if (n == 0)
        {
            // 상대가 연결 종료 (요청 미완인 채 끊김)
            conn_close(epfd, c);
            return;
        }
        // n < 0
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break; // 지금 도착한 만큼 다 읽음
        if (errno == EINTR)
            continue;
        perror("read");
        conn_close(epfd, c);
        return;
    }

    int done = is_request_complete(c->rbuf, c->total);
    if (done == 0)
        return; // 미완 - 다음 EPOLLIN 이벤트에서 이어서 누적    
    if (done == 1)
        handle_http_request(c);
    // done < 0 (버퍼 초과)이면 응답 없이 종료

    conn_close(epfd, c);   // Connection: close 정책 - 처리 후 항상 종료
}

// ==================== main ====================

int main(void)
{
    int listen_fd = -1;
    int epfd = -1, sig_fd = -1, timer_fd = -1;
    int ret = EXIT_FAILURE;

    // ================================================================= VIEW
    const char *VIEW_NAMES[] =
    {
        "main.html", "hello.html", "login.html"
    };
    int view_names_len = sizeof(VIEW_NAMES) / sizeof(VIEW_NAMES[0]);
    if (view_names_len > VIEW_COUNT)
    {
        printf("too many view files\n");
        return -1;
    }
    for (int i = 0; i < view_names_len; i++)
    {
        strcpy(views[i].path, VIEW_NAMES[i]);

        int res = open_view_file(&views[i]);
        if (res)
            memset(&views[i], 0, sizeof(views[i]));
        else printf("opened View >======>> %s\n", views[i].path);
    }

    // ================================================================= 시그널
    signal(SIGPIPE, SIG_IGN);

    // SIGTERM/SIGINT를 fd 이벤트로 전환 (signalfd)
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0)
    {
        perror("sigprocmask");
        goto error_std;
    }
    sig_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sig_fd < 0)
    {
        perror("signalfd");
        goto error_std;
    }

    // ================================================================= 타이머
    // 세션 만료 검사 주기 실행용 타이머
    timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC); // 모노토닉(부팅후 계속 증가), 논블로킹 | exec일떄 close
    if (timer_fd < 0)
    {
        perror("timerfd_create");
        goto error_std;
    }
    struct itimerspec its =
    {
        .it_interval = { .tv_sec = EXPIRE_INTERVAL_SEC },   // 10초 인터벌
        .it_value    = { .tv_sec = EXPIRE_INTERVAL_SEC },   // 10초 뒤에 발화
    };
    if (timerfd_settime(timer_fd, 0, &its, NULL) < 0)   // 타이머 시작
    {
        perror("timerfd_settime");
        goto error_std;
    }

    // ================================================================= 소켓
    listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (listen_fd < 0)
    {
        perror("Error main > socket");
        goto error_std;
    }

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
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
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("Error main > bind");
        goto error_std;
    }
    if (listen(listen_fd, BACKLOG) < 0)
    {
        perror("Error main > listen");
        goto error_std;
    }

    // ================================================================= epoll
    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0)
    {
        perror("epoll_create1");
        goto error_std;
    }

    // epoll 등록
    conn_t *listen_conn = conn_add(epfd, listen_fd, FD_LISTEN);
    conn_t *sig_conn = conn_add(epfd, sig_fd,    FD_SIGNAL);
    conn_t *timer_conn = conn_add(epfd, timer_fd,  FD_TIMER);
    if (!listen_conn || !sig_conn || !timer_conn)
        goto error_std;

    printf("========= listening on port %d (epoll) =========\n", PORT);
    // ================================================================= 이벤트 루프
    struct epoll_event events[MAX_EVENTS];

    while (g_running)
    {
        int ep_ready = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (ep_ready < 0)
        {
            if (errno == EINTR)
                continue;
            perror("epoll_wait");
            break;
        }
        
        for (int i = 0; i < ep_ready; i++)
        {
            conn_t *c = events[i].data.ptr;
            switch (c->type)
            {
                case FD_SIGNAL: // 시그널 처리
                {
                    struct signalfd_siginfo si;
                    read(sig_fd, &si, sizeof(si));
                    printf("\n[SHUTDOWN] signal %u received\n", si.ssi_signo);
                    g_running = 0;
                    break;
                }

                case FD_TIMER:  // 세션 타이머 처리
                {
                    uint64_t expirations;
                    read(timer_fd, &expirations, sizeof(expirations));
                    session_expire_check();
                    break;
                }

                case FD_LISTEN: // LISTEN 처리
                {
                    // 대기 중인 연결 전부 accept (EAGAIN까지)
                    while (1)
                    {
                        struct sockaddr_in cli;
                        socklen_t cli_len = sizeof(cli);
                        int conn_fd = accept4(listen_fd, (struct sockaddr *)&cli, &cli_len, SOCK_NONBLOCK | SOCK_CLOEXEC); // 논블로킹 연결 생성
                        if (conn_fd < 0)
                        {
                            if (errno == EAGAIN || errno == EWOULDBLOCK)
                                break;
                            if (errno == EINTR)
                                continue;
                            perror("accept4");
                            break;
                        }

                        char ip[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
                        printf("=====> conn from %s:%u\n", ip, ntohs(cli.sin_port));

                        if (conn_add(epfd, conn_fd, FD_HTTP) == NULL)   // 커넥션 생성 & epoll 등록
                            close(conn_fd);
                    }
                    break;
                }

                case FD_HTTP:
                    handle_http_event(epfd, c);
                    break;
            }
        }
    }

    ret = EXIT_SUCCESS;

error_std:
    // 정리 - farmd 합체 시 이 자리에서 펌프 OFF가 최우선이 된다
    if (epfd >= 0)
        close(epfd);
    if (listen_fd >= 0)
        close(listen_fd);
    if (sig_fd >= 0)
        close(sig_fd);
    if (timer_fd >= 0) 
        close(timer_fd);

    for (int i = 0; i < VIEW_COUNT; i++)
    {
        if (views[i].body)
            free(views[i].body);
    }

    printf("end.\n");
    return ret;
}