#ifndef COMMON_H
#define COMMON_H

#include <signal.h>

/*
 * User App -> Firmware 시그널 프로토콜
 *
 * 기본: 실시간 시그널 하나(SIG_CMD)에 명령 번호를 실어 sigqueue()로 보낸다.
 *   - 표준 시그널은 같은 시그널이 여러 번 오면 하나로 합쳐진다   (README 4-B)
 *   - 서로 다른 시그널은 보낸 순서가 아니라 번호 순서로 전달된다 (README 4-C)
 *   같은 실시간 시그널 하나만 쓰면 큐잉되고 보낸 순서(FIFO)도 지켜진다.
 *
 * Firmware는 셸에서 kill로 보내기 쉬운 SIGUSR1(추가), SIGUSR2(제거),
 * SIGTERM/SIGINT(종료)도 함께 받는다. User App의 -l 모드가 이 방식이다.
 */
#define SIG_CMD  (SIGRTMIN)

enum {
    CMD_ADD  = 1,   /* 채굴기 추가 */
    CMD_DEL  = 2,   /* 가장 오래된 채굴기 제거 */
    CMD_QUIT = 3,   /* 정상 종료 */
    CMD_SHOW = 4,   /* 상태 출력 */
    CMD_HANG = 5,   /* 메인 루프 멈춤 (WDT 테스트용) */
};

#endif
