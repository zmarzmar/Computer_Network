#include "protocol.h" // 소켓/타이머/이벤트 등 하부 기능을 모아둔 우리 헤더를 포함

// 교재 Figure 3-14의 Protocol 4 본체. 양방향 1비트 슬라이딩 윈도우 프로토콜.
static void protocol4(void) {
    seq_nr next_frame_to_send;  // 다음에 보낼 프레임의 시퀀스 번호(0 또는 1)
    seq_nr frame_expected;      // 다음에 받을 거라고 기대하는 시퀀스 번호
    frame  r, s;                // r=수신용 프레임, s=송신용 프레임
    packet buffer;              // 현재 보내는 중인 패킷(ack 받기 전까지 보관)
    event_type event;           // wait_for_event가 알려줄 이벤트 종류

    next_frame_to_send = 0;     // 첫 송신 시퀀스는 0
    frame_expected     = 0;     // 첫 기대 수신 시퀀스도 0
    from_network_layer(&buffer);// 상위 계층에서 첫 패킷을 가져옴
    s.info = buffer;            // 보낼 프레임에 패킷을 담음
    s.seq  = next_frame_to_send;// 프레임에 송신 시퀀스 번호 기입
    s.ack  = 1 - frame_expected;// 피기백 ack(아직 받은 게 없어 1-0=1)
    to_physical_layer(&s);      // 첫 프레임 전송
    start_timer(s.seq);         // 분실 대비 재전송 타이머 시작

    while (1) {                 // 메인 루프: 이벤트를 계속 처리
        wait_for_event(&event); // 프레임 도착 또는 타임아웃이 생길 때까지 대기

        if (event == frame_arrival) {  // 프레임이 도착한 경우
            from_physical_layer(&r);   // 소켓에서 프레임을 받아 r에 저장

            if (r.seq == frame_expected) { // 기대하던 순서의 프레임이면(중복 아님)
                to_network_layer(&r.info); // 데이터를 상위 계층으로 전달
                inc(frame_expected);       // 다음 기대 시퀀스를 토글(0<->1)
            }

            if (r.ack == next_frame_to_send) { // 내가 보낸 프레임이 잘 받아졌으면(ack 일치)
                stop_timer(r.ack);             // 재전송 타이머 정지
                from_network_layer(&buffer);   // 다음 보낼 패킷을 가져옴
                inc(next_frame_to_send);       // 송신 시퀀스를 토글(0<->1)
            }
        }
        // event가 timeout이면 위 if들을 건너뛰고 아래에서 같은 프레임을 재전송

        s.info = buffer;            // 보낼 프레임 구성(같은/새 패킷)
        s.seq  = next_frame_to_send;// 송신 시퀀스 번호 기입
        s.ack  = 1 - frame_expected;// 피기백 ack = 직전에 받은 프레임 번호
        to_physical_layer(&s);      // 프레임 전송(신규 전송 또는 재전송)
        start_timer(s.seq);         // 타이머 다시 시작
    }
}

int main(int argc, char **argv) {  // 프로그램 진입점
    if (argc != 4) {               // 인자 개수 확인(이름, 내 포트, 상대 포트)
        fprintf(stderr, "usage: %s <name A/B> <my_port> <peer_port>\n", argv[0]); // 사용법 안내
        return 1;                  // 잘못된 실행이면 종료
    }
    char name = argv[1][0];        // 첫 번째 인자의 첫 글자를 노드 이름으로(A/B)
    int  my_port   = atoi(argv[2]);// 두 번째 인자를 내 포트로 변환
    int  peer_port = atoi(argv[3]);// 세 번째 인자를 상대 포트로 변환

    setvbuf(stdout, NULL, _IOLBF, 0);     // 출력을 줄 단위로 즉시 내보냄(로그 섞임 방지)
    init_network(name, my_port, peer_port);// 소켓 생성 및 주소 설정
    protocol4();                          // 프로토콜 본체 실행(무한 루프)
    return 0;                             // (정상적으로는 도달하지 않음)
}
