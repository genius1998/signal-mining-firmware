
#include <stdio.h>      // 표준 입출력 함수를 사용하기 위한 헤더 파일
#include <signal.h>     // 시그널 처리를 위한 헤더 파일 (kill 함수 사용 및 시그널 정의)
#include <sys/types.h>  // 데이터 타입 (pid_t 등)을 사용하기 위한 헤더 파일
#include <stdlib.h>     // 표준 라이브러리 함수 (exit 등)를 사용하기 위한 헤더 파일
#include <string.h>
int main(int argc, char *argv[]) {
    // 인자가 전달되었는지 확인
    if (argc != 2) {
        printf("사용법: %s <PID>\n", argv[0]);
        return 1;
    }

    // 전달받은 인자를 정수로 변환
    pid_t target_pid = (pid_t)atoi(argv[1]);
    printf("target_pid:%d\n", target_pid);
    // target_pid가 설정되지 않은 경우 프로그램을 종료
    if (target_pid == 0) {
        printf("Set up Signal Number\n");  // 시그널을 보낼 PID를 설정하지 않았다는 메시지 출력
        exit(1);  // 비정상 종료 (에러 코드 1)
    }
    // target_pid에 SIGUSR1 시그널을 전송
    while(1){
        char buffer[16];

        printf("INPUT >>");
        fflush(stdout);
        if (scanf("%15s", buffer) != 0){
            if (strcmp(buffer, "1") == 0){
                int ret = kill(target_pid, SIGUSR1);

                // 시그널 전송이 성공했는지 확인
                if (ret == 0) {
                    printf("%d <- signal transmit!\n", target_pid);  // 시그널 전송 성공 시 메시지 출력
                }
                else {
                    printf("Fail!\n");  // 시그널 전송 실패 시 실패 메시지 출력
                    exit(1);             // 비정상 종료 (에러 코드 1)
                }
            }
            else if (strcmp(buffer, "2") == 0){
                int ret = kill(target_pid, SIGUSR2);

                // 시그널 전송이 성공했는지 확인
                if (ret == 0) {
                    printf("%d <- signal transmit!\n", target_pid);  // 시그널 전송 성공 시 메시지 출력
                }
                else {
                    printf("Fail!\n");  // 시그널 전송 실패 시 실패 메시지 출력
                    exit(1);             // 비정상 종료 (에러 코드 1)
                }
            }
            else if (strcmp(buffer, "3") == 0){
                int ret = kill(target_pid, SIGTERM);

                // 시그널 전송이 성공했는지 확인
                if (ret == 0) {
                    printf("%d <- signal transmit!\n", target_pid);  // 시그널 전송 성공 시 메시지 출력
                }
                else {
                    printf("Fail!\n");  // 시그널 전송 실패 시 실패 메시지 출력
                    exit(1);             // 비정상 종료 (에러 코드 1)
                }
            }
            else if (strcmp(buffer, "exit") == 0){
                exit(1);
            }
            else{
                printf("\nplease re type\n");
                continue;
            }

        }
    }

    return 0;  // 프로그램 정상 종료
}
