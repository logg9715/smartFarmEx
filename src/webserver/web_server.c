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

#include "webserver/web_server.h"
#include "config.h"
#include "util/util_log.h"
#include "epoll_loop.h"
#include "uart/uart.h"
#include "uart/uart_parser.h"
#include "util/util_time.h"

#define MAX_EVENTS 32
#define MAX_SESSIONS 64
#define VIEW_COUNT 10
#define BACKLOG 8
#define CONN_IDLE_TIMEOUT 30 

static web_view_t views[VIEW_COUNT] = {};
static session_cus_t g_sessions[MAX_SESSIONS];
static web_conn_t *g_conn_head = NULL; // 살아있는 연결 목록

// ==================== 세션 / 파서 ====================

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
    if (len < 0 || (size_t)len >= buff_size)
    {
        log_write(LL_ERROR, LC_SHOW_PRINTF, "send_redirect > response truncated\n");
        return;
    }

    if (write(fd, buff, (size_t)len) < 0)
        log_write(LL_ERROR, LC_SHOW_PERROR, "send_redirect > write");
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

// 응답 생성 + 전송. 성공 0, 실패 -1
static int send_response(int fd, char *buff, size_t buff_size, int status, const char *status_text, const char *content_type, const char *body)
{
    int len = build_response(buff, buff_size, status, status_text, content_type, body);

    if (len < 0 || (size_t)len >= buff_size)
    {
        log_write(LL_ERROR, LC_SHOW_PRINTF, "send_response > response truncated\n");
        return -1;
    }

    if (write(fd, buff, (size_t)len) < 0)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "send_response > write");
        return -1;
    }

    return 0;
}

int open_view_file(web_view_t *view)
{
    int return_code = -1;
    char path_buff[PATH_MAX];
    snprintf(path_buff, sizeof(path_buff), "%s%s", VIEW_FILE_PATH, view->path);
    FILE *fp = fopen(path_buff, "rb");
    if (fp == NULL)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "open_view_file > File not exist Or Error");
        goto clear;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size < 0)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "open_view_file > fseek");
        goto clear;
    }

    char *buff = malloc(size + 1);
    if (!buff)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "open_view_file > malloc");
        goto clear;
    }

    size_t n = fread(buff, 1, size, fp);
    buff[n] = '\0';
    view->body = buff;
    view->body_len = n;

    return_code = 0;
clear:
    if(fp) 
        fclose(fp);
    return return_code;
}

// 요청 완성 판정: 1 완성, 0 미완, -1 버퍼 초과
int is_request_complete(const char *buf, size_t total)
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

