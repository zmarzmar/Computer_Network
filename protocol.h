#ifndef PROTOCOL_H   // 헤더 중복 포함 방지: PROTOCOL_H가 아직 정의 안 됐을 때만 아래를 컴파일
#define PROTOCOL_H   // PROTOCOL_H를 정의 -> 다음번 include부터는 통째로 건너뜀

/* =====================================================================
 * Part 1 — 정의부 (헤더 · 상수 · 자료형)
 * 시스템 헤더, 프로토콜 상수, 프레임/패킷/이벤트 자료형 정의
 * ===================================================================== */

#include <stdio.h>      // printf, snprintf, perror 등 입출력 함수를 쓰기 위해
#include <stdlib.h>     // exit, atoi 함수를 쓰기 위해
#include <string.h>     // memset 함수를 쓰기 위해
#include <unistd.h>     // usleep 등 POSIX 함수를 쓰기 위해
#include <errno.h>      // 오류 번호 변수 errno를 쓰기 위해
#include <arpa/inet.h>  // sockaddr_in, htons, htonl 등 주소 변환을 쓰기 위해
#include <sys/socket.h> // socket, bind, sendto, recvfrom 소켓 함수를 쓰기 위해
#include <sys/select.h> // select, fd_set 등 이벤트 대기 기능을 쓰기 위해
#include <sys/time.h>   // timeval 구조체, gettimeofday 시간 함수를 쓰기 위해

#define MAX_SEQ 1       // 시퀀스 번호 최대값. 1비트 프로토콜이라 0~1 범위 -> 1
#define MAX_PKT 256     // 패킷 데이터의 최대 크기(바이트)
#define TIMEOUT_MS 2000 // 재전송 타임아웃 시간(2000ms = 2초)
#define PACE_MS 800     // 전송 간격(800ms). 화면에서 천천히 보이도록 일부러 둔 지연

#define inc(k) (k = (k + 1) % (MAX_SEQ + 1)) // 시퀀스 번호를 0<->1로 토글하는 매크로

typedef int seq_nr; // 시퀀스 번호 타입(int에 별칭을 붙여 가독성 ↑)
typedef enum { frame_arrival, cksum_err, timeout } event_type; // 발생 가능한 이벤트 3종

typedef struct { char data[MAX_PKT]; } packet; // 상위 계층이 주고받는 데이터 덩어리(패킷)

typedef struct {    // 실제로 소켓에 실어 보내는 전송 단위(프레임)
    seq_nr seq;     // 이 프레임의 송신 시퀀스 번호(0 또는 1)
    seq_nr ack;     // 피기백 확인응답: 직전에 받은 프레임 번호를 함께 실어 보냄
    packet info;    // 실제로 운반하는 데이터(패킷)
} frame;

/* =====================================================================
 * Part 2 — 초기화 (소켓 생성 · bind)
 * 전역 상태와 ms_until 유틸, UDP 소켓 생성 후 내 포트에 bind
 * ===================================================================== */

static int             g_sock = -1;          // 이 프로세스가 사용할 UDP 소켓 번호
static struct sockaddr_in g_peer;            // 상대(목적지)의 IP+포트를 저장
static char            g_name = '?';         // 로그에 찍을 노드 이름(A 또는 B)
static int             g_timer_running = 0;   // 타이머가 동작 중인지 여부(1=동작)
static struct timeval  g_timer_deadline;     // 타이머가 만료되는 시각

static long ms_until(struct timeval deadline) { // 지정한 시각까지 남은 시간(ms) 계산
    struct timeval now;                         // 현재 시각을 담을 변수
    gettimeofday(&now, NULL);                   // 현재 시각을 now에 가져옴
    return (deadline.tv_sec - now.tv_sec) * 1000L      // 초 차이를 ms로 환산
         + (deadline.tv_usec - now.tv_usec) / 1000L;   // 마이크로초 차이를 ms로 더함
}

static void init_network(char name, int my_port, int peer_port) { // 소켓 초기 설정
    g_name = name;                              // 노드 이름 저장
    g_sock = socket(AF_INET, SOCK_DGRAM, 0);    // IPv4 + UDP(SOCK_DGRAM) 소켓 생성
    if (g_sock < 0) { perror("socket"); exit(1); } // 생성 실패 시 오류 출력 후 종료

    struct sockaddr_in me;                      // 내 주소 정보를 담을 구조체
    memset(&me, 0, sizeof(me));                 // 구조체를 0으로 초기화
    me.sin_family = AF_INET;                    // 주소 종류: IPv4
    me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);// 내 IP = 127.0.0.1(같은 머신)
    me.sin_port = htons(my_port);               // 내 포트 지정(네트워크 바이트 순서로 변환)
    if (bind(g_sock, (struct sockaddr *)&me, sizeof(me)) < 0) { // 소켓을 내 포트에 묶음
        perror("bind"); exit(1);                // 실패 시 오류 출력 후 종료
    }

    memset(&g_peer, 0, sizeof(g_peer));         // 상대 주소 구조체 0으로 초기화
    g_peer.sin_family = AF_INET;                // 주소 종류: IPv4
    g_peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 상대 IP = 127.0.0.1(같은 머신)
    g_peer.sin_port = htons(peer_port);         // 상대 포트 지정

    printf("[%c] up: my_port=%d -> peer_port=%d\n", name, my_port, peer_port); // 시작 로그
}

