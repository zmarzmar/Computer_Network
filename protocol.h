#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <signal.h>

#define MAX_SEQ 1
#define MAX_PKT 256
#define TIMEOUT_MS 2000
#define PACE_DEFAULT_MS 1500
#define PACE_MIN_MS 200
#define PACE_STEP_MS 300

#define inc(k) (k = (k + 1) % (MAX_SEQ + 1))

typedef int seq_nr;
typedef enum { frame_arrival, cksum_err, timeout } event_type;

typedef struct { char data[MAX_PKT]; } packet;

typedef struct {
    seq_nr seq;
    seq_nr ack;
    packet info;
} frame;

static int             g_sock = -1;
static struct sockaddr_in g_peer;
static char            g_name = '?';

static int             g_timer_running = 0;
static struct timeval  g_timer_deadline;

static int             g_paused = 0;
static long            g_timer_saved = 0;
static struct termios  g_orig_termios;
static int             g_step_mode = 0;
static int             g_pace_ms = PACE_DEFAULT_MS;

static long ms_until(struct timeval deadline) {
    struct timeval now;
    gettimeofday(&now, NULL);
    long ms = (deadline.tv_sec - now.tv_sec) * 1000L
            + (deadline.tv_usec - now.tv_usec) / 1000L;
    return ms;
}

static void init_network(char name, int my_port, int peer_port) {
    g_name = name;
    g_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sock < 0) { perror("socket"); exit(1); }

    struct sockaddr_in me;
    memset(&me, 0, sizeof(me));
    me.sin_family = AF_INET;
    me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    me.sin_port = htons(my_port);
    if (bind(g_sock, (struct sockaddr *)&me, sizeof(me)) < 0) {
        perror("bind"); exit(1);
    }

    memset(&g_peer, 0, sizeof(g_peer));
    g_peer.sin_family = AF_INET;
    g_peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    g_peer.sin_port = htons(peer_port);

    printf("[%c] up: my_port=%d -> peer_port=%d\n", name, my_port, peer_port);
}

static int g_out_seq = 0;
static void from_network_layer(packet *p) {
    snprintf(p->data, MAX_PKT, "MSG#%d from %c", g_out_seq++, g_name);
}
static void to_network_layer(packet *p) {
    printf("[%c] -> network layer 전달: \"%s\"\n", g_name, p->data);
}

static void handle_key(char c);
static void pace_gate(void) {
    if (g_step_mode) {
        printf("[%c]   …다음 전송:[Enter/Space]  자동:[m]  종료:[q]\n", g_name);
        fflush(stdout);
        for (;;) {
            char c;
            if (read(STDIN_FILENO, &c, 1) <= 0) continue;
            if (c == 'q' || c == 'Q') { printf("[%c] 종료.\n", g_name); exit(0); }
            if (c == 'm' || c == 'M') {
                g_step_mode = 0;
                printf("[%c] ▶ 자동 모드 (간격 %dms)\n", g_name, g_pace_ms);
                return;
            }
            if (c == '\n' || c == '\r' || c == ' ') return;
        }
    } else {
        struct timeval tv;
        tv.tv_sec  = g_pace_ms / 1000;
        tv.tv_usec = (g_pace_ms % 1000) * 1000;
        fd_set r; FD_ZERO(&r); FD_SET(STDIN_FILENO, &r);
        if (select(STDIN_FILENO + 1, &r, NULL, NULL, &tv) > 0
                && FD_ISSET(STDIN_FILENO, &r)) {
            char c;
            if (read(STDIN_FILENO, &c, 1) > 0) handle_key(c);
        }
    }
}

static void to_physical_layer(frame *s) {
    pace_gate();
    sendto(g_sock, s, sizeof(*s), 0,
           (struct sockaddr *)&g_peer, sizeof(g_peer));
    printf("[%c] send  frame: seq=%d ack=%d info=\"%s\"\n",
           g_name, s->seq, s->ack, s->info.data);
}
static void from_physical_layer(frame *r) {
    recvfrom(g_sock, r, sizeof(*r), 0, NULL, NULL);
    printf("[%c] recv  frame: seq=%d ack=%d info=\"%s\"\n",
           g_name, r->seq, r->ack, r->info.data);
}

