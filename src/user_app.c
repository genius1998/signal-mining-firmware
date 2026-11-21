/*
 * User App: Firmware App에 시그널로 명령을 보낸다
 * 원본: original/hw7_userapp.c (PID를 인자로 받아 1/2/3 입력에 따라 kill)
 *
 * 바뀐 점
 *   - kill(SIGUSR1/SIGUSR2/SIGTERM) 대신 sigqueue(SIGRTMIN, 명령 번호)로 보낸다.
 *     표준 시그널은 연속으로 보내면 합쳐지고(README 4-B), 서로 다른 시그널은
 *     순서가 바뀌기 때문이다(README 4-C). -l 옵션이면 원본처럼 kill()을 쓴다.
 *   - show, add N, del N, hang 명령 추가
 *   - 대상이 없어지면(ESRCH) 바로 알리고 종료
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"

static pid_t target_pid;
static int   legacy;       /* -l: 원본처럼 SIGUSR1/SIGUSR2를 kill()로 보냄 */

static int send_cmd(int cmd)
{
    int ret;
    if (legacy) {
        int sig = cmd == CMD_ADD ? SIGUSR1 : cmd == CMD_DEL ? SIGUSR2 : SIGTERM;
        ret = kill(target_pid, sig);
    } else {
        union sigval v = { .sival_int = cmd };
        /* 받는 쪽 실시간 시그널 큐가 가득 차면 EAGAIN: 잠깐 쉬고 다시 보낸다 */
        while ((ret = sigqueue(target_pid, SIG_CMD, v)) != 0 && errno == EAGAIN)
            usleep(1000);
    }
    if (ret != 0) {
        if (errno == ESRCH)
            printf("Fail! pid %d not found (firmware stopped?)\n", target_pid);
        else
            printf("Fail! %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static void help(void)
{
    printf("  1 | add [N]  : 채굴기 N개 추가 (기본 1)\n"
           "  2 | del [N]  : 가장 오래된 채굴기 N개 제거 (기본 1)\n"
           "  show         : 펌웨어 상태 출력 (펌웨어 쪽 터미널)\n"
           "  hang         : 펌웨어 메인 루프 멈춤 (WDT 테스트)\n"
           "  3 | quit     : 펌웨어 정상 종료\n"
           "  exit         : User App만 종료\n");
}

int main(int argc, char *argv[])
{
    int ai = 1;
    if (argc >= 2 && strcmp(argv[1], "-l") == 0) {
        legacy = 1;
        ai = 2;
    }
    if (argc - ai != 1) {
        printf("사용법: %s [-l] <PID>\n", argv[0]);
        return 1;
    }

    target_pid = (pid_t)atoi(argv[ai]);
    printf("target_pid:%d%s\n", target_pid, legacy ? " (legacy SIGUSR1/2)" : "");
    if (target_pid <= 0) {
        printf("Set up Signal Number\n");
        return 1;
    }
    if (kill(target_pid, 0) != 0) {           /* 시그널 0: 존재 여부만 확인 */
        printf("pid %d: %s\n", target_pid, strerror(errno));
        return 1;
    }

    char line[64];

    for (;;) {
        printf("INPUT >> ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin))
            break;

        char cmd[16] = "";
        int n = 1;
        int argn = sscanf(line, "%15s %d", cmd, &n);
        if (argn <= 0)
            continue;
        if (n < 1 || n > 100) {
            printf("N은 1~100\n");
            continue;
        }

        int c = 0;
        if (!strcmp(cmd, "1") || !strcmp(cmd, "add"))        c = CMD_ADD;
        else if (!strcmp(cmd, "2") || !strcmp(cmd, "del"))   c = CMD_DEL;
        else if (!strcmp(cmd, "show"))                       { c = CMD_SHOW; n = 1; }
        else if (!strcmp(cmd, "hang"))                       { c = CMD_HANG; n = 1; }
        else if (!strcmp(cmd, "3") || !strcmp(cmd, "quit"))  { c = CMD_QUIT; n = 1; }
        else if (!strcmp(cmd, "exit"))                       break;
        else if (!strcmp(cmd, "help"))                       { help(); continue; }
        else { printf("please re type (help)\n"); continue; }

        if (legacy && (c == CMD_SHOW || c == CMD_HANG)) {
            printf("legacy 모드는 1/2/3만 지원\n");
            continue;
        }

        for (int i = 0; i < n; i++)
            if (send_cmd(c) != 0)
                return 1;
        printf("%d <- signal transmit! (x%d)\n", target_pid, n);
        if (c == CMD_QUIT)
            break;
    }
    return 0;
}
