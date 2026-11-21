#!/bin/sh
# Firmware App + User App 통합 테스트 (Linux)
# 사용법: make test  또는  sh tests/test_integration.sh
set -u
cd "$(dirname "$0")/.."
make -s all || exit 1

TMP=$(mktemp -d)
PASS=0
FAIL=0
FW_PID=""

cleanup() { [ -n "$FW_PID" ] && kill -9 "$FW_PID" 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT

check() {   # check "설명" 조건식
    desc=$1; shift
    if "$@"; then PASS=$((PASS+1)); echo "PASS  $desc"
    else          FAIL=$((FAIL+1)); echo "FAIL  $desc"; fi
}

start_fw() {   # start_fw [옵션...]
    ./firmware "$@" > "$TMP/fw.log" 2>&1 &
    FW_PID=$!
    sleep 0.3
}

send() {       # send "명령 줄들" [user_app 옵션]
    printf "$1" | ./user_app ${2:-} "$FW_PID" > "$TMP/ua.log" 2>&1
    sleep "${3:-0.3}"
}

log_has()   { grep -q -- "$1" "$TMP/fw.log"; }
log_count() { grep -c -- "$1" "$TMP/fw.log"; }
threads()   { ls "/proc/$FW_PID/task" 2>/dev/null | wc -l; }

stop_fw() {
    kill -TERM "$FW_PID" 2>/dev/null
    wait "$FW_PID" 2>/dev/null
    RC=$?
    FW_PID=""
}

echo "== 1. 기본 동작"
start_fw
check "PID 출력 ('My PID : n')" log_has "My PID : $FW_PID"
check "시작 시 채굴기 1개 (스레드 = main+signal+wdt+1 = 4)" test "$(threads)" -eq 4
send 'add 3\nshow\n'
check "add 3 -> 채굴기 4개" log_has "miners=4"
check "스레드 수 7 (채굴기 4 + 3)" test "$(threads)" -eq 7
send 'del\n'
check "del -> 가장 오래된 #1 제거" log_has "miner #1 removed (total 3)"
send 'del 10\nshow\n'
check "빈 큐에서 del -> 거부 메시지, 비정상 종료 없음" log_has "no miner to remove"
check "del 후 스레드 3개 (채굴기 0)" test "$(threads)" -eq 3
c1=$(grep STATUS "$TMP/fw.log" | tail -1 | sed 's/.*coins=\([0-9]*\).*/\1/')
send 'add 2\n' "" 1.0
send 'show\n'
c2=$(grep STATUS "$TMP/fw.log" | tail -1 | sed 's/.*coins=\([0-9]*\).*/\1/')
check "채굴기 2개 1초 -> 코인 증가 ($c1 -> $c2)" test "$c2" -gt "$c1"
send 'quit\n' "" 0.5
wait "$FW_PID"; RC=$?; FW_PID=""
check "quit -> 종료 코드 0" test "$RC" -eq 0
check "quit -> 채굴기 정리 후 BYE" log_has "BYE"

echo "== 2. 명령 순서 보존 (sigqueue 단일 실시간 시그널)"
start_fw -n 0
send 'add\nshow\nadd\nshow\ndel\nshow\n'
seq=$(grep STATUS "$TMP/fw.log" | sed 's/.*miners=\([0-9]*\).*/\1/' | tr '\n' ' ')
check "add,show,add,show,del,show -> miners 1 2 1 (실제: $seq)" test "$seq" = "1 2 1 "
send 'add 3\nquit\n' "" 0.5
wait "$FW_PID" 2>/dev/null; FW_PID=""
check "add 3 직후 quit -> add 3개가 quit보다 먼저 처리" test "$(log_count 'added')" -eq 5

echo "== 3. 시그널 합쳐짐 비교 (20번 연속 전송, 5회 반복)"
for mode in legacy rt; do
    total=0
    for i in 1 2 3 4 5; do
        start_fw -n 0
        if [ $mode = legacy ]; then send 'add 20\n' -l; else send 'add 20\n'; fi
        total=$((total + $(log_count 'added')))
        stop_fw
    done
    echo "      $mode: 100번 보내서 $total 개 추가"
    if [ $mode = legacy ]; then LEG=$total; else RT=$total; fi
done
check "kill(SIGUSR1) x100 -> 100개보다 적게 처리 (합쳐짐 재현)" test "$LEG" -lt 100
check "sigqueue(SIGRTMIN) x100 -> 100개 모두 처리" test "$RT" -eq 100

echo "== 4. kill() 방식 (-l) 명령"
start_fw -n 0
send '1
1
2
3
' -l 0.5
wait "$FW_PID" 2>/dev/null; RC=$?; FW_PID=""
check "-l 모드 1, 2 -> SIGUSR1, SIGUSR2 처리" test "$(log_count 'SIGUSR1 from pid')" -eq 1 -a "$(log_count 'SIGUSR2 from pid')" -eq 1
check "-l 모드 3 -> SIGTERM으로 정상 종료 (코드 0)" test "$RC" -eq 0

echo "== 5. 워치독"
start_fw -t 1000
send 'add 2\nhang\n' "" 2.0
check "hang -> WDT timeout 감지" log_has "WDT    main loop no kick"
check "재시작 후 같은 PID로 다시 부팅" test "$(log_count "My PID : $FW_PID")" -eq 2
send 'show\n'
check "재시작 후 resets=1, 채굴기는 초기 상태 1개" log_has "miners=1 coins=[0-9]* resets=1"
send 'hang\n' "" 2.0
send 'show\n'
check "두 번째 hang -> resets=2" log_has "resets=2"
stop_fw
check "재시작된 펌웨어도 SIGTERM으로 정상 종료" test "$RC" -eq 0

start_fw -t 1000 -x
send 'hang\n' "" 2.0
wait "$FW_PID" 2>/dev/null; RC=$?; FW_PID=""
check "-x 옵션: hang -> 종료 코드 2" test "$RC" -eq 2

echo "== 6. 예외 상황"
start_fw
kill -INT "$FW_PID"; wait "$FW_PID"; RC=$?; DEAD=$FW_PID; FW_PID=""
check "SIGINT(Ctrl+C) -> 정상 종료 경로 (코드 0)" test "$RC" -eq 0
./user_app "$DEAD" > "$TMP/ua.log" 2>&1; RC=$?
check "없는 PID -> User App이 시작 전에 거부" test "$RC" -eq 1

echo
echo "결과: PASS $PASS / FAIL $FAIL"
[ "$FAIL" -eq 0 ]
