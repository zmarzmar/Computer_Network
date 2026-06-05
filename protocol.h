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

#define MAX_SEQ 1
#define MAX_PKT 256
#define TIMEOUT_MS 2000
#define PACE_MS 800

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

static long ms_until(struct timeval deadline) {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (deadline.tv_sec - now.tv_sec) * 1000L
         + (deadline.tv_usec - now.tv_usec) / 1000L;
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

static void to_physical_layer(frame *s) {
    usleep(PACE_MS * 1000);
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
    gettimeofday(&g_timer_deadline, NULL);
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

static void wait_for_event(event_type *event) {
    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(g_sock, &rfds);

        struct timeval tv, *ptv = NULL;
        if (g_timer_running) {
            long rem = ms_until(g_timer_deadline);
            if (rem <= 0) { *event = timeout; return; }
            tv.tv_sec  = rem / 1000;
            tv.tv_usec = (rem % 1000) * 1000;
            ptv = &tv;
        }

        int n = select(g_sock + 1, &rfds, NULL, NULL, ptv);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("select"); exit(1);
        }
        if (n == 0) { *event = timeout; return; }
        if (FD_ISSET(g_sock, &rfds)) { *event = frame_arrival; return; }
    }
}

#endif
