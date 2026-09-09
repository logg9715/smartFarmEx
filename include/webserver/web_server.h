#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <linux/limits.h>
#include "epoll_loop.h"

#define RBUF_SIZE 4096

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
    int fd;
    char rbuf[RBUF_SIZE];   // 수신 누적 버퍼
    size_t total;   // 지금까지 누적된 바이트 수
} conn_t;

typedef struct web_conn {
    epoll_event_handle_t handle;
    struct web_conn *prev, *next;
    time_t last_active;  
    char rbuf[RBUF_SIZE];
    size_t total;
} web_conn_t;

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

typedef struct { 
    int epoll_fd; 
} web_timer_ctx_t;

typedef struct { 
    int epoll_fd; 
} web_accept_ctx_t;

int build_response_ex(char *, size_t, int, const char *, const char *, const char *, const char *);
int gen_sid(char *);
void send_redirect(int, char *, size_t, const char *, const char *);
void session_destroy(const char *);
int parse_form_value(const char *, const char *, char *, size_t);
int build_response_ex(char *, size_t, int, const char *, const char *, const char *, const char *);
session_cus_t *session_create(const char *);
session_cus_t *session_find(const char *);
void session_expire_check(void);
int parse_cookie_sid(const char *, char *, size_t);
int parse_request(const char *, http_req_cus_t *);
int build_response(char *, size_t, int, const char *, const char *, const char *);
int open_view_file(web_view_t *);
int is_request_complete(const char *, size_t);
void handle_http_request(web_conn_t *);
int ready_webserver(int *, int *);
int read_web_timer(epoll_event_handle_t *);
int web_accept(epoll_event_handle_t *);
int web_handle_conn(epoll_event_handle_t *);
void web_conn_close(int, web_conn_t *);
void web_conn_close_all(int);

#endif