void handle_http_request(web_conn_t *c)
{
    char response_buff[32768];   // todo 동적할당
    http_req_cus_t req_info;
    // printf("---- RAW REQUEST ----\n%s\n---------------------\n", c->rbuf);

    if (parse_request(c->rbuf, &req_info))
        return;

    // printf("=====================\n%s | %s | sid=[%s]\n", req_info.method, req_info.path, req_info.cookie_sid);
    // if (req_info.body)
        // printf("%s\n", req_info.body);

    // ---- [비인증 허용] POST /login ----
    if (strcmp(req_info.method, "POST") == 0 && strcmp(req_info.path, "login") == 0)
    {
        char id[32] = {0};
        char pw[32] = {0};

        if (req_info.body == NULL ||
            parse_form_value(req_info.body, "id", id, sizeof(id)) < 0 ||
            parse_form_value(req_info.body, "pw", pw, sizeof(pw)) < 0)
        {
            send_response(c->handle.fd, response_buff, sizeof(response_buff), 400, "Bad Request", "text/plain", "missing id or pw");
            return;
        }

        // 계정 검증 (테스트 단계: 하드코딩)
        if (strcmp(id, "admin") != 0 || strcmp(pw, "farm1234") != 0)
        {
            send_response(c->handle.fd, response_buff, sizeof(response_buff), 401, "Unauthorized", "text/plain", "login failed");
            return;
        }

        session_cus_t *s = session_create(id);
        if (s == NULL)
        {
            send_response(c->handle.fd, response_buff, sizeof(response_buff), 500, "Internal Server Error", "text/plain", "session full");
            return;
        }

        // 쿠키로 sid 전달
        char cookie_hdr[160];
        snprintf(cookie_hdr, sizeof(cookie_hdr), "Set-Cookie: sid=%s; HttpOnly; Path=/\r\n", s->sid);
        send_redirect(c->handle.fd, response_buff, sizeof(response_buff), "/main.html", cookie_hdr);   // 메인페이지로 리다이렉트
        // printf("[LOGIN] user=%s sid=%.8s...\n", s->user, s->sid);
        return;
    }

    // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< ---- 인증 게이트 ---- >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
    session_cus_t *sess = session_find(req_info.cookie_sid);
    if (sess == NULL && strcmp(req_info.path, "login.html") != 0)
    {
        send_redirect(c->handle.fd, response_buff, sizeof(response_buff), "/login.html", NULL);    // 미인증자 로그인 페이지로 다시 보냄
        return;
    }

    // ---- [인증 필요] GET /logout ----
    if (strcmp(req_info.method, "GET") == 0 && strcmp(req_info.path, "logout") == 0)
    {
        // printf("[LOGOUT] user=%s\n", sess->user);
        session_destroy(req_info.cookie_sid);

        send_redirect(c->handle.fd, response_buff, sizeof(response_buff), "/login.html", "Set-Cookie: sid=; Max-Age=0; Path=/\r\n");
        return;
    }

    // ---- [인증 필요] GET /api/sensor ----
    if (strcmp(req_info.method, "GET") == 0 && strcmp(req_info.path, "api/sensor") == 0)
    {
        frame_stm32_t g_state = {0};
        get_stm32_value(&g_state);
        char res[256] = {0};
        char timestamp[TM_BUFF_LEN] = {0};
        get_stm32_last_rcv_tm(timestamp);
        snprintf(res, sizeof(res), "{\"temp\":%d.%02d,\"humi\":%d.%02d,\"light\":%d,\"timestamp\":\"%s\", \"stm32_stat\":1}",   // todo : 와치독 만들면 연결
            g_state.temp / 100, 
            g_state.temp % 100,
            g_state.humi / 100, 
            g_state.humi % 100,
            g_state.light,
            timestamp
        );
        send_response(c->handle.fd, response_buff, sizeof(response_buff), 200, "OK", "application/json", res);
       
        return;
    }

    // ---- [인증 필요] POST /api/water ----
    if (strcmp(req_info.method, "POST") == 0 && strcmp(req_info.path, "api/water") == 0)
    {
        char sec_str[8] = {0};
        int  sec = 5;

        if (req_info.body && parse_form_value(req_info.body, "sec", sec_str, sizeof(sec_str)) == 0)
            sec = atoi(sec_str);

        if (sec < 1 || sec > WATER_MAX_SEC)
        {
            send_response(c->handle.fd, response_buff, sizeof(response_buff),400, 
                "Bad Request", "application/json", "{\"ok\":0,\"msg\":\"invalid sec\"}");
            return;
        }

        if (send_water_cmd(sec) < 0)
        {
            send_response(c->handle.fd, response_buff, sizeof(response_buff), 500, 
                "Internal Server Error", "application/json", "{\"ok\":0,\"msg\":\"uart write failed\"}");
            return;
        }

        char res[64] = {0};
        snprintf(res, sizeof(res), "{\"ok\":1,\"sec\":%d}", sec);
        send_response(c->handle.fd, response_buff, sizeof(response_buff), 200, "OK", "application/json", res);
        return;
    }

    // ---- [인증 필요] view 서빙 ----
    for (int i = 0; i < VIEW_COUNT; i++)
    {
        if (views[i].body && !strcmp(views[i].path, req_info.path))
        {
            send_response(c->handle.fd, response_buff, sizeof(response_buff), 200, "OK", "text/html; charset=utf-8", views[i].body);
            return;
        }
    }

    // ---- 매칭 실패: 404 ----
    send_response(c->handle.fd, response_buff, sizeof(response_buff), 404, "Not Found", "text/plain", "not found");
}

int ready_webserver(int *listen_fd_out, int *timer_fd_out)
{
    int listen_fd = -1, timer_fd = -1;

    // ================================================================= VIEW
    const char *VIEW_NAMES[] =
    {
        "main.html", "login.html"
    };
    int view_names_len = sizeof(VIEW_NAMES) / sizeof(VIEW_NAMES[0]);
    if (view_names_len > VIEW_COUNT)
    { 
        log_write(LL_ERROR, LC_SHOW_PRINTF, "ready_webserver > too many view files\n");
        goto err;
    }
    for (int i = 0; i < view_names_len; i++)
    {
        strcpy(views[i].path, VIEW_NAMES[i]);

        int res = open_view_file(&views[i]);
        if (res)
        {
            memset(&views[i], 0, sizeof(views[i]));
            continue;
        }
        char tmpBuff[1024] = {0};
        snprintf(tmpBuff, sizeof(tmpBuff), "opened View >======>> %s\n", views[i].path);
        log_write(LL_INFO, LC_SHOW_PRINTF, tmpBuff);
    }
    // ================================================================= 타이머
    // 세션 만료 검사 주기 실행용 타이머
    timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC); // 모노토닉(부팅후 계속 증가), 논블로킹 | exec일떄 close
    if (timer_fd < 0)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "ready_webserver > timerfd_create");
        goto err;
    }
    struct itimerspec its =
    {
        .it_interval = { .tv_sec = EXPIRE_INTERVAL_SEC },   // 10초 인터벌
        .it_value    = { .tv_sec = EXPIRE_INTERVAL_SEC },   // 10초 뒤에 발화
    };
    if (timerfd_settime(timer_fd, 0, &its, NULL) < 0)   // 타이머 시작
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "ready_webserver > timerfd_settime");
        goto err;
    }
    
    // ================================================================= 소켓
    listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (listen_fd < 0)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "ready_webserver > socket");
        goto err;
    }

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "ready_webserver > setsocketopt");
        goto err;
    }

    struct sockaddr_in addr =
    {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(WEBPORT),
    };
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "ready_webserver > bind");
        goto err;
    }
    if (listen(listen_fd, BACKLOG) < 0)
    {
        log_write(LL_ERROR, LC_SHOW_PERROR, "ready_webserver > listen");
        goto err;
    }

    *listen_fd_out = listen_fd;
    *timer_fd_out = timer_fd;
    return 0;