/* =====================================================================
 * Part 3 — 통신 (네트워크 계층 · 물리 계층)
 * 상위 계층과 패킷을 주고받고, sendto/recvfrom 으로 프레임 송수신
 * ===================================================================== */

static int g_out_seq = 0;                       // 보낼 메시지에 붙일 일련번호 카운터
static void from_network_layer(packet *p) {     // 상위 계층에서 보낼 패킷을 가져옴(시뮬레이션)
    snprintf(p->data, MAX_PKT, "MSG#%d from %c", g_out_seq++, g_name); // 더미 메시지 생성
}
static void to_network_layer(packet *p) {       // 받은 패킷을 상위 계층으로 전달(시뮬레이션)
    printf("[%c] -> network layer 전달: \"%s\"\n", g_name, p->data); // 전달 사실을 로그로 출력
}

static void to_physical_layer(frame *s) {       // 프레임을 물리 계층(소켓)으로 전송
    usleep(PACE_MS * 1000);                     // 화면에서 천천히 보이도록 잠시 대기
    sendto(g_sock, s, sizeof(*s), 0,            // 내 소켓으로 프레임 s를 전송
           (struct sockaddr *)&g_peer, sizeof(g_peer)); // 목적지 = 상대(g_peer)
    printf("[%c] send  frame: seq=%d ack=%d info=\"%s\"\n", // 전송 내용을 로그로 출력
           g_name, s->seq, s->ack, s->info.data);
}

static void from_physical_layer(frame *r) {     // 물리 계층(소켓)에서 프레임을 수신
    recvfrom(g_sock, r, sizeof(*r), 0, NULL, NULL); // 소켓에서 프레임을 받아 r에 저장
    printf("[%c] recv  frame: seq=%d ack=%d info=\"%s\"\n", // 수신 내용을 로그로 출력
           g_name, r->seq, r->ack, r->info.data);
}

/* =====================================================================
 * Part 4 — 타이머 & 이벤트 대기
 * 재전송용 소프트웨어 타이머와, select 로 프레임 도착·타임아웃 동시 대기
 * ===================================================================== */

static void start_timer(seq_nr k) {             // 재전송 타이머 시작
    (void)k;                                    // 인자 k는 안 쓰지만 교재 형식 유지(경고 방지)
    gettimeofday(&g_timer_deadline, NULL);      // 현재 시각을 만료시각 기준점으로 잡음
    g_timer_deadline.tv_sec  += TIMEOUT_MS / 1000;      // 만료시각에 초 단위 타임아웃 더함
    g_timer_deadline.tv_usec += (TIMEOUT_MS % 1000) * 1000; // 나머지를 마이크로초로 더함
    if (g_timer_deadline.tv_usec >= 1000000) {  // 마이크로초가 100만(=1초)을 넘으면
        g_timer_deadline.tv_sec++;              // 초를 1 올리고
        g_timer_deadline.tv_usec -= 1000000;    // 마이크로초에서 1초만큼 뺌(자릿수 정리)
    }
    g_timer_running = 1;                         // 타이머 동작 중으로 표시
}

static void stop_timer(seq_nr k) {              // 타이머 정지(ack를 잘 받았을 때 호출)
    (void)k;                                    // 인자 미사용(경고 방지)
    g_timer_running = 0;                         // 타이머 꺼짐으로 표시
}

static void wait_for_event(event_type *event) { // 프레임 도착 또는 타임아웃을 기다림
    for (;;) {                                   // 이벤트가 정해질 때까지 반복
        fd_set rfds;                             // select가 감시할 소켓 집합
        FD_ZERO(&rfds);                          // 집합을 비움
        FD_SET(g_sock, &rfds);                   // 내 소켓을 감시 대상에 추가

        struct timeval tv, *ptv = NULL;          // 대기 제한시간(NULL이면 무한 대기)
        if (g_timer_running) {                    // 타이머가 켜져 있으면
            long rem = ms_until(g_timer_deadline);// 만료까지 남은 시간(ms) 계산
            if (rem <= 0) { *event = timeout; return; } // 이미 지났으면 즉시 타임아웃
            tv.tv_sec  = rem / 1000;             // 남은 시간의 초 부분
            tv.tv_usec = (rem % 1000) * 1000;    // 남은 시간의 마이크로초 부분
            ptv = &tv;                           // 이만큼만 기다리도록 설정
        }

        int n = select(g_sock + 1, &rfds, NULL, NULL, ptv); // 소켓 수신/타임아웃 대기
        if (n < 0) {                             // select가 오류를 반환하면
            if (errno == EINTR) continue;        // 신호로 인한 중단이면 다시 시도
            perror("select"); exit(1);           // 그 외 오류는 종료
        }
        if (n == 0) { *event = timeout; return; }// 0이면 시간 만료 -> 타임아웃 이벤트
        if (FD_ISSET(g_sock, &rfds)) { *event = frame_arrival; return; } // 소켓에 데이터 도착
    }
}

#endif // 헤더 가드 끝(맨 위 #ifndef와 짝을 이룸)
