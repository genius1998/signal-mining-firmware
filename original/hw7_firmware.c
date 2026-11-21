
#include <stdio.h>   // 표준 입출력 함수를 사용하기 위한 헤더 파일
#include <signal.h>  // 시그널 처리를 위한 헤더 파일
#include <unistd.h>  // UNIX 표준 함수 (pid, sleep 등)를 사용하기 위한 헤더 파일

// 시그널 핸들러 함수 정의, SIGUSR1 시그널을 받으면 실행됨
void button_click(){
    printf("[Button Clicked]\n");
}
void clear_reset(){
    printf("\033[2J\033[H");
    printf("RESET\n");
}
void BYE(){
    printf("\nBYE\n");
    _exit(0);
}

int main() {
    // SIGUSR1 시그널을 받으면 run 함수가 호출되어 동작한다.
    signal(SIGUSR1, button_click);
    signal(SIGUSR2, clear_reset);
    signal(SIGTERM, BYE);
    // 현재 프로세스의 PID를 가져와 출력
    pid_t pid = getpid();
    printf("My PID : %d\n", pid);  // 프로세스 ID 출력

    // 무한 루프를 통해 반복적으로 메시지를 출력
    while(1) {
        for (int i = 0; i <= 'Z' - 'A'; i++){
            printf("%c", i + 'A');
            usleep(100000);

        }
        for (int i = 'Z' - 'A'; i >= 0; i--){
            printf("%c", i + 'A');
            usleep(100000);

        }

    }

    return 0;  // 프로그램 종료 (실제 도달하지 않음)
}
