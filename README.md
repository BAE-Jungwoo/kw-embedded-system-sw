# 광운대학교 임베디드시스템SW설계 (Embedded System SW Design)

광운대학교 컴퓨터정보공학부 **임베디드시스템SW설계** 강의 자료. RTOS(µC/OS-II) 기반 임베디드 시스템 설계 학습 및 ATM 시뮬레이터를 텀 프로젝트로 구현.

- 학번: 2019202084

## 자료

### Assignment 1 & 2
- `[Assignment1&2]2019202084_배정우.pdf` — 초기 과제 보고서

### 텀 프로젝트 — ATM Machine 시뮬레이터
- `[PROPOSAL]2019202084_배정우.pdf` — 프로젝트 제안서
- `[FINAL]2019202084_배정우/` — 최종 산출물
  - `ATM.h`, `ATM_machine.c`: ATM 본체 (계좌 관리, 잔액, 출금 한도, PIN 검증)
  - `INCLUDES.H`: µC/OS-II 헤더 모음
  - `accounts.txt`, `cards.txt`, `card_map.txt`: 계좌/카드 데이터 파일
  - `makefile`: 빌드 스크립트
  - `ATM_machine.exe`: 빌드 산출물 (Windows 실행)
  - `[FINAL]2019202084_배정우.pdf`: 최종 보고서

## 핵심 코드 (요약)
```c
// ATM.h
typedef struct {
    char account_number[7];
    char owner_name[10];
    long balance;
    int  daily_withdraw_limit;
    int  daily_used;
    char pin[5];
} AccountInfo;

extern OS_EVENT *AccountSem;   // µC/OS-II 세마포어
```

- **µC/OS-II RTOS**의 `OS_EVENT` 세마포어로 동시 접근 제어
- 계좌별 일일 출금 한도, PIN 인증, 카드↔계좌 매핑

## 빌드
```bash
make
./ATM_machine
```
