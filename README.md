# smartFarmEx
STM32(stm32-f411re) + Raspberry-pi(버젼 4) 스마트팜 모니터링/제어 시스템
C로 구현, Epoll 기반 단일 스레드 동작

| | |
|---|---|
| 리눅스 데몬 | smartFarmEx(현재저장소) (`farmd`) |
| STM32 펌웨어 | [smartFarmEx_stm32](https://github.com/logg9715/smartFarmEx_stm32) |

## 구현 특징
- 단일 epoll 이벤트 기반 루프 (시그널, 타이머, UART, 소켓, 다중 HTTP연결 통합)
- 함수 포인터를 활용한 동적 디스패치 (epoll FD들이 각각의 이벤트핸들러 함수와 컨텍스트를 가지고 동작)
- 자체 설계 UART 통신 프로토콜 (프레임 재조립)
- Http서버 자체 구현, 요청 파싱/라우팅/세션 수명 관리

## 1. 블럭 다이어그램
<img width="1470" height="900" alt="image" src="https://github.com/user-attachments/assets/b62432a9-aa38-479a-b790-3c38e2f5c2be" />

<br/>

| 계층 | 담당 |
|---|---|
| STM32 | 센서값 측정, 액추에이터 구동(물주기, 조명 같은 / 프로젝트에서는 워터펌프 대신 LED 동작으로 대체) |
| farmd | stm32에서 보내준 프레임 파싱, 웹 콘솔 관리, OLED제어, 프로세스 로그 기록 |

=> 액추에이터 동작 차단 타이머는 STM32에 배치, AP가 멈추거나 통신이 끊겨도 동작 타이머가 만료되면 액추에이터가 정지한다. 

## 2. 이벤트 루프
| FD | 핸들러 함수 | 역할 상세|
|---|---|---|
| signal_fd | finish_loop | -SIGTERM -SIGINT감지하여 프로세스 정상 종료 |
| UART | read_uart_stm32 | stm32가 보낸 프레임(센서 정보) 수신&파싱 |
| timer_fd(웹서버) | read_web_timer | 세션 수명 관리, 무응답 연결 회수 |
| timer_fd(OLED) | oled_handle | 디스플레이 정보 갱신 |
| listen(소켓) | web_accept | 웹 연결 수립 |
| conn(소켓) | web_handle_conn | 웹 HTTP 연결 요청/응답 |

### 2.1 epoll의 동적 디스패치
``` C
// epoll 이벤트 공용 구조체
typedef struct epoll_event_handle epoll_event_handle_t;
struct epoll_event_handle {
    int   fd;
    int (*func)(epoll_event_handle_t *self);
    void *ctx;
};
```
``` C
// if 분기 없이, 감지된 이벤트 목록에서 핸들러로 연결됨
epoll_event_handle_t *e = event_list[i].data.ptr;
e->func(e);
```
| 구분 | 대상 | 수명주기 |
|---|---|---|
| 고정 FD | 시그널, UART, 타이머, listen | 프로세스 수명, 종료 시 일괄 회수 |
| 동적 FD | HTTP 연결 | 힙 할당, 각자 개별 해제 |

### 2.2 논 블로킹
 
| 구분 | 설정 |
|---|---|
| 소켓 | `accept4(SOCK_NONBLOCK \| SOCK_CLOEXEC)` |
| UART | `open(O_NONBLOCK)` |
| 타이머 | `timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK \| TFD_CLOEXEC)` |

— 등록 fd 중 하나라도 블로킹이면 루프 전체가 정지함.
- `accept4` 는 플래그를 원자적으로 설정해 `fcntl` 경쟁을 제거
- `sigaction` 대신 `signalfd` 를 쓴 것은 `epoll_wait` 진입 직전에 도착한 시그널을 놓치지 않기 위함.


## 3. HTTP Server
 
### 3.1 API
 
| Method | Path | 로그인 여부 | Response |
|---|---|---|---|
| POST | `/login` | — | 302 → `/main.html` + `Set-Cookie` |
| GET | `/logout` | 필요 | 302 → `/login.html`, 쿠키 만료 |
| GET | `/api/sensor` | 필요 | `{temp, humi, light, timestamp, stm32_stat}` |
| POST | `/api/water` | 필요 | `{ok, sec}` — 파라미터 `sec` |
| GET | `/*.html` | 필요 | 정적 페이지 |
 
### 3.2 상세 정보
 
| 항목 | 값 |
|---|---|
| 프로토콜 | HTTP/1.1, `Connection: close` |
| 요청 버퍼 | 4096 |
| 완성 판정 | `\r\n\r\n` + `Content-Length` |
| 세션 ID | 256비트 (`getrandom`), hex 64자 |
| 세션 저장 | 서버 테이블, 쿠키는 sid만 (`HttpOnly`) |
| 세션 만료 | 우선은 30분, 5초 주기 검사 |
| 연결 유휴 타임아웃 | 30초 |
| 대시보드 데이터 폴링 | 2초 |

- 센서 갱신 주기가 초 단위라 폴링에서 푸시로 바꿔도 이득이 적음

## 4. UART

### 4.1 UART 프레임 레이아웃
<img width="1470" height="900" alt="image" src="https://github.com/user-attachments/assets/0f081fe1-963d-4020-9775-be2c6599315b" />

### 4.2 프레임 재조립
| 단계 | 동작 | 실패 시 |
|---|---|---|
| 1 | `0x02` 탐색, 찾으면 버퍼 앞으로 밀어버림 | 쓰레기값 폐기 |
| 2 | LEN == `sizeof(payload)` 검증 | 틀린 경우, 해당 바이트 폐기 후 재탐색 |
| 3 | LEN+5 바이트 도착 대기 | 대기 |
| 4 | 페이로드 복사 | — |


 ## 5. OLED (SSD1306)
 
| 항목 | 값 |
|---|---|
| 인터페이스 | `/dev/i2c-1`, `ioctl(I2C_SLAVE, 0x3C)` |
| 해상도 | 128 × 64, 1bpp |
| 갱신 | 전용 `timerfd` 주기 |
 
wiringPi 등 GPIO/디스플레이 라이브러리 미사용. 초기화 커맨드 시퀀스를 데이터시트 기준으로 직접 전송하고,
페이지/컬럼 주소 지정 방식으로 프레임버퍼를 전송한다.
> TODO : 디바이스 드라이버로 초기화 작업 자동화

## 6. 펌웨어 (STM32F411)

STM32F411. `HAL_GetTick()` 기반 스케줄러
- 센서 송신과 액추에이터 동작을 동시 진행시킴 (명령은 UART 인터럽트로 수신하되, ISR은 플래그만 세우고 실행은 메인 루프가 담당)
- AP는 fd 이벤트를 `epoll` 로, STM32는 시간 이벤트를 틱 비교로 다중화, 둘 다 논블로킹 운영 수행

상세: [smartFarmEx_stm32](https://github.com/logg9715/smartFarmEx_stm32)
 
## 7. 빌드 방법
 
```bash
make            # bin/farmd
./bin/farmd     # 웹 포트 :8080
```
의존성: 표준 C 라이브러리
 
**Config 파일** — `include/config.h`
 
| 이름 | 기본값 |
|---|---|
| `WEBPORT` | 8080 |
| `UART_DEVICE` | `/dev/serial0` |
| `UART_SPEED` | `B115200` (펌웨어도 같이 수정해야함) |
| `SESSION_TIMEOUT_SEC` | 1800 |
| `EXPIRE_INTERVAL_SEC` | 5 (세션 검사주기) |
| `WATER_MAX_SEC` | 30 (물주기시간 상한) |
| `LOG_LEVEL` | `LL_ERROR` (N 이상의 심각도 로그만 표출) |
 
## 8. Source Map
 
```
src/
├── main.c              프로세스 수명 관리
├── epoll_loop.c        이벤트 루프, 핸들러 등록/회수
├── sig_handler.c       signalfd 시그널 처리
├── uart/
│   ├── uart.c          termios 설정, 수신, 명령 송신
│   └── uart_parser.c   프레임 관련
├── webserver/
│   └── web_server.c    HTTP 파싱, 라우팅, 세션, 연결 관리
├── oled/oled.c         OLED 제어, OLED 프레임버퍼 렌더링
└── util/               로거, 유틸함수
 
include/    헤더
html/       정적 페이지 (빌드 파일과 같은 위치로 넣으면 보임=기본값 설정)
expr/       테스트 코드
```
 
## 9. TODO list

 - 프레임 무결성 / STX랑 LEN까지만 검증중
   ㄴ ETX나 TYPE 등 검증 추가
- 명령 실행 확인 / UART 명령 전송 성공까지만 확인중
  ㄴ 상태 프레임에 액추에이터 상태 포함해야됨
- 웹 인증 / 관리자계정 하드코딩 상태
  ㄴ 환경설정에 관리자 메뉴 추가해야됨
 
## 10. 시스템 인터페이스

| 분류 | 인터페이스 | 용도 |
|---|---|---|
| I/O 다중화 | `epoll_create1` `epoll_ctl` `epoll_wait` | 이벤트 루프 |
| 시그널 | `sigprocmask` `signalfd` | 시그널을 fd로 수신, 유실 방지 |
| 타이머 | `timerfd_create` `timerfd_settime` | 주기 작업을 fd 이벤트로 |
| 소켓 | `socket` `bind` `listen` `accept4` `setsockopt` | TCP 서버, 논블로킹 연결 수립 |
| 난수 | `getrandom` | 세션 ID 생성 |
| 시리얼 | `termios` `cfmakeraw` `tcsetattr` | UART 회선 설정 |
| I2C | `open` `ioctl(I2C_SLAVE)` | OLED 직접 제어 |
| 동기화 | `pthread_mutex` | 로거 잠금 |

