#ifndef ATM_H
#define ATM_H

#include "includes.h"

#define MAX_ACCOUNTS 10


typedef struct {
    char account_number[7];     // 계좌 번호
    char owner_name[10];        // 예금주
    long balance;               // 잔액
    int daily_withdraw_limit;   // 일일 출금 한도
    int daily_used;             // 일일 사용액
    char pin[5];                // PIN 번호
} AccountInfo;

// 외부 변수 선언 (정의는 .c 파일에 있음)
extern AccountInfo accounts[MAX_ACCOUNTS];
extern int account_count;
extern OS_EVENT *AccountSem;

#endif