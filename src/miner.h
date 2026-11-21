#ifndef MINER_H
#define MINER_H

#define MINER_MAX 100

void miner_init(void);
int  miner_add(void);           /* 성공 시 새 채굴기 번호, 실패 시 -1 */
int  miner_del(void);           /* 성공 시 제거한 번호, 비었거나 실패 시 -1 */
int  miner_count(void);
long miner_coins(void);
void miner_del_all(void);
void miner_destroy(void);

#endif