static void start_timer(seq_nr k) {
    (void)k;
    struct timeval now;
    gettimeofday(&now, NULL);
    g_timer_deadline = now;
    g_timer_deadline.tv_sec  += TIMEOUT_MS / 1000;
    g_timer_deadline.tv_usec += (TIMEOUT_MS % 1000) * 1000;
    if (g_timer_deadline.tv_usec >= 1000000) {
        g_timer_deadline.tv_sec++;
        g_timer_deadline.tv_usec -= 1000000;
    }
    g_timer_running = 1;
}
static void stop_timer(seq_nr k) {
    (void)k;
    g_timer_running = 0;
}

static void restore_termios(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
}
static void on_sigint(int s) { (void)s; exit(0); }
static void enable_keyboard(void) {
    tcgetattr(STDIN_FILENO, &g_orig_termios);
    atexit(restore_termios);
    signal(SIGINT, on_sigint);
    struct termios t = g_orig_termios;
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN]  = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

static void wait_for_start(void) {
    printf("[%c] 준비됨.  시작:[s/Enter]   종료:[q]\n", g_name);
    fflush(stdout);
    for (;;) {
        char c;
        if (read(STDIN_FILENO, &c, 1) <= 0) continue;
        if (c == 'q' || c == 'Q') { printf("[%c] 종료.\n", g_name); exit(0); }
        if (c == 's' || c == 'S' || c == '\n' || c == '\r' || c == ' ') {
            printf("[%c] ▶ 시작!  [m]모드전환  [+/-]속도  [p]일시정지  [q]종료\n",
                   g_name);
            return;
        }
    }
}

static void handle_key(char c) {
    if (c == 'q' || c == 'Q') {
        printf("\n[%c] 종료합니다.\n", g_name);
        exit(0);
    }
    if (c == 'm' || c == 'M') {
        g_step_mode = 1;
        printf("\n[%c] ⏭ 수동(스텝) 모드: 전송할 때마다 [Enter/Space]\n", g_name);
        return;
    }
    if (c == '+' || c == '=') {
        g_pace_ms -= PACE_STEP_MS;
        if (g_pace_ms < PACE_MIN_MS) g_pace_ms = PACE_MIN_MS;
        printf("[%c] 속도: 간격 %dms\n", g_name, g_pace_ms);
        return;
    }
    if (c == '-' || c == '_') {
        g_pace_ms += PACE_STEP_MS;
        printf("[%c] 속도: 간격 %dms\n", g_name, g_pace_ms);
        return;
    }
    if (c == 'p' || c == 'P') {
        if (!g_paused) {
            g_timer_saved = g_timer_running ? ms_until(g_timer_deadline) : 0;
            if (g_timer_saved < 0) g_timer_saved = 0;
            g_paused = 1;
            printf("\n[%c] ⏸ 일시정지  (재개:[p/space]  종료:[q])\n", g_name);
        } else {
            g_paused = 0;
            if (g_timer_running) {
                gettimeofday(&g_timer_deadline, NULL);
                g_timer_deadline.tv_sec  += g_timer_saved / 1000;
                g_timer_deadline.tv_usec += (g_timer_saved % 1000) * 1000;
                if (g_timer_deadline.tv_usec >= 1000000) {
                    g_timer_deadline.tv_sec++;
                    g_timer_deadline.tv_usec -= 1000000;
                }
            }
            printf("[%c] ▶ 재개\n", g_name);
        }
    }
}

static void wait_for_event(event_type *event) {
    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        int maxfd = STDIN_FILENO;
        if (!g_paused) {
            FD_SET(g_sock, &rfds);
            if (g_sock > maxfd) maxfd = g_sock;
        }

        struct timeval tv, *ptv = NULL;
        if (!g_paused && !g_step_mode && g_timer_running) {
            long rem = ms_until(g_timer_deadline);
            if (rem <= 0) { *event = timeout; return; }
            tv.tv_sec  = rem / 1000;
            tv.tv_usec = (rem % 1000) * 1000;
            ptv = &tv;
        }

        int n = select(maxfd + 1, &rfds, NULL, NULL, ptv);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("select"); exit(1);
        }
        if (n == 0) { *event = timeout; return; }
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            char c;
            if (read(STDIN_FILENO, &c, 1) > 0) handle_key(c);
            continue;
        }
        if (FD_ISSET(g_sock, &rfds)) { *event = frame_arrival; return; }
    }
}

#endif
