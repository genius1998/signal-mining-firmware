CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2 -g
LDFLAGS += -pthread
CFLAGS  += -pthread

BIN := firmware user_app

all: $(BIN)

firmware: src/firmware.c src/miner.c src/watchdog.c src/miner.h src/watchdog.h src/common.h
	$(CC) $(CFLAGS) -o $@ src/firmware.c src/miner.c src/watchdog.c $(LDFLAGS)

user_app: src/user_app.c src/common.h
	$(CC) $(CFLAGS) -o $@ src/user_app.c $(LDFLAGS)

test: all
	sh tests/test_integration.sh

clean:
	rm -f $(BIN)

.PHONY: all test clean
