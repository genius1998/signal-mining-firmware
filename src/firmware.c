/*
 * Firmware App: 시그널로 제어되는 채굴 펌웨어
 *
 * 원본
 *   - original/hw7_firmware.c : signal()로 SIGUSR1/SIGUSR2/SIGTERM 핸들러 등록, PID 출력
 *   - original/miner.c        : 채굴기 스레드 추가/삭제/조회 (stdin 명령)
 *
 * 통합 구조
 *   [User App] --kill/sigqueue--> [시그널 핸들러] --self-pipe--> [메인 루프] --> 채굴기 스레드 N개
 *                                   (= ISR)                          ^ kick
 *                                                              [워치독 스레드]
 *
 *   - 원본처럼 시그널마다 핸들러를 등록한다(button_click, clear_reset, BYE).
 *     signal() 대신 sigaction(SA_SIGINFO)을 써서 보낸 PID와 sigqueue() 값을 받는다.
 *   - 핸들러는 ISR처럼 짧게: 이벤트 하나를 파이프에 write() 하고 끝난다.
 *     write()는 async-signal-safe 함수라서 핸들러 안에서 불러도 된다.
 *     printf, mutex, pthread_create 같은 실제 작업은 메인 루프가 한다.
 *   - 핸들러가 메인 스레드에서만 돌도록, 채굴기/워치독 스레드에서는 시그널을 막는다.
 *   - 메인 루프는 돌 때마다 워치독을 kick 한다. 멈추면 워치독이 감지하고 재시작(execv)한다.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "miner.h"
#include "watchdog.h"

/* ---------- 로그 ---------- */
static struct timespec t0;

static void logf_(const char *tag, const char *fmt, ...)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double t = (now.tv_sec - t0.tv_sec) + (now.tv_nsec - t0.tv_nsec) / 1e9;

    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    printf("[%7.3f] %-6s %s\n", t, tag, msg);   /* printf 한 번 호출 = 한 줄, 스레드 간 섞이지 않음 */
}

/* ---------- 이벤트 (핸들러 -> 메인 루프) ---------- */
typedef enum { EV_ADD, EV_DEL, EV_SHOW, EV_HANG, EV_QUIT } ev_t;

/* 핸들러가 파이프에 쓰는 기록 하나. PIPE_BUF(4096)보다 작으므로 write()가 쪼개지지 않는다 */
struct sig_event {
    int   sig;    /* 받은 시그널 번호 */
    int   cmd;    /* SIG_CMD일 때 sigqueue()로 실려 온 명령 번호 */
    pid_t pid;    /* 보낸 프로세스 */
};

static int sig_pipe[2] = { -1, -1 };          /* [0] 메인 루프가 읽음, [1] 핸들러가 씀 */
static volatile sig_atomic_t dropped;         /* 파이프가 가득 차 버린 시그널 수 */

/* ---------- 시그널 핸들러 (= ISR) ---------- */
/*
 * 핸들러 안에서는 async-signal-safe 함수만 쓴다: write(), errno 저장/복원.
 * 원본 과제7은 핸들러에서 printf()를 불렀는데, 채굴기 스레드와 mutex가 있는 통합 버전에서는
 * 메인 루프가 printf/mutex lock을 잡은 순간 핸들러가 끼어들면 교착될 수 있어서 출력은 메인 루프로 옮겼다.
 */
static void post_event(int sig, const siginfo_t *si)
{
    int saved = errno;                          /* 끼어든 코드의 errno를 망가뜨리지 않는다 */
    struct sig_event ev = {
        .sig = sig,
        .cmd = (sig == SIG_CMD) ? si->si_value.sival_int : 0,
        .pid = si->si_pid,
    };
    if (write(sig_pipe[1], &ev, sizeof ev) != (ssize_t)sizeof ev)
        dropped++;                              /* 파이프가 가득 참 (O_NONBLOCK이라 막히지 않는다) */
    errno = saved;
}

/* SIGUSR1: 원본의 "1" 버튼 -> 채굴기 추가 */
static void button_click(int sig, siginfo_t *si, void *uc) { (void)uc; post_event(sig, si); }
/* SIGUSR2: 원본의 "2" (화면 RESET) -> 가장 오래된 채굴기 제거 */
static void clear_reset(int sig, siginfo_t *si, void *uc)  { (void)uc; post_event(sig, si); }
/* SIGTERM, SIGINT: 원본의 "3" -> 종료. 원본은 여기서 _exit(0) 했지만 지금은 메인 루프가 정리 후 종료 */
static void BYE(int sig, siginfo_t *si, void *uc)          { (void)uc; post_event(sig, si); }
/* SIGRTMIN: 통합 버전 명령 채널. 명령 번호는 si_value에 실려 온다 */
static void command(int sig, siginfo_t *si, void *uc)      { (void)uc; post_event(sig, si); }

