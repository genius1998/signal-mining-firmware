/*
 * 채굴기 스레드 관리
 * 채굴기 스레드 ID를 원형 큐에 넣고, 제거는 가장 오래된 것부터 한다.
 * 큐(front/size)도 mutex로 보호한다. 지금은 메인 루프만 큐를 바꾸지만,
 * 다른 스레드에서 호출해도 깨지지 않게 하려는 것이다.
 */
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "miner.h"

typedef struct {
    pthread_t tid;
    int       id;
} miner_t;

static pthread_mutex_t mlock = PTHREAD_MUTEX_INITIALIZER;  /* cnt 보호 */
static pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;  /* 큐 보호 */

static long    cnt = 0;              /* 채굴한 코인 개수 */
static miner_t queue[MINER_MAX];     /* 채굴기 스레드 원형 큐 */
static int     front = 0;
static int     size = 0;
static int     next_id = 1;

/* 채굴기 스레드 함수 */
static void *run(void *arg)
{
    (void)arg;
    while (1) {
        usleep(100000);              /* 0.1초 대기, 취소 지점(cancellation point) */

        pthread_mutex_lock(&mlock);
        cnt++;
        pthread_mutex_unlock(&mlock);
    }
    return NULL;
}

void miner_init(void)
{
    pthread_mutex_lock(&qlock);
    front = 0;
    size = 0;
    pthread_mutex_unlock(&qlock);
}

int miner_add(void)
{
    pthread_mutex_lock(&qlock);
    if (size == MINER_MAX) {
        pthread_mutex_unlock(&qlock);
        return -1;
    }

    miner_t m;
    m.id = next_id;
    if (pthread_create(&m.tid, NULL, run, NULL) != 0) {
        pthread_mutex_unlock(&qlock);
        return -1;
    }
    next_id++;

    /* enqueue */
    queue[(front + size) % MINER_MAX] = m;
    size++;
    pthread_mutex_unlock(&qlock);
    return m.id;
}

/*
 * 가장 오래된 채굴기 제거.
 * 초기 버전의 버그(README 4-A)를 고친 순서:
 *   1) queue[front]를 cancel  2) join으로 종료 확인  3) 그 다음에 dequeue
 */
int miner_del(void)
{
    pthread_mutex_lock(&qlock);
    if (size == 0) {
        pthread_mutex_unlock(&qlock);
        return -1;
    }

    miner_t m = queue[front];

    if (pthread_cancel(m.tid) != 0 || pthread_join(m.tid, NULL) != 0) {
        pthread_mutex_unlock(&qlock);
        return -1;
    }

    /* 종료가 확인된 스레드만 큐에서 뺀다 */
    front = (front + 1) % MINER_MAX;
    size--;
    pthread_mutex_unlock(&qlock);
    return m.id;
}

int miner_count(void)
{
    pthread_mutex_lock(&qlock);
    int n = size;
    pthread_mutex_unlock(&qlock);
    return n;
}

long miner_coins(void)
{
    pthread_mutex_lock(&mlock);
    long c = cnt;
    pthread_mutex_unlock(&mlock);
    return c;
}

void miner_del_all(void)
{
    while (miner_del() != -1)
        ;
}

void miner_destroy(void)
{
    miner_del_all();
}
