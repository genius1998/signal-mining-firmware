/*
 * Firmware App: 시그널로 제어되는 채굴 펌웨어
 *
 * 원본
 *   - original/hw7_firmware.c : signal()로 SIGUSR1/SIGUSR2/SIGTERM 핸들러 등록, PID 출력
 *   - original/miner.c        : 채굴기 스레드 추가/삭제/조회 (stdin 명령)
 *
 * 통합 구조
 *   [User App] --kill/sigqueue--> [시그널 스레드] --이벤트 큐--> [메인 루프] --> 채굴기 스레드 N개
 *                                                                  ^ kick
 *                                                            [워치독 스레드]
 *
 *   - 시그널은 모든 스레드에서 막아 두고(pthread_sigmask), 전용 스레드가 sigwaitinfo()로 받는다.
 *     핸들러 안에서 printf/mutex를 쓰지 않기 위해서다.
 *   - 실제 작업(채굴기 추가/삭제/출력)은 메인 루프가 한다. 시그널 스레드는 이벤트만 넣는다.
 *   - 메인 루프는 돌 때마다 워치독을 kick 한다. 멈추면 워치독이 감지하고 재시작(execv)한다.
 */
#define _GNU_SOURCE
#include <errno.h>
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

/* ---------- 이벤트 큐 (시그널 스레드 -> 메인 루프) ---------- */
typedef enum { EV_ADD, EV_DEL, EV_SHOW, EV_HANG, EV_QUIT } ev_t;

#define EVQ_SIZE 64
static ev_t            evq[EVQ_SIZE];
static int             evq_head, evq_len;
static pthread_mutex_t evq_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  evq_cond;

static int ev_push(ev_t e)
{
    int ok = 0;
    pthread_mutex_lock(&evq_lock);
    if (evq_len < EVQ_SIZE) {
        evq[(evq_head + evq_len) % EVQ_SIZE] = e;
        evq_len++;
        ok = 1;
        pthread_cond_signal(&evq_cond);
    }
    pthread_mutex_unlock(&evq_lock);
    return ok;
}

/* timeout_ms 동안 이벤트를 기다린다. 이벤트가 있으면 1 */
static int ev_pop(ev_t *e, int timeout_ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_nsec += (long)timeout_ms * 1000000L;
    ts.tv_sec  += ts.tv_nsec / 1000000000L;
    ts.tv_nsec %= 1000000000L;

    int got = 0;
    pthread_mutex_lock(&evq_lock);
    while (evq_len == 0) {
        if (pthread_cond_timedwait(&evq_cond, &evq_lock, &ts) == ETIMEDOUT)
            break;
    }
    if (evq_len > 0) {
        *e = evq[evq_head];
        evq_head = (evq_head + 1) % EVQ_SIZE;
        evq_len--;
        got = 1;
    }
    pthread_mutex_unlock(&evq_lock);
    return got;
}

/* ---------- 시그널 스레드 ---------- */
static sigset_t ctrl_set;

static void build_sigset(sigset_t *set)
{
    sigemptyset(set);
    sigaddset(set, SIGUSR1);      /* 원본 과제의 "1" -> 채굴기 추가 */
    sigaddset(set, SIGUSR2);      /* 원본 과제의 "2" -> 가장 오래된 채굴기 제거 */
    sigaddset(set, SIGTERM);      /* 원본 과제의 "3" -> 종료 */
    sigaddset(set, SIGINT);       /* Ctrl+C도 같은 정상 종료 경로로 */
    sigaddset(set, SIG_CMD);      /* 통합 버전: sigqueue()로 명령 번호를 받는다 */
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
static int to_event(int sig, const siginfo_t *si)
{
    if (sig == SIGUSR1) return EV_ADD;
    if (sig == SIGUSR2) return EV_DEL;
    if (sig == SIGTERM || sig == SIGINT) return EV_QUIT;

    switch (si->si_value.sival_int) {   /* SIG_CMD: sigqueue()로 실려 온 명령 번호 */
    case CMD_ADD:  return EV_ADD;
    case CMD_DEL:  return EV_DEL;
    case CMD_SHOW: return EV_SHOW;
    case CMD_HANG: return EV_HANG;
    case CMD_QUIT: return EV_QUIT;
    default:       return -1;
    }
}

static void *signal_thread(void *arg)
{
    (void)arg;
    for (;;) {
        siginfo_t si;
        int sig = sigwaitinfo(&ctrl_set, &si);
        if (sig < 0) {
            if (errno == EINTR)
                continue;
            logf_("SIG", "sigwaitinfo error: %s", strerror(errno));
            continue;
        }

        int e = to_event(sig, &si);
        if (e < 0) {
            logf_("SIG", "%s from pid %d: unknown cmd %d", sig_name(sig),
                  (int)si.si_pid, si.si_value.sival_int);
            continue;
        }

        logf_("SIG", "%s from pid %d -> %s", sig_name(sig), (int)si.si_pid, ev_name(e));
        if (!ev_push((ev_t)e))
            logf_("SIG", "event queue full, %s dropped", sig_name(sig));
        if (e == EV_QUIT)
            break;
    }
    return NULL;
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
     * 메모리(코인 수, 채굴기 스레드)는 리셋처럼 모두 사라진다.
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

    /* 1) 스레드를 만들기 전에 시그널을 막는다. 이후 생성되는 스레드는 이 마스크를 물려받는다. */
    build_sigset(&ctrl_set);
    pthread_sigmask(SIG_BLOCK, &ctrl_set, NULL);

    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    pthread_cond_init(&evq_cond, &ca);

    printf("My PID : %d\n", (int)getpid());   /* 원본 과제와 같은 형식 */
    logf_("BOOT", "firmware start (wdt %d ms, reset count %d)", wdt_ms, g_reset_count);

    miner_init();
    for (int i = 0; i < initial; i++) {
        int id = miner_add();
        if (id > 0) logf_("MINER", "miner #%d added (total %d)", id, miner_count());
    }

    pthread_t sig_tid;
    if (pthread_create(&sig_tid, NULL, signal_thread, NULL) != 0) {
        perror("pthread_create");
        return 1;
    }
    if (wdt_start(wdt_ms, on_wdt_timeout) != 0) {
        perror("wdt_start");
        return 1;
    }

    /* 2) 메인 루프: 이벤트 처리 + 워치독 kick */
    int quit = 0;
    while (!quit) {
        wdt_kick();

        ev_t e;
        if (!ev_pop(&e, 100))
            continue;

        int id;
        switch (e) {
        case EV_ADD:
            id = miner_add();
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
                pause();          /* 시그널은 막혀 있으므로 영원히 깨지 않는다 */
        case EV_QUIT:
            quit = 1;
            break;
        }
    }

    /* 3) 정상 종료: 채굴기 -> 워치독 -> 시그널 스레드 순으로 정리 */
    logf_("MAIN", "shutdown: stopping %d miners", miner_count());
    miner_del_all();
    wdt_stop();
    pthread_join(sig_tid, NULL);     /* EV_QUIT을 보낸 뒤 스스로 빠져나와 있다 */
    logf_("MAIN", "BYE (coins=%ld)", miner_coins());
    pthread_cond_destroy(&evq_cond);
    return 0;
}