static sigset_t ctrl_set;   /* 위 핸들러로 받는 시그널 전부 */

static void build_sigset(sigset_t *set)
{
    sigemptyset(set);
    sigaddset(set, SIGUSR1);
    sigaddset(set, SIGUSR2);
    sigaddset(set, SIGTERM);
    sigaddset(set, SIGINT);
    sigaddset(set, SIG_CMD);
}

static void install(int sig, void (*fn)(int, siginfo_t *, void *))
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = fn;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;    /* siginfo 받기, 끊긴 read/write는 다시 시도 */
    sa.sa_mask = ctrl_set;                    /* 핸들러끼리 중첩되지 않게: 한 번에 하나씩 */
    sigaction(sig, &sa, NULL);
}

static void install_handlers(void)
{
    /* 원본: signal(SIGUSR1, button_click); signal(SIGUSR2, clear_reset); signal(SIGTERM, BYE); */
    install(SIGUSR1, button_click);
    install(SIGUSR2, clear_reset);
    install(SIGTERM, BYE);
    install(SIGINT,  BYE);        /* Ctrl+C도 같은 정상 종료 경로로 */
    install(SIG_CMD, command);
}

static const char *sig_name(int sig)
{
    if (sig == SIGUSR1)    return "SIGUSR1";
    if (sig == SIGUSR2)    return "SIGUSR2";
    if (sig == SIGTERM)    return "SIGTERM";
    if (sig == SIGINT)     return "SIGINT";
    if (sig == SIG_CMD)    return "SIGRTMIN";
    return "?";
}

static const char *ev_name(ev_t e)
{
    static const char *n[] = { "ADD", "DEL", "SHOW", "HANG", "QUIT" };
    return n[e];
}

/* 시그널 -> 이벤트. 모르는 명령이면 -1 */
static int to_event(const struct sig_event *se)
{
    if (se->sig == SIGUSR1) return EV_ADD;
    if (se->sig == SIGUSR2) return EV_DEL;
    if (se->sig == SIGTERM || se->sig == SIGINT) return EV_QUIT;

    switch (se->cmd) {            /* SIG_CMD: sigqueue()로 실려 온 명령 번호 */
    case CMD_ADD:  return EV_ADD;
    case CMD_DEL:  return EV_DEL;
    case CMD_SHOW: return EV_SHOW;
    case CMD_HANG: return EV_HANG;
    case CMD_QUIT: return EV_QUIT;
    default:       return -1;
    }
}

/* 채굴기 스레드는 시그널을 막은 채로 만든다. 새 스레드는 만든 스레드의 마스크를 물려받는다 */
static int add_miner(void)
{
    sigset_t old;
    pthread_sigmask(SIG_BLOCK, &ctrl_set, &old);
    int id = miner_add();
    pthread_sigmask(SIG_SETMASK, &old, NULL);
    return id;
}

/* ---------- 워치독 timeout 처리 ---------- */
static char **g_argv;
static int    g_reset_count;
static int    g_no_reset;

static void on_wdt_timeout(long elapsed_ms)
{
    logf_("WDT", "main loop no kick for %ld ms -> %s", elapsed_ms,
          g_no_reset ? "exit(2)" : "RESET");
    fflush(stdout);
    if (g_no_reset)
        _exit(2);

    /*
     * 하드웨어 WDT의 보드 리셋 대신 자기 자신을 다시 exec 한다.
     * PID가 그대로 유지되어 User App이 같은 PID로 계속 제어할 수 있다.
     * 메모리(코인 수, 채굴기 스레드, 파이프에 남은 이벤트)는 리셋처럼 모두 사라진다.
     * 이 함수는 시그널을 막아 둔 워치독 스레드에서 불리므로, 새 프로그램도 시그널이 막힌 채 시작한다.
     */
    char cnt_arg[16];
    snprintf(cnt_arg, sizeof cnt_arg, "%d", g_reset_count + 1);

    char *nargv[16];
    int n = 0;
    nargv[n++] = g_argv[0];
    for (int i = 1; g_argv[i] && n < 13; i++) {
        if (strcmp(g_argv[i], "-r") == 0) { i++; continue; }  /* 이전 리셋 카운트는 버림 */
        nargv[n++] = g_argv[i];
    }
    nargv[n++] = "-r";
    nargv[n++] = cnt_arg;
    nargv[n] = NULL;

    execv("/proc/self/exe", nargv);
    logf_("WDT", "execv failed: %s", strerror(errno));
    _exit(3);
}

/* ---------- main ---------- */
static void usage(const char *p)
{
    fprintf(stderr, "usage: %s [-n initial_miners] [-t wdt_timeout_ms] [-x]\n"
                    "  -x : WDT timeout 시 재시작하지 않고 exit(2)\n", p);
}

