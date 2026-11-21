/*
 * 채굴기 관리 초기 버전 (실패). README 4-A 참고.
 * 이 파일은 그대로는 컴파일되지 않는다 (enqueue()의 value 미정의). 빌드 대상이 아니다.
 */
#include <stdio.h>      // 표준 입출력 라이브러리 포함
#include <pthread.h>    // POSIX 스레드 라이브러리 포함
#include <unistd.h>     // 유닉스 표준 기능 포함 (sleep 함수 사용을 위해 포함)
#include <string.h>

pthread_mutex_t mlock;
int cnt = 0; // 채굴한 코인 갯수
// 첫 번째 스레드가 실행할 함수
void *run(void *arg) {
    while (1){
                usleep(100000);
                pthread_mutex_lock(&mlock);
                cnt++;
                pthread_mutex_unlock(&mlock);
        }
    return NULL;
}

#define SIZE 100
int int_arr[SIZE];
pthread_t queue[SIZE];
int front = 0;
int rear = 0;

// 큐에 데이터 삽입
void enqueue() {
    if (rear == SIZE) {
        return;
    }

    queue[rear] = value;
    rear++;
}

// 큐에서 데이터 꺼내기
int dequeue() {
    if (front == rear) {
        return -1;
    }

    front++;

    return queue[front];
}
int add(){
        int_arr[rear] = rear;
        pthread_create(&queue[rear], NULL, run, &int_arr[rear]);
        enqueue();
}
int del(){
        dequeue();
        pthread_cancel(queue[front]);
        pthread_join(queue[front], NULL);
}
int main(){
        pthread_mutex_init(&mlock, NULL);
        char start_buffer[8];
        printf("ssafy>>");
        while (1){
                char buffer[5];
                scanf("%4s", buffer);
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
        else {
            continue;
        }
        }
        pthread_mutex_destroy(&mlock);
        return 0;
}