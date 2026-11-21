# Signal 기반 채굴 펌웨어 제어 시스템

User App이 보내는 시그널로 Firmware App 안의 채굴기 스레드를 추가·제거·조회하고, 워치독 스레드가 펌웨어 메인 루프의 멈춤을 감지해 재시작하는 Linux C 프로젝트다.

- Firmware App(`src/firmware.c`): 채굴기 스레드를 큐로 관리하고, 시그널을 명령으로 받아 처리한다. 원본 과제7처럼 시그널마다 핸들러를 등록하고, 핸들러는 ISR처럼 이벤트만 남기고 실제 처리는 메인 루프가 한다.
- User App(`src/user_app.c`): 펌웨어 PID를 인자로 받아 사용자 입력을 시그널로 바꿔 보낸다. 명령은 실시간 시그널 하나에 명령 번호를 실어 `sigqueue()`로 보낸다.
- Watchdog(`src/watchdog.c`): 메인 루프가 일정 시간 동안 kick 하지 않으면 timeout으로 보고 펌웨어를 다시 exec 한다.
- SSAFY에서 따로 낸 두 과제(채굴기, 과제7 시그널 Firmware/User App)를 하나로 합친 것이다. 원본 코드는 `original/`에 그대로 두었고, 원본과 통합하면서 추가한 부분을 [구현 범위](#구현-범위)에 나눠 적었다.

| 항목 | 내용 |
|---|---|
| 과정 | SSAFY 16기 임베디드 트랙, 리눅스 시스템 프로그래밍 |
| 인원 | 1명 |
| 사용 기술 | C, POSIX 스레드(pthread, mutex, condition variable, cancel/join), POSIX 시그널(sigaction, sigqueue, 실시간 시그널, pthread_sigmask), self-pipe, poll, execv |
| 개발 환경 | Linux (테스트: Ubuntu 24.04, gcc 13.3), Makefile |

---

## 프로젝트 배경

### 펌웨어와 외부 제어
임베디드 장치의 펌웨어는 보통 메인 루프 하나가 계속 돌면서 일을 처리하고, 버튼이나 외부 장치의 요청은 인터럽트로 들어온다. 인터럽트 핸들러(ISR)는 짧게 끝내야 하기 때문에, 핸들러는 "무슨 일이 왔다"는 표시만 남기고 실제 처리는 메인 루프가 맡는 구조를 많이 쓴다.

Linux 사용자 공간에서 이 인터럽트 역할을 하는 것이 시그널이다. 과제7은 User App이 `kill()`로 보낸 시그널을 Firmware App이 받아 버튼 입력처럼 처리하는 과제였다. 이 프로젝트도 원본처럼 시그널마다 핸들러(`button_click`, `clear_reset`, `BYE`)를 등록한다. 다만 시그널 핸들러 안에서는 `printf()`나 `pthread_mutex_lock()` 같은 함수를 안전하게 쓸 수 없다(async-signal-safe가 아님). 그래서 핸들러는 ISR처럼 "무슨 시그널이 왔다"는 기록 하나를 파이프에 `write()`하고 바로 끝나고, 채굴기 추가·제거와 출력은 메인 루프가 그 기록을 읽어서 한다.

### 워치독(WDT)
펌웨어가 무한 루프나 교착 상태에 빠지면 외부에서 알아챌 방법이 없다. 하드웨어 워치독은 펌웨어가 주기적으로 타이머를 리셋(kick)하지 않으면 보드를 재시작한다. 이 프로젝트는 같은 동작을 스레드로 만들었다. 메인 루프가 kick을 멈추면 워치독 스레드가 감지하고, 보드 리셋 대신 프로세스를 다시 exec 한다.

### 구현 범위

| 구현한 것 | 구현하지 않은 것 |
|---|---|
| 채굴기 스레드 추가/제거(가장 오래된 것부터)/조회 *(원본: 채굴기 과제)* | 실제 해시 계산 (채굴은 0.1초마다 카운터 +1) |
| PID 출력, SIGUSR1/SIGUSR2/SIGTERM 수신 *(원본: 과제7 Firmware)* | 시그널 대신 소켓·파이프 같은 다른 IPC |
| PID 인자로 시그널 전송 *(원본: 과제7 User App)* | 명령 처리 결과를 User App에 돌려주는 응답 채널 |
| 핸들러는 기록만, 처리는 메인 루프 (self-pipe) *(통합하면서 추가)* | 여러 User App의 권한 구분 |
| `sigqueue()` + 실시간 시그널 하나로 명령 전달 *(통합하면서 추가)* | 리셋 전 상태(코인 수 등) 보존 |
| 소프트웨어 워치독, timeout 시 `execv()` 재시작 *(통합하면서 추가)* | 하드웨어 WDT(`/dev/watchdog`) 연동 |
| 정상 종료 시 모든 스레드 cancel/join *(통합하면서 추가)* | |
| 통합 테스트 스크립트 24개 항목 *(통합하면서 추가)* | |

과제7 Firmware 원본은 메인 루프에서 A\~Z를 출력하는 화면 동작이 있었는데, 채굴기 로그와 섞여서 통합 버전에서는 뺐다. 그 자리는 이벤트 처리와 워치독 kick이 대신한다.

---

## 1. 내가 맡은 역할

1. 원본 두 과제 작성: 채굴기 스레드 관리(`original/miner.c`), 시그널 Firmware/User App(`original/hw7_*.c`)
2. 채굴기 과제의 트러블슈팅: 실패한 버전(`original/miner_troubleshooting.c`)의 원인을 찾아 cancel → join → dequeue 순서로 고침 ([4-A](#a-채굴기-제거-순서-원본-과제))
3. 통합 구조 설계: 시그널 핸들러(ISR) → self-pipe → 메인 루프 → 채굴기 스레드, 그리고 메인 루프를 감시하는 워치독
4. 시그널 프로토콜 변경: 표준 시그널의 합쳐짐과 순서 바뀜을 재현하고 `sigqueue()` 방식으로 바꿈 ([4-B](#b-같은-시그널이-합쳐져-명령이-사라짐), [4-C](#c-서로-다른-시그널의-순서가-바뀜))
5. 워치독과 재시작, 정상 종료 순서 구현
6. 통합 테스트 스크립트(`tests/test_integration.sh`) 작성

---

## 2. 구조와 동작 흐름

```mermaid
flowchart LR
    U["User App<br/>(user_app)"] -- "sigqueue(SIGRTMIN, 명령 번호)<br/>kill(SIGUSR1/2/TERM)" --> S

    subgraph F["Firmware App (firmware) — 하나의 프로세스"]
        S["시그널 핸들러 (ISR)<br/>button_click / clear_reset / BYE / command"] -- "write()" --> Q[("self-pipe<br/>O_NONBLOCK")]
        Q -- "poll(100ms) + read()" --> M["메인 루프<br/>(핸들러는 메인 스레드에서만 실행)"]
        M -- "miner_add / miner_del" --> W1["채굴기 #1"]
        M --> W2["채굴기 #2"]
        M --> W3["채굴기 #N"]
        M -. "wdt_kick()" .-> D["워치독 스레드<br/>200ms마다 검사"]
        D -. "timeout → execv()" .-> F
    end
```

| 구성 | 펌웨어에 비유하면 | 역할 |
|---|---|---|
| User App (`src/user_app.c`) | 외부 버튼 / 호스트 | 사용자 입력을 명령 번호로 바꿔 시그널로 보낸다 |
| 시그널 핸들러 (`button_click()` 등) | 인터럽트 핸들러(ISR) | 받은 시그널, 보낸 PID, 명령 번호를 파이프에 `write()` 하고 끝난다 |
| self-pipe (`sig_pipe`) | ISR과 메인 루프 사이의 버퍼 | 핸들러가 쓰고 메인 루프가 읽는다. 쓰는 쪽은 `O_NONBLOCK`이라 가득 차도 핸들러가 막히지 않는다 |
| 메인 루프 (`main()`) | 펌웨어 메인 루프 | 파이프에서 이벤트를 꺼내 채굴기를 추가·제거하고 상태를 출력한다. 매 반복마다 워치독을 kick 한다 |
| 채굴기 스레드 (`src/miner.c`) | 작업 태스크 | 0.1초마다 공용 코인 카운터를 1 올린다 |
| 워치독 스레드 (`src/watchdog.c`) | 하드웨어 WDT | 마지막 kick 이후 시간이 timeout을 넘으면 재시작한다 |

명령 하나가 처리되는 순서는 다음과 같다.

```
User App에 add 입력
 → sigqueue(firmware_pid, SIGRTMIN, {sival_int = CMD_ADD})
 → 커널이 메인 스레드를 끊고 핸들러 command() 실행 → {SIGRTMIN, CMD_ADD, 보낸 pid}를 파이프에 write() → 복귀
 → 메인 루프: poll()이 EINTR/POLLIN으로 깨어남 → read()로 기록을 꺼내 EV_ADD로 변환 → miner_add() → pthread_create(run)
 → 로그: [  0.503] MINER  miner #2 added (total 2)
```

---

## 3. 핵심 구현

### 시그널 프로토콜 (`src/common.h`)

| User App 입력 | 보내는 시그널 (기본) | `-l` 모드 (원본 방식) | Firmware 동작 |
|---|---|---|---|
| `1`, `add [N]` | `SIGRTMIN` + `CMD_ADD` | `SIGUSR1` | 채굴기 N개 추가 |
| `2`, `del [N]` | `SIGRTMIN` + `CMD_DEL` | `SIGUSR2` | 가장 오래된 채굴기 N개 제거 |
| `show` | `SIGRTMIN` + `CMD_SHOW` | - | 채굴기 수, 코인 수, 리셋 횟수 출력 |
| `hang` | `SIGRTMIN` + `CMD_HANG` | - | 메인 루프를 멈춤 (워치독 테스트용) |
| `3`, `quit` | `SIGRTMIN` + `CMD_QUIT` | `SIGTERM` | 정상 종료 |
| `exit` | 보내지 않음 | - | User App만 종료 |

Firmware는 원본 과제의 `SIGUSR1`, `SIGUSR2`, `SIGTERM`을 지금도 그대로 받는다. 그래서 원본 과제7 User App(`original/hw7_userapp.c`)으로도 1/2/3 명령을 보낼 수 있고, 셸에서 `kill -TERM <pid>`나 Ctrl+C(`SIGINT`)로 끝내도 같은 정상 종료 경로를 탄다.

### 시그널 받기: 원본처럼 핸들러, 대신 ISR처럼 짧게
원본 과제7의 등록 코드와 핸들러 이름을 그대로 살렸다.

```c
/* original/hw7_firmware.c */
signal(SIGUSR1, button_click);
signal(SIGUSR2, clear_reset);
signal(SIGTERM, BYE);

/* src/firmware.c install_handlers(): siginfo(보낸 PID, sigqueue 값)를 받기 위해 sigaction으로 */
install(SIGUSR1, button_click);
install(SIGUSR2, clear_reset);
install(SIGTERM, BYE);
install(SIGINT,  BYE);
install(SIG_CMD, command);      /* SIGRTMIN: 통합 버전 명령 채널 */
```

달라진 것은 핸들러 **안에서 하는 일**이다.

```c
/* 원본: 핸들러에서 바로 출력 */
void button_click() { printf("[Button Clicked]\n"); }

/* 통합 버전: 핸들러는 기록 하나를 파이프에 쓰고 끝 (모든 핸들러 공통) */
static void post_event(int sig, const siginfo_t *si)
{
    int saved = errno;
    struct sig_event ev = { sig, (sig == SIG_CMD) ? si->si_value.sival_int : 0, si->si_pid };
    if (write(sig_pipe[1], &ev, sizeof ev) != sizeof ev)   /* write()는 async-signal-safe */
        dropped++;
    errno = saved;
}

/* 메인 루프: 일반 코드라서 printf, mutex, pthread_create를 써도 된다 */
poll(&pfd, 1, 100);                         /* 100ms마다, 또는 시그널이 오면 바로 깨어남 */
read(sig_pipe[0], &se, sizeof se);
logf_("SIG", "%s from pid %d -> %s", ...);
miner_add();  /* 또는 miner_del(), show, quit */
```

- **왜 핸들러에서 바로 처리하지 않나**: 원본은 단일 스레드가 출력만 해서 `printf()`를 핸들러에서 불러도 문제가 드러나지 않았다. 통합 버전에서 시그널은 채굴기 스레드를 만들고(`pthread_create`), 취소·join 하고(`pthread_cancel/join`), 큐와 코인 수를 mutex로 건드린다. 메인 루프가 `qlock`이나 `printf` 내부 lock을 잡은 순간 핸들러가 끼어들어 같은 lock을 잡으면 교착된다. 그래서 핸들러는 async-signal-safe인 `write()`만 쓴다.
- **왜 파이프인가(self-pipe)**: `volatile sig_atomic_t` 플래그 하나로는 "몇 번 왔는지"와 "보낸 PID, 명령 번호"를 담을 수 없다. 파이프에 고정 크기 기록을 쓰면 온 순서대로 쌓이고, 기록이 `PIPE_BUF`보다 작아서 한 번의 `write()`가 쪼개지지 않는다. 쓰는 쪽은 `O_NONBLOCK`이라 가득 차도 핸들러가 멈추지 않고, 버린 개수만 세어 메인 루프가 로그로 남긴다.
- **핸들러는 메인 스레드에서만**: 멀티스레드 프로세스에서 시그널은 막지 않은 아무 스레드로나 전달된다. 그래서 준비하는 동안 시그널을 막고(`pthread_sigmask`), 채굴기·워치독 스레드는 막힌 상태에서 만들어 그 마스크를 물려받게 한 뒤, 마지막에 메인 스레드만 연다. 실행 중에 채굴기를 추가할 때도 `add_miner()`가 잠깐 막고 `pthread_create()` 한다. 핸들러가 늘 메인 루프를 끊고 들어오므로 "ISR이 메인 루프를 선점한다"는 펌웨어 구조와 그대로 맞는다.
- **핸들러끼리 중첩 없음**: `sa_mask`에 받는 시그널 전부를 넣어, 핸들러가 도는 동안 다른 시그널은 대기했다가 하나씩 들어온다.
- `SA_SIGINFO`로 받은 `si_pid`로 누가 보냈는지도 로그에 남긴다.

### 채굴기 관리 (`src/miner.c`)

| 함수 | 동작 |
|---|---|
| `miner_add()` | `pthread_create()` 성공 후에만 원형 큐에 넣는다. 최대 100개 |
| `miner_del()` | `queue[front]`를 `pthread_cancel()` → `pthread_join()` → 둘 다 성공하면 dequeue |
| `miner_del_all()` | 큐가 빌 때까지 `miner_del()` 반복 |
| `miner_count()`, `miner_coins()` | 큐 크기, 코인 수를 각각 mutex 안에서 읽는다 |

- 원본 채굴기 과제의 `run()`, cancel → join → dequeue 순서를 그대로 썼다. 원본은 큐가 비면 `front`, `rear`를 0으로 되돌리는 선형 큐였는데, 통합 버전은 `front`와 `size`를 쓰는 원형 큐로 바꿔서 추가·제거를 반복해도 앞쪽 칸이 버려지지 않게 했다.
- 채굴기 스레드의 취소 지점은 `usleep()` 하나다. `pthread_mutex_lock()`은 취소 지점이 아니라서, `cnt`를 올리는 도중(lock을 잡은 상태)에 취소되는 일이 없다.

### 워치독 (`src/watchdog.c`)
```c
void wdt_kick(void) { atomic_store(&last_kick_ms, now_ms()); }   /* 메인 루프가 매 반복 호출 */

/* 워치독 스레드: 200ms마다 검사 */
long elapsed = now_ms() - atomic_load(&last_kick_ms);
if (elapsed > wdt_timeout_ms)
    wdt_cb(elapsed);                                             /* firmware.c의 on_wdt_timeout() */
```

- 시간은 `CLOCK_MONOTONIC`으로 잰다. 시스템 시계가 바뀌어도 timeout 판단이 흔들리지 않는다.
- 메인 루프는 이벤트가 없어도 `poll()`이 100ms마다 timeout으로 돌아오기 때문에, 정상이면 kick 간격이 100ms를 넘지 않는다. 기본 timeout은 3000ms(`-t`로 변경)이고, 200ms 간격으로 검사하므로 실제 감지는 timeout 뒤 0\~200ms 안에 일어난다. (측정: timeout 3000ms → 3099ms에 감지)
- timeout이 나면 `execv("/proc/self/exe", ...)`로 자기 자신을 다시 실행한다. exec는 PID를 바꾸지 않기 때문에 User App은 같은 PID로 계속 명령을 보낼 수 있다. 채굴기 스레드와 코인 수는 보드 리셋처럼 모두 사라지고, 리셋 횟수만 `-r` 인자로 넘겨 `show`에 표시한다. `-x` 옵션을 주면 재시작 대신 종료 코드 2로 끝난다.

### 정상 종료 순서
`quit`(또는 SIGTERM, SIGINT)을 받으면 `BYE`(또는 `command`) 핸들러가 종료 기록을 파이프에 넣는다. 메인 루프는 이 기록을 꺼낸 뒤 다음 순서로 정리한다.

1. `pthread_sigmask(SIG_BLOCK)`: 정리하는 동안 더 오는 시그널은 받지 않는다
2. `miner_del_all()`: 채굴기를 오래된 것부터 cancel/join
3. `wdt_stop()`: 워치독 스레드 종료 후 join
4. 최종 코인 수를 출력하고 `return 0`

원본 과제7은 `BYE` 핸들러에서 바로 `_exit(0)`을 불러 끝났다. 통합 버전에서 그렇게 하면 채굴기 스레드를 정리하지 못하고, 종료 직전에 도착해 있던 명령도 처리하지 못한다. 그래서 `BYE`는 이름과 역할(종료 요청)은 그대로 두고, 실제 종료는 메인 루프가 한다.

---

## 4. 어려웠던 문제와 해결 과정

### A. 채굴기 제거 순서 (원본 과제)
처음 만든 채굴기(`original/miner_troubleshooting.c`)는 `del`을 입력하면 엉뚱한 채굴기가 사라지거나 프로그램이 죽었다. 원인은 `del()`과 `dequeue()`의 순서였다.

```c
/* 실패한 버전 */
int dequeue() {
    if (front == rear) return -1;
    front++;                       /* (1) 먼저 front를 옮기고 */
    return queue[front];           /* (2) 옮긴 칸을 돌려준다 → 가장 오래된 칸이 아님 */
}
int del() {
    dequeue();                     /* front가 이미 다음 칸으로 넘어감, 반환값도 무시 */
    pthread_cancel(queue[front]);  /* 가장 오래된 채굴기가 아니라 그다음 채굴기를 취소 */
    pthread_join(queue[front], NULL);
}
```

`front++`를 먼저 하기 때문에 `del()`이 취소하는 것은 `queue[front]`, 즉 두 번째로 오래된 채굴기다. 가장 오래된 채굴기는 큐에서만 빠지고 스레드는 계속 돌아서, 누구도 취소할 수 없는 채굴기가 남는다.

컴파일 오류(`enqueue()`의 정의되지 않은 `value`)만 최소로 고친 사본으로 직접 실행해 확인했다.

| 상황 | 결과 |
|---|---|
| 채굴기 3개(0, 1, 2번) 뒤 `del` | 0번이 아니라 1번이 취소됨. 0번은 큐에서 빠진 채로 계속 채굴 |
| 채굴기 1개 뒤 `del` | 취소 대상 `queue[1]`이 한 번도 쓰지 않은 칸(값 0) → `pthread_cancel(0)`에서 Segmentation fault |
| 채굴기 2개 뒤 `del` 두 번 | 첫 `del`은 1번을 취소, 두 번째 `del`은 빈 칸 `queue[2]`를 취소하려다 Segmentation fault |
| 채굴기 없이 `del` | `dequeue()`는 -1을 돌려주지만 `del()`이 무시하고 `queue[0]`(값 0)을 취소 → Segmentation fault |

이 밖에도 `add()`는 `rear == SIZE` 검사보다 먼저 `pthread_create(&queue[rear], ...)`를 호출하기 때문에, 100개를 넘게 추가하면 배열 밖에 스레드 ID를 쓴다.

해결은 순서를 바꾸는 것이었다. `queue[front]`를 먼저 cancel 하고, join으로 종료를 확인한 다음, 둘 다 성공했을 때만 dequeue 한다. 이 순서는 통합 버전의 `miner_del()`에도 그대로 들어 있다.

```c
/* 고친 버전 (original/miner.c) */
pthread_t t = queue[front];
if (pthread_cancel(t) != 0) { ...; return; }
if (pthread_join(t, NULL) != 0) { ...; return; }
dequeue(&removed);                 /* 종료가 확인된 스레드만 큐에서 뺀다 */
```

### B. 같은 시그널이 합쳐져 명령이 사라짐
두 과제를 합친 뒤 `add 20`처럼 같은 명령을 여러 번 보내 보니, `SIGUSR1`을 20번 보냈는데 채굴기는 1개만 늘었다. 표준 시그널은 커널이 "도착했다"는 비트 하나로만 기억해서, 처리되기 전에 같은 시그널이 또 오면 하나로 합쳐진다. 과제7처럼 사람이 한 번씩 입력할 때는 처리 속도가 충분히 빨라 드러나지 않던 문제다.

| 방식 | 보낸 횟수 | 처리된 횟수 (5회 반복 합계) |
|---|---|---|
| `kill(pid, SIGUSR1)` 20번 연속 | 100 | 5 |
| `sigqueue(pid, SIGRTMIN, CMD_ADD)` 20번 연속 | 100 | 100 |

실시간 시그널(`SIGRTMIN`\~`SIGRTMAX`)은 보낸 만큼 큐에 쌓이기 때문에 사라지지 않는다. 처음에는 명령마다 다른 실시간 시그널(`SIGRTMIN+0`은 add, `+1`은 del, `+2`는 show, `+3`은 hang)을 쓰도록 바꿨다.

### C. 서로 다른 시그널의 순서가 바뀜
B를 고친 버전으로 `add 2`, `show`, `hang`, `show`를 한 번에 보냈더니 로그에서 순서가 바뀌어 있었다.

```
[  1.708] SIG    SIGRTMIN+0 from pid 433     ← add
[  1.708] SIG    SIGRTMIN+0 from pid 433     ← add
[  1.708] SIG    SIGRTMIN+2 from pid 433     ← show
[  1.708] SIG    SIGRTMIN+2 from pid 433     ← 마지막에 보낸 show가 hang보다 먼저 옴
[  1.708] SIG    SIGRTMIN+3 from pid 433     ← hang
```

워치독 재시작 직후 `show`, `add`, `show`, `3`(SIGTERM)을 보냈을 때는 더 심했다. 로그에 `SIGTERM` 하나만 찍히고 펌웨어가 종료되어, 앞의 세 명령이 처리되지 않았다.

여러 시그널이 동시에 대기 중이면 Linux는 보낸 순서가 아니라 시그널 번호 순서로 전달한다. 표준 시그널(`SIGTERM`은 15번)이 실시간 시그널보다 먼저 나가고, 실시간 시그널끼리는 번호가 작은 것부터 나간다. 그래서 `show`(+2)가 `hang`(+3)을 앞질렀고, `SIGTERM`이 먼저 처리되어 종료 경로로 들어가는 바람에 대기 중이던 명령이 버려졌다.

해결은 **실시간 시그널을 하나(`SIGRTMIN`)만 쓰고, 명령 번호는 `sigqueue()`의 값(`sival_int`)에 싣는 것**이다. 같은 실시간 시그널끼리는 보낸 순서(FIFO)대로 전달되고, 종료(`CMD_QUIT`)도 같은 시그널로 보내므로 앞의 명령보다 먼저 처리되지 않는다. 핸들러는 `siginfo_t.si_value.sival_int`를 기록에 담고, 메인 루프가 그 값을 이벤트로 바꾼다. 핸들러가 받은 순서대로 파이프에 쓰므로 이 순서는 메인 루프까지 그대로 간다. 테스트에서 `add 3` 직후 `quit`을 보내도 3개가 모두 추가된 뒤 종료되는 것을 확인했다.

### D. 워치독 재시작 후 PID 유지
User App은 시작할 때 받은 PID로만 시그널을 보낸다. 워치독이 timeout 때 프로세스를 끝내고 새로 띄우면 PID가 바뀌어 User App을 다시 실행해야 한다. 그래서 `fork()` 대신 `execv()`로 자기 자신을 덮어쓰도록 했다. exec는 같은 프로세스에서 프로그램만 바꾸므로 PID가 유지된다.

핸들러 방식이라 하나 더 챙겨야 했다. exec를 하면 등록한 핸들러는 모두 기본 동작(SIGUSR1/SIGTERM 등은 프로세스 종료)으로 돌아가는데, 시그널 마스크는 그대로 이어진다. exec는 시그널을 막아 둔 워치독 스레드에서 부르기 때문에, 새로 시작한 펌웨어는 시그널이 막힌 채로 시작한다. 그래서 핸들러를 다시 등록하기 전에 시그널이 와도 죽지 않고 대기했다가, 메인 스레드가 시그널을 여는 순간 새 핸들러로 전달된다.

남은 문제는 재시작 순간 파이프에 있던 명령이 사라진다는 것이다. `hang` 중에도 핸들러(ISR)는 계속 돌아서 `hang` 뒤에 보낸 `show`를 파이프에 넣지만, 파이프는 `O_CLOEXEC`라 exec와 함께 닫히고 그 기록도 버려진다. 실제 보드 리셋에서 RAM 내용이 사라지는 것과 같은 동작이라 그대로 두었다.

---

## 5. 테스트와 검증 결과

### 자동 테스트
```sh
make test        # 또는 sh tests/test_integration.sh
```
펌웨어를 백그라운드로 띄우고 User App에 표준 입력으로 명령을 넣은 뒤, 펌웨어 로그와 `/proc/<pid>/task`의 스레드 수, 종료 코드를 확인한다.

실행 환경: Ubuntu 24.04, gcc 13.3.0

| 분류 | 입력 | 기대 결과 | 결과 |
|---|---|---|---|
| 시작 | `./firmware` | `My PID : n` 출력, 스레드 3개(main, 워치독, 채굴기 1) | - |
| 추가 | `add 3` → `show` | `miners=4`, 스레드 6개 | - |
| 제거 순서 | `del` | 가장 오래된 `#1` 제거 | - |
| 빈 큐 제거 | `del 10` | `no miner to remove`, 비정상 종료 없음, 스레드 2개 | - |
| 채굴 | 채굴기 2개로 1초 | 코인 증가 (24 → 44) | - |
| 정상 종료 | `quit` | 채굴기 정리 후 `BYE`, 종료 코드 0 | - |
| 순서 보존 | `add`, `show`, `add`, `show`, `del`, `show` 연속 전송 | `show` 결과가 1, 2, 1 순서 | - |
| 종료 전 명령 | `add 3` 직후 `quit` | 3개 모두 추가된 뒤 종료 | - |
| 합쳐짐 재현 | `-l` 모드 `add 20` × 5회 | 100보다 적게 처리 (실제 5) | - |
| 합쳐짐 해결 | 기본 모드 `add 20` × 5회 | 100개 모두 처리 | - |
| 원본 호환 | `original/hw7_userapp.c`로 `1`, `1`, `2`, `3` | SIGUSR1/SIGUSR2 처리, SIGTERM으로 종료 코드 0 | - |
| 워치독 | `hang` (timeout 1000ms) | timeout 감지, 같은 PID로 재부팅, `resets=1`, 채굴기 1개로 초기화 | - |
| 반복 재시작 | `hang` 한 번 더 | `resets=2`, 이후 SIGTERM으로 정상 종료 | - |
| 재시작 끔 | `-x` + `hang` | 종료 코드 2 | - |
| 외부 종료 | `kill -INT <pid>` | 정상 종료 경로, 종료 코드 0 | - |
| 잘못된 PID | 종료된 PID로 User App 실행 | 시작 전에 거부, 종료 코드 1 | - |

시그널 수신을 핸들러 방식으로 바꾼 뒤에는 아직 Linux에서 다시 실행하지 않았다. `make test`로 24개 항목을 확인한 뒤 결과 칸을 채울 예정이다. (전용 스레드 `sigwaitinfo()`로 받던 이전 버전에서는 24개 모두 통과했고, `-fsanitize=thread` 빌드에서도 ThreadSanitizer 경고가 없었다.)

### 실행 예시
```
$ ./firmware                              $ ./user_app 4321
My PID : 4321                             target_pid:4321
[  0.000] BOOT   firmware start (wdt 3000 ms, reset count 0)
[  0.000] MINER  miner #1 added (total 1) INPUT >> add 2
[  0.503] SIG    SIGRTMIN from pid 1030 -> ADD
[  0.503] SIG    SIGRTMIN from pid 1030 -> ADD
[  0.503] MINER  miner #2 added (total 2)
[  0.503] MINER  miner #3 added (total 3) INPUT >> show
[  0.503] STATUS miners=3 coins=5 resets=0
[  0.503] MINER  miner #1 removed (total 2)   INPUT >> del
[  1.506] SIG    SIGRTMIN from pid 1035 -> HANG   INPUT >> hang
[  1.506] MAIN   simulated hang: main loop stops kicking WDT
[  4.604] WDT    main loop no kick for 3099 ms -> RESET
My PID : 4321
[  0.000] BOOT   firmware start (wdt 3000 ms, reset count 1)
[  0.000] MINER  miner #1 added (total 1)
[  0.504] STATUS miners=1 coins=5 resets=1    INPUT >> show
[  0.504] MAIN   shutdown: stopping 1 miners  INPUT >> quit
[  0.601] MAIN   BYE (coins=5)
```
*두 터미널의 출력을 시간 순서대로 나란히 정리한 것이다. 일부 `SIG` 줄은 생략했다.*

### 검증하지 않은 것
- 실제 임베디드 보드나 하드웨어 워치독(`/dev/watchdog`)에서는 실행하지 않았다.
- User App 여러 개가 동시에 보내는 경우는 순서를 확인하지 않았다. 한 User App 안에서의 순서만 확인했다.
- 실시간 시그널 대기 큐 한도(`RLIMIT_SIGPENDING`)에 닿을 만큼 많이 보내는 상황은 재현하지 않았다. User App은 `EAGAIN`이 나면 1ms 쉬고 다시 보내도록만 해 두었다.
- 파이프(Linux 기본 64KB, 기록 약 5000개)가 가득 차는 상황은 `hang` 상태에서만 생길 수 있고, 이때 넘친 시그널은 버리고 개수만 로그로 남긴다. 실제로 가득 채워 보지는 않았다.

---

## 6. 배운 점

1. 스레드 정리 순서: `pthread_cancel()`은 취소를 요청할 뿐이고, 실제로 끝났는지는 `pthread_join()`으로 확인해야 한다. 자료구조에서 지우는 일은 확인 다음이어야 한다는 것을 실패한 코드로 직접 겪었다.
2. 시그널은 "횟수"와 "순서"를 보장하지 않는다: 표준 시그널은 합쳐지고, 서로 다른 시그널은 번호 순서로 나간다. 사람이 천천히 입력할 때는 보이지 않다가, 프로그램이 연속으로 보내자 바로 드러났다. 명령 채널로 쓰려면 같은 실시간 시그널 하나에 값을 싣는 식으로 설계해야 한다.
3. 시그널과 스레드를 같이 쓰는 법: 핸들러 안에서 할 수 있는 일은 매우 적다(async-signal-safe 함수만). 원본처럼 핸들러를 쓰되 핸들러는 기록만 남기고, 핸들러가 메인 스레드에서만 돌도록 마스크를 나누니 ISR과 메인 루프를 나누는 펌웨어 구조와 그대로 맞았다.
4. 감시와 복구: 워치독은 "멈췄다"를 직접 알 수 없으니 "살아 있다는 신호가 끊겼다"로 판단한다는 것, 그리고 복구 방식(exec)에 따라 PID 유지 여부와 잃어버리는 상태가 달라진다는 것을 정리할 수 있었다.

---

## 빌드와 실행

```sh
make                 # firmware, user_app 빌드
./firmware           # 터미널 1: PID가 출력된다
./user_app <PID>     # 터미널 2: 명령 입력 (help로 목록 확인)
make test            # 통합 테스트
```

| 옵션 | 프로그램 | 설명 |
|---|---|---|
| `-n N` | firmware | 시작할 때 만들 채굴기 수 (기본 1) |
| `-t MS` | firmware | 워치독 timeout, ms (기본 3000) |
| `-x` | firmware | timeout 시 재시작하지 않고 종료 코드 2로 종료 |
| `-l` | user_app | 원본 과제처럼 `kill()`로 SIGUSR1/SIGUSR2/SIGTERM 전송 (B 재현용) |

### 디렉터리
```
src/
  firmware.c     시그널 핸들러, self-pipe, 메인 루프, 워치독 timeout 처리
  miner.c/.h     채굴기 스레드 원형 큐 (원본: 채굴기 과제)
  watchdog.c/.h  소프트웨어 워치독
  user_app.c     User App (원본: 과제7 User App)
  common.h       시그널 프로토콜 (명령 번호)
original/        제출했던 원본 과제 코드 (빌드 대상 아님)
  miner.c                   채굴기 과제 최종 버전
  miner_troubleshooting.c   채굴기 과제 실패 버전 (컴파일되지 않음, 4-A)
  hw7_firmware.c            과제7 Firmware App
  hw7_userapp.c             과제7 User App
tests/test_integration.sh  통합 테스트
```

---

## 한계와 개선 방향

### 코드에서 확인된 한계
- 응답 채널 없음: 시그널은 한 방향이라 User App은 명령이 처리됐는지 알 수 없다. 결과는 펌웨어 쪽 로그로만 확인한다.
- 재시작 시 상태 손실: 워치독 재시작 때 코인 수와 파이프에 남아 있던 명령이 사라진다 (4-D).
- 워치독이 메인 루프만 감시: 채굴기 스레드가 멈추는 경우는 감지하지 않는다.
- 보낸 사람 확인 없음: 같은 사용자 권한의 어떤 프로세스든 시그널을 보내 펌웨어를 제어할 수 있다. `si_pid`는 로그에만 쓴다.
- 코인 카운터 하나를 모든 채굴기가 mutex로 같이 쓴다: 채굴기가 많아지면 lock 경쟁이 늘어난다.

### 개선 아이디어 (진행하지 않음)
- 명령 결과를 돌려주는 응답 채널(FIFO, Unix 도메인 소켓) 추가
- 재시작 전에 상태를 파일에 저장하고 부팅 때 복구
- 스레드마다 heartbeat를 두고 워치독이 전부 감시
- 채굴기마다 카운터를 따로 두고 조회할 때 합산해 lock 경쟁 줄이기
- 실제 보드에서 `/dev/watchdog`와 연동
