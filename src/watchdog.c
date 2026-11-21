/*
 * 소프트웨어 워치독
 * 하드웨어 WDT처럼, 감시 대상(펌웨어 메인 루프)이 주기적으로 kick 하지 않으면
 * timeout으로 판단한다. kick 시각은 CLOCK_MONOTONIC 기준 ms로 atomic 변수에 저장한다.
 */
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>
#include "watchdog.h"

static atomic_long last_kick_ms;
static atomic_int  running;
static pthread_t   wdt_tid;
static int         wdt_timeout_ms;
static void      (*wdt_cb)(long);

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

void wdt_kick(void)
{
    atomic_store(&last_kick_ms, now_ms());
}

static void *wdt_thread(void *arg)
{
    (void)arg;
    while (atomic_load(&running)) {
        usleep(200000);                       /* 200ms마다 검사 */
        long elapsed = now_ms() - atomic_load(&last_kick_ms);
        if (elapsed > wdt_timeout_ms) {
            wdt_cb(elapsed);                  /* 보통 돌아오지 않는다 (reset) */
            wdt_kick();
        }
    }
    return NULL;
}

int wdt_start(int timeout_ms, void (*on_timeout)(long))
{
    wdt_timeout_ms = timeout_ms;
    wdt_cb = on_timeout;
    wdt_kick();
    atomic_store(&running, 1);
    return pthread_create(&wdt_tid, NULL, wdt_thread, NULL);
}

void wdt_stop(void)
{
    atomic_store(&running, 0);
    pthread_join(wdt_tid, NULL);
}
