#include "protocol.h"

static void protocol4(void) {
    seq_nr next_frame_to_send;
    seq_nr frame_expected;
    frame  r, s;
    packet buffer;
    event_type event;

    next_frame_to_send = 0;
    frame_expected     = 0;
    from_network_layer(&buffer);
    s.info = buffer;
    s.seq  = next_frame_to_send;
    s.ack  = 1 - frame_expected;
    to_physical_layer(&s);
    start_timer(s.seq);

    while (1) {
        wait_for_event(&event);

        if (event == frame_arrival) {
            from_physical_layer(&r);

            if (r.seq == frame_expected) {
                to_network_layer(&r.info);
                inc(frame_expected);
            }

            if (r.ack == next_frame_to_send) {
                stop_timer(r.ack);
                from_network_layer(&buffer);
                inc(next_frame_to_send);
            }
        }

        s.info = buffer;
        s.seq  = next_frame_to_send;
        s.ack  = 1 - frame_expected;
        to_physical_layer(&s);
        start_timer(s.seq);
    }
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <name A/B> <my_port> <peer_port>\n", argv[0]);
        return 1;
    }
    char name = argv[1][0];
    int  my_port   = atoi(argv[2]);
    int  peer_port = atoi(argv[3]);

    setvbuf(stdout, NULL, _IOLBF, 0);
    init_network(name, my_port, peer_port);
    enable_keyboard();
    wait_for_start();
    protocol4();
    return 0;
}