err:
    if(listen_fd >= 0)
        close(listen_fd);
    if(timer_fd >= 0)
        close(timer_fd);
    *listen_fd_out = -1;
    *timer_fd_out = -1;
    return -1;
}

//  세션 & 연결 만료 체크
int read_web_timer(epoll_event_handle_t *handle)
{    
    uint64_t expirations;
    read(handle->fd, &expirations, sizeof(expirations));
    session_expire_check();

    // 무응답 연결 만료 체크
    web_accept_ctx_t *actx = handle->ctx;
    time_t now = time(NULL);
    web_conn_t *ptr = g_conn_head;
    while (ptr)
    {
        web_conn_t *next = ptr->next;
        if (now - ptr->last_active > CONN_IDLE_TIMEOUT)
            web_conn_close(actx->epoll_fd, ptr);
        ptr = next;
    }

    return 0;
}

int web_accept(epoll_event_handle_t *handle)
{
    web_accept_ctx_t *actx = handle -> ctx;
    int listen_fd = handle->fd;

    while(1)
    {
        int conn_fd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC); // 논블로킹 연결 생성
        if(conn_fd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) 
                break;
            if (errno == EINTR) 
                continue;
            log_write(LL_ERROR, LC_SHOW_PERROR, "web_accept > accept4");
            break;
        }

        web_conn_t *conn = calloc(1, sizeof(*conn));
        if (!conn) 
        { 
            close(conn_fd); 
            continue; 
        }
        conn->handle.fd   = conn_fd;
        conn->handle.func = web_handle_conn;
        conn->handle.ctx  = actx;

        struct epoll_event epev = 
        {
            .events = EPOLLIN,
            .data.ptr = &conn->handle,
        };
        if (epoll_ctl(actx->epoll_fd, EPOLL_CTL_ADD, conn_fd, &epev) < 0)
        {
            close(conn_fd);
            free(conn);
            continue;
        }
        conn->next = g_conn_head;
        conn->prev = NULL;
        if(g_conn_head) g_conn_head->prev = conn;
        g_conn_head = conn;
        conn->last_active = time(NULL);
    }
    return 0;
}

/* 웹 송수신 처리 */
int web_handle_conn(epoll_event_handle_t *handle)
{
    web_accept_ctx_t *actx = handle->ctx;
    web_conn_t *conn = (web_conn_t *)handle;
    int epoll_fd = actx->epoll_fd;

    while (1)
    {
        ssize_t n = read(conn->handle.fd, conn->rbuf + conn->total, sizeof(conn->rbuf) - 1 - conn->total);
        if (n > 0)
        {
            conn->total += n;
            conn->rbuf[conn->total] = '\0';
            conn->last_active = time(NULL);
            continue;
        }
        if (n == 0) // 상대가 끊음
        {
            web_conn_close(epoll_fd, conn);
            return 0;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EINTR) continue;
        log_write(LL_ERROR, LC_SHOW_PERROR, "web_handle_conn > read");
        web_conn_close(epoll_fd, conn);
        return 0;
    }

    int done = is_request_complete(conn->rbuf, conn->total);
    if (done == 0)
        return 0; // 미완성, 연결 유지. 다음 이벤트에서 이어받음

    if (done == 1)
        handle_http_request(conn); // 라우팅 + 응답

    web_conn_close(epoll_fd, conn); // Connection: close
    return 0;
}

void web_conn_close(int epoll_fd, web_conn_t *conn)
{
    if(conn->prev)  // 중간에 낀 경우
        conn->prev->next = conn->next;
    else    // 리스트 헤더인 경우
        g_conn_head = conn->next;
    
    if(conn->next)
        conn->next->prev = conn->prev;

    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conn->handle.fd, NULL);
    close(conn->handle.fd); // accpet FD 종료
    free(conn); // 연결마다 여기서 free
}

void web_conn_close_all(int epoll_fd)
{
    while (g_conn_head)
        web_conn_close(epoll_fd, g_conn_head);
}