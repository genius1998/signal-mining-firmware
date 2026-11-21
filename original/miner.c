
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

#define SIZE 100

pthread_mutex_t mlock;

int cnt = 0;             // 채굴한 코인 개수
pthread_t queue[SIZE];   // 채굴기 스레드 ID 저장
int front = 0;
int rear = 0;

// 채굴기 스레드 함수
void *run(void *arg) {
    while (1) {
        usleep(100000);  // 0.1초 대기

        pthread_mutex_lock(&mlock);
        cnt++;
        pthread_mutex_unlock(&mlock);
    }

    return NULL;
}

// 큐에 스레드 ID 삽입
void enqueue(pthread_t value) {
    queue[rear] = value;
    rear++;
}

// 큐에서 가장 오래된 스레드 ID 꺼내기
int dequeue(pthread_t *value) {
    if (front == rear) {
        return 0;
    }

    *value = queue[front];
    front++;

    // 큐가 비었으면 인덱스 초기화
    if (front == rear) {
        front = 0;
        rear = 0;
    }

    return 1;
}

// 채굴기 추가
void add(void) {
    if (rear == SIZE) {
        printf("Queue Full\n");
        return;
    }

    pthread_t t;

    if (pthread_create(&t, NULL, run, NULL) != 0) {
        printf("스레드 생성 실패\n");
        return;
    }

    enqueue(t);
}

// 가장 오래된 채굴기 제거
void del(void) {
    if (front == rear) {
        printf("제거할 채굴기가 없습니다.\n");
        return;
    }

    pthread_t t = queue[front];

    // 스레드 종료 요청
    if (pthread_cancel(t) != 0) {
        printf("스레드 취소 요청 실패\n");
        return;
    }

    // 스레드 종료 대기 및 자원 회수
    if (pthread_join(t, NULL) != 0) {
        printf("스레드 종료 대기 실패\n");
        return;
    }

    // 종료가 확인된 스레드를 큐에서 제거
    pthread_t removed;
    dequeue(&removed);
}

int main(void) {
    pthread_mutex_init(&mlock, NULL);

    // 시작할 때 채굴기 하나 자동 생성
    add();

    while (1) {
        char buffer[16];

        printf("ssafy>> ");
        fflush(stdout);

        if (scanf("%15s", buffer) != 1) {
            break;
        }

        if (strcmp(buffer, "show") == 0) {
            pthread_mutex_lock(&mlock);
            printf("%d\n", cnt);
            pthread_mutex_unlock(&mlock);
        }
        else if (strcmp(buffer, "add") == 0) {
            add();
        }
        else if (strcmp(buffer, "del") == 0) {
            del();
        }
        else if (strcmp(buffer, "exit") == 0) {
            break;
        }
        else {
            printf("잘못된 명령어입니다.\n");
        }
    }

    // 종료 전 모든 채굴기 제거
    while (front != rear) {
        int before = front;
        int before_rear = rear;
        del();

        if (front == before && rear == before_rear) {
            break;
        }
    }

    pthread_mutex_destroy(&mlock);

    return 0;
}
