#ifndef WATCHDOG_H
#define WATCHDOG_H

/* timeout_ms 동안 kick이 없으면 on_timeout()을 WDT 스레드에서 호출한다 */
int  wdt_start(int timeout_ms, void (*on_timeout)(long elapsed_ms));
void wdt_kick(void);
void wdt_stop(void);

#endif