int main(int argc, char *argv[])
{
    int initial = 1;           /* 원본 채굴기 과제처럼 시작할 때 1개 생성 */
    int wdt_ms = 3000;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    setvbuf(stdout, NULL, _IOLBF, 0);
    g_argv = argv;

    int opt;
    while ((opt = getopt(argc, argv, "n:t:r:x")) != -1) {
        switch (opt) {
        case 'n': initial = atoi(optarg); break;
        case 't': wdt_ms = atoi(optarg); break;
        case 'r': g_reset_count = atoi(optarg); break;
        case 'x': g_no_reset = 1; break;
        default:  usage(argv[0]); return 1;
        }
    }

    /* 1) 준비하는 동안은 시그널을 막아 둔다. 그 사이 온 시그널은 커널에 대기했다가 3)에서 전달된다 */
    build_sigset(&ctrl_set);
    pthread_sigmask(SIG_BLOCK, &ctrl_set, NULL);

    if (pipe2(sig_pipe, O_NONBLOCK | O_CLOEXEC) != 0) {   /* exec 재시작 때 닫힌다 */
        perror("pipe2");
        return 1;
    }
    install_handlers();

    printf("My PID : %d\n", (int)getpid());   /* 원본 과제와 같은 형식 */
    logf_("BOOT", "firmware start (wdt %d ms, reset count %d)", wdt_ms, g_reset_count);

    /* 2) 스레드는 시그널이 막힌 상태에서 만든다 -> 핸들러는 메인 스레드에서만 실행된다 */
    miner_init();
    for (int i = 0; i < initial; i++) {
        int id = add_miner();
        if (id > 0) logf_("MINER", "miner #%d added (total %d)", id, miner_count());
    }
    if (wdt_start(wdt_ms, on_wdt_timeout) != 0) {
        perror("wdt_start");
        return 1;
    }

    /* 3) 메인 스레드만 시그널을 연다 */
    pthread_sigmask(SIG_UNBLOCK, &ctrl_set, NULL);

    /* 4) 메인 루프: 워치독 kick + 핸들러가 남긴 이벤트 처리 */
    int quit = 0;
    int reported_drop = 0;
    while (!quit) {
        wdt_kick();

        /* 이벤트가 없어도 100ms마다 돌아와서 kick 한다. 시그널이 오면 EINTR로 바로 깨어난다 */
        struct pollfd pfd = { .fd = sig_pipe[0], .events = POLLIN };
        if (poll(&pfd, 1, 100) <= 0)
            continue;

        struct sig_event se;
        while (!quit && read(sig_pipe[0], &se, sizeof se) == (ssize_t)sizeof se) {
            int e = to_event(&se);
            if (e < 0) {
                logf_("SIG", "%s from pid %d: unknown cmd %d", sig_name(se.sig),
                      (int)se.pid, se.cmd);
                continue;
            }
            logf_("SIG", "%s from pid %d -> %s", sig_name(se.sig), (int)se.pid, ev_name(e));

            int id;
            switch ((ev_t)e) {
            case EV_ADD:
                id = add_miner();
                if (id > 0) logf_("MINER", "miner #%d added (total %d)", id, miner_count());
                else        logf_("MINER", "add failed (max %d)", MINER_MAX);
                break;
            case EV_DEL:
                id = miner_del();
                if (id > 0) logf_("MINER", "miner #%d removed (total %d)", id, miner_count());
                else        logf_("MINER", "no miner to remove");
                break;
            case EV_SHOW:
                logf_("STATUS", "miners=%d coins=%ld resets=%d",
                      miner_count(), miner_coins(), g_reset_count);
                break;
            case EV_HANG:
                logf_("MAIN", "simulated hang: main loop stops kicking WDT");
                for (;;)
                    pause();      /* 핸들러(ISR)는 계속 돌지만 메인 루프는 다시 돌지 않는다 */
            case EV_QUIT:
                quit = 1;
                break;
            }
        }

        if (dropped != reported_drop) {
            logf_("SIG", "pipe full, %d signal(s) dropped", (int)dropped - reported_drop);
            reported_drop = dropped;
        }
    }

    /* 5) 정상 종료: 시그널 닫기 -> 채굴기 -> 워치독 순으로 정리 */
    pthread_sigmask(SIG_BLOCK, &ctrl_set, NULL);
    logf_("MAIN", "shutdown: stopping %d miners", miner_count());
    miner_del_all();
    wdt_stop();
    logf_("MAIN", "BYE (coins=%ld)", miner_coins());
    close(sig_pipe[0]);
    close(sig_pipe[1]);
    return 0;
}
