#include "includes.h"
#include "ATM.h"

#define TASK_STK_SIZE 512
#define MAX_ACCOUNTS 10

// 각 task가 사용할 Stack 공간 정의 
OS_STK CardTaskStk[TASK_STK_SIZE];
OS_STK AuthTaskStk[TASK_STK_SIZE];
OS_STK TransTaskStk[TASK_STK_SIZE];
OS_STK CashTaskStk[TASK_STK_SIZE];
OS_STK LogTaskStk[TASK_STK_SIZE];
OS_STK ResetTaskStk[TASK_STK_SIZE];

// task 간 통신을 위한 메시지 큐
OS_EVENT *CardToAuthQ;
OS_EVENT *AuthToTransQ;
OS_EVENT *TransToCashQ;
OS_EVENT *LogQ;

// 공유 자원 보호를 위한 세마포어
OS_EVENT *AccountSem;
OS_EVENT *CashBoxSem;

// 구조체 정의
typedef struct {
    char card_number[9];        // 카드 번호
    int is_valid;               // 카드 유효성 (0: 만료, 1: 유효)
    char expire_date[7];        // 카드 유효 날짜
} CardInfo;


typedef struct {
    char account_number[7];     // 계좌 번호
    int transaction_type;       // 거래 종류 (0:출금, 1:입금, 2:조회, 3:이체)
    long amount;                // 거래 금액
    char transaction_time[20];  // 거래 시간
    int status;                 // 상태 (0:진행중, 1:완료, 2:실패)
    char error_msg[50];         // 에러 메시지
} TransactionInfo;


typedef struct {
    char account_number[7];     // 인증된 계좌 번호
    int auth_ok;                // 인증 성공 여부
} AuthResult;


// 계좌 데이터
AccountInfo accounts[MAX_ACCOUNTS];
int account_count = 0;

// 계좌 데이터 파일 로드 함수
void LoadAccountsFromFile(const char* filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        printf("[시스템] 계좌 파일을 열 수 없습니다.\n");
        return;
    }

    char line[128];
    while (fgets(line, sizeof(line), fp) && account_count < MAX_ACCOUNTS) {
        AccountInfo acc;
        sscanf(line, "%s %s %ld %d %d %s",
               acc.account_number,
               acc.owner_name,
               &acc.balance,
               &acc.daily_withdraw_limit,
               &acc.daily_used,
               acc.pin
            );
        accounts[account_count++] = acc;
    }
    fclose(fp);
    printf("[시스템] %d개의 계좌를 불러왔습니다.\n", account_count);
}


// 카드-계좌 매핑 함수
int FindAccountByCard(CardInfo *card) {
    FILE *fp = fopen("card_map.txt", "r");
    if (!fp) {
        printf("[시스템] 카드 매핑 파일을 열 수 없습니다.\n");
        return -1;
    }
    char line[64], card_no[9], acc_no[7];
    while (fgets(line, sizeof(line), fp)) {
        sscanf(line, "%s %s", card_no, acc_no);
        if (strcmp(card->card_number, card_no) == 0) {
            fclose(fp);
            for (int i = 0; i < account_count; i++) {
                if (strcmp(accounts[i].account_number, acc_no) == 0)
                    return i;
            }
            return -1;
        }
    }
    fclose(fp);
    return -1;
}


// 카드 유효성 검사 함수    
int IsCardExpired(const char *expire_date) {
    int year, month;
    sscanf(expire_date, "%4d%2d", &year, &month);

    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    int curr_year = t->tm_year + 1900;
    int curr_month = t->tm_mon + 1;

    if (year < curr_year) return 1;
    if (year == curr_year && month < curr_month) return 1;
    return 0;
}


// 현재 시간을 문자열로 반환하는 함수
void GetTimestamp(char *buf) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, 30, "%Y-%m-%d %H:%M", t);
}


// 계좌 데이터를 파일로 저장하는 함수
void SaveAccountsToFile(const char* filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        printf("[시스템] 계좌 파일 저장 실패!\n");
        return;
    }

    for (int i = 0; i < account_count; i++) {
        fprintf(fp, "%s %s %ld %d %d %s\n",
                accounts[i].account_number,
                accounts[i].owner_name,
                accounts[i].balance,
                accounts[i].daily_withdraw_limit,
                accounts[i].daily_used,
                accounts[i].pin);
    }

    fclose(fp);
}


// 거래 실패 메시지를 로그 큐에 추가하는 함수
void PostFailedTrans(TransactionInfo *trans, const char* account_number, const char* err_msg) {
    trans->amount = 0;
    strcpy(trans->account_number, account_number);
    strcpy(trans->error_msg, err_msg);
    trans->status = 2; // 실패 상태
    GetTimestamp(trans->transaction_time);
    OSQPost(LogQ, (void*)trans); // LogTask로 전송
}


// 카드 인식 Task
// cards.txt에서 카드 정보를 무작위로 선택하여 ATM에 삽입된 것처럼 처리
// 유효기간이 지난 카드는 거절하고, 유효한 카드는 다음 인증 Task로 전송
void CardReaderTask(void *pdata) {
    while (1) {
        CardInfo *card = (CardInfo*)malloc(sizeof(CardInfo)); // 카드 정보를 저장할 구조체 동적 할당
        printf("\n=== ATM 기계 시뮬레이션 ===\n");
        srand(time(NULL)); // 랜덤 카드 선택을 위해 난수 시드 설정

        // 카드 정보 자동 설정
        FILE *fp = fopen("cards.txt", "r");
        if (!fp) {
            printf("[시스템] 카드 정보 파일을 열 수 없습니다.\n");
            free(card); // 메모리 해제
            OSTimeDlyHMSM(0,0,3,0); // 3초 대기
            continue;
        }

        // cards.txt의 각 줄을 읽어서 배열에 저장
        char lines[10][20]; // 최대 10개의 카드 정보 저장
        int line_cnt = 0;
        while (fgets(lines[line_cnt], sizeof(lines[0]), fp) && line_cnt < 10) {
            line_cnt++;     // 카드 정보 개수 카운트
        }
        fclose(fp);

        if (line_cnt == 0) {
            printf("[시스템] 카드 정보가 없습니다.\n");
            free(card); // 메모리 해제
            OSTimeDlyHMSM(0,0,3,0); // 3초 대기
            continue;
        }

        int random = rand() % line_cnt;
        sscanf(lines[random], "%s %s", card->card_number, card->expire_date); // 랜덤으로 선택된 카드 정보를 구조체에 저장

        printf("[카드인식] 카드가 삽입되었습니다. (카드 번호: %s)\n", card->card_number);
        OSTimeDlyHMSM(0,0,0,300); // 300ms 대기

        card->is_valid = !IsCardExpired(card->expire_date); // 카드 유효성 검사

        if (!card->is_valid) {
            printf("[카드인식] 카드 유효기간 만료 → 카드 반환\n");
            free(card); // 메모리 해제
            OSTimeDlyHMSM(0,0,3,0); // 3초 대기
            continue;
        }

        printf("[카드인식] 카드 정보 읽는 중... 완료\n");
        OSQPost(CardToAuthQ, (void*)card); // 인증 Task로 전송
        OSTimeDlyHMSM(0,0,5,0); // 5초 대기
    }
}

// 인증 Task
// 카드 정보를 받고 계좌 정보를 확인하여 인증을 수행 
// 인증 성공 시, AuthResult를 TransactionTask로 전송
void AuthTask(void *pdata) {
    void *msg;
    INT8U err;
    while (1) {
        msg = OSQPend(CardToAuthQ, 0, &err); // CardToAuthQ에서 카드 메시지 수신
        CardInfo *card = (CardInfo*)msg;

        // 카드 정보를 이용하여 계좌 인덱스 찾기
        int acc_index = FindAccountByCard(card); 
        if (acc_index < 0) {
            printf("[PIN인증] 계좌를 찾을 수 없습니다 → 카드 반환\n");
            free(card); // 메모리 해제
            continue;
        }

        int attempt = 0;
        int auth_success = 0;
        char pin_input[20];

        while (attempt < 3) {
            printf("[PIN인증] PIN 번호 4자리를 입력하세요: ");
            fgets(pin_input, sizeof(pin_input), stdin);
            pin_input[strcspn(pin_input, "\n")] = 0; // 입력 버퍼에서 개행 문자 제거

            // 입력된 PIN이 4자리가 아니면 실패
            if (strlen(pin_input) != 4) {
                printf("[PIN인증] 비밀번호 오류! (%d/3)\n", attempt + 1);
                attempt++;
                continue;
            }

            // 입력된 PIN이 숫자가 아니면 실패
            int is_digit = 1;
            for (int i = 0; i < 4; i++) {
                if (!isdigit(pin_input[i])) {
                    is_digit = 0;
                    break;
                }
            }
            if (!is_digit) {
                printf("[PIN인증] 비밀번호 오류! (%d/3)\n", attempt + 1);
                attempt++;
                continue;
            }

            // 입력된 PIN이 계좌의 PIN과 일치하면 인증 성공
            if (strncmp(pin_input, accounts[acc_index].pin, 4) == 0 && strlen(accounts[acc_index].pin) == 4) {
                auth_success = 1;
                break;
            } else {
                printf("[PIN인증] 비밀번호 오류! (%d/3)\n", attempt + 1);
                attempt++;
            }
        }

        // 3회 실패 시 카드 반환
        if (!auth_success) {
            printf("[PIN인증] 3회 실패 → 카드 반환\n");
            free(card);
            continue;
        }

        printf("[PIN인증] 인증 중... 성공\n");

        // 인증 결과를 AuthResult 구조체에 저장 후, TransactionTask로 전송
        AuthResult *result = (AuthResult*)malloc(sizeof(AuthResult));
        strcpy(result->account_number, accounts[acc_index].account_number);
        result->auth_ok = 1;

        OSQPost(AuthToTransQ, (void*)result);  // 인증 성공 결과를 TransactionTask로 전송
        free(card); // 카드 정보 메모리 해제
    }
}

// 거래 처리 Task
// 인증 Task로부터 받은 계좌에 대해 출금, 입금, 잔액조회, 이체를 처리
// 출금 요청은 CashDispenserTask로 전달
// 입금/잔액조회/이체 요청은 직접 처리하고 LogTask로 결과 전달
void TransactionTask(void *pdata) {
    void *msg;
    INT8U err;
    while (1) {
        // 인증 Task로부터 인증 결과 수신
        msg = OSQPend(AuthToTransQ, 0, &err);
        AuthResult *auth = (AuthResult*)msg;

        // 사용자에게 거래 선택 요청
        printf("[거래처리] 거래 메뉴를 선택하세요\n");
        printf("           1. 출금  2. 입금  3. 잔액조회  4. 이체\n");
        int choice;
        printf("[거래처리] 선택: ");
        scanf("%d", &choice); getchar();

        // 거래 정보 초기화
        TransactionInfo *trans = (TransactionInfo*)malloc(sizeof(TransactionInfo));
        trans->transaction_type = choice - 1;
        trans->status = 0; // 진행 중인 상태
        strcpy(trans->error_msg, ""); // 기본 에러 메시지
        strcpy(trans->account_number, auth->account_number);

        // 계좌 인덱스 탐색
        int acc_index = -1;
        for (int i = 0; i < account_count; i++) {
            if (strcmp(accounts[i].account_number, auth->account_number) == 0) {
                acc_index = i;
                break;
            }
        }

        // 계좌를 찾을 수 없으면 거래 거절
        if (acc_index == -1) {
            printf("[거래처리] 계좌를 찾을 수 없습니다.\n");
            PostFailedTrans(trans, auth->account_number, "계좌를 찾을 수 없습니다.");
            free(auth);
            continue;
        }

        // 출금 선택
        if (choice == 1) {                              
            printf("[거래처리] 출금을 선택하셨습니다.\n");
            printf("[거래처리] 출금액을 입력하세요: ");
            scanf("%ld", &trans->amount); getchar();

            OSSemPend(AccountSem, 0, &err); // 계좌 정보 동기화
            // 잔액 부족
            if (trans->amount > accounts[acc_index].balance) {
                printf("[거래처리] 잔액 부족으로 거래 거절\n");
                PostFailedTrans(trans, auth->account_number, "잔액 부족");
                OSSemPost(AccountSem); // 계좌 정보 동기화
                free(auth);
                continue;
            }
            // 일일 출금 한도 초과
            if (accounts[acc_index].daily_used + trans->amount > accounts[acc_index].daily_withdraw_limit) {
                printf("[거래처리] 일일 출금 한도 초과로 거래 거절\n");
                printf("[안내] 오늘 남은 출금 가능 금액은 %d원입니다.\n",
                    accounts[acc_index].daily_withdraw_limit - accounts[acc_index].daily_used);
                PostFailedTrans(trans, auth->account_number, "일일 출금 한도 초과");
                OSSemPost(AccountSem); // 계좌 정보 동기화
                free(auth);
                continue;
            }
            OSSemPost(AccountSem); // 계좌 정보 동기화

            // 여기서 계좌 차감하지 않고 출금 Task로 넘기기
            trans->status = 0; // 진행 중인 상태
            OSQPost(TransToCashQ, (void*)trans); // 출금 Task로 전달
        }

        // 입금 선택
        else if(choice == 2){                           
            printf("[거래처리] 입금을 선택하셨습니다.\n");
            printf("[거래처리] 입금액을 입력하세요: ");
            scanf("%ld", &trans->amount); getchar();
            
            OSSemPend(AccountSem, 0, &err); // 계좌 정보 동기화
            accounts[acc_index].balance += trans->amount;
            OSSemPost(AccountSem); // 계좌 정보 동기화

            trans->status = 1; // 성공
            GetTimestamp(trans->transaction_time); // 거래 시간 저장
            SaveAccountsToFile("accounts.txt");
                
            printf("[거래처리] %ld원 입금 완료. 현재 잔액: %ld원\n",
                   trans->amount, accounts[acc_index].balance);

            OSQPost(LogQ, (void*)trans); // LogTask로 전달
        }

        // 잔액조회 선택
        else if(choice == 3){                           
            OSSemPend(AccountSem, 0, &err); // 계좌 정보 동기화
            printf("[거래처리] 잔액조회를 선택하셨습니다.\n");
            printf("[거래처리] 현재 잔액: %ld원\n", accounts[acc_index].balance);
            OSSemPost(AccountSem); // 계좌 정보 동기화

            trans->amount = 0; // 금액 없음
            trans->status = 1; // 성공
            GetTimestamp(trans->transaction_time); // 거래 시간 저장

            OSQPost(LogQ, (void*)trans); // LogTask로 전달
        }

        // 이체 선택            
        else if(choice == 4){                           
            printf("[거래처리] 이체를 선택하셨습니다.\n");
            char target_account[7];
            printf("[거래처리] 이체 대상 계좌번호 입력: ");
            scanf("%s", target_account); getchar();
            printf("[거래처리] 이체액을 입력하세요: ");
            scanf("%ld", &trans->amount); getchar();

            // 이체 대상 계좌 찾기
            int target_index = -1;
            for (int i = 0; i < account_count; i++) {
                if (strcmp(accounts[i].account_number, target_account) == 0) {
                    target_index = i;
                    break;
                }
            }

            if (target_index == -1) {
                printf("[거래처리] 이체 대상 계좌가 존재하지 않습니다.\n");
                PostFailedTrans(trans, auth->account_number, "이체 대상 계좌가 존재하지 않습니다.");
                free(auth);
                continue;
            }

            OSSemPend(AccountSem, 0, &err); // 계좌 정보 동기화

            // 잔액 부족
            if (trans->amount > accounts[acc_index].balance) {
                printf("[거래처리] 잔액 부족으로 이체 실패\n");
                PostFailedTrans(trans, auth->account_number, "이체 잔액 부족");
                OSSemPost(AccountSem); // 계좌 정보 동기화
                free(auth);
                continue;
            }

            // 이체 처리
            accounts[acc_index].balance -= trans->amount; // 출금 계좌에 금액 차감
            accounts[target_index].balance += trans->amount; // 입금 계좌에 금액 추가
            OSSemPost(AccountSem); // 계좌 정보 동기화
            
            trans->status = 1; // 성공
            GetTimestamp(trans->transaction_time); // 거래 시간 저장
            SaveAccountsToFile("accounts.txt");

            printf("[거래처리] %ld원 이체 완료. 현재 잔액: %ld원\n",
                   trans->amount, accounts[acc_index].balance);

            OSQPost(LogQ, (void*)trans); // LogTask로 전달
        }
        free(auth); // 메모리 해제
    }
}

// 현금 출납 Task
// 출금 요청을 처리하고 ATM 내부의 현금량을 관리하며 계좌 금액을 차감
void CashDispenserTask(void *pdata) {
    void *msg;
    INT8U err;

    while (1) {
        // 거래 Task로부터 출금 요청 수신
        msg = OSQPend(TransToCashQ, 0, &err);
        TransactionInfo *trans = (TransactionInfo*)msg;
        
        long cash_stock = 200000;  // ATM 내부의 현금량

        // 출금일 때만 현금 출납 처리
        if (trans->transaction_type == 0) {
            // 계좌 인덱스 탐색
            int acc_index = -1;
            for (int i = 0; i < account_count; i++) {
                if (strcmp(accounts[i].account_number, trans->account_number) == 0) {
                    acc_index = i;
                    break;
                }
            }

            OSSemPend(CashBoxSem, 0, &err); // 현금 상자 동기화

            // 현금 부족 시 처리 거부
            if (trans->amount > cash_stock) {
                printf("[현금출납] 현금 부족! 관리자 호출 필요\n");
                PostFailedTrans(trans, trans->account_number, "ATM 현금 부족");
                OSSemPost(CashBoxSem); // 현금 상자 동기화
                continue;
            }

            // ATM 내 현금 차감
            cash_stock -= trans->amount;
            
            // 계좌에서 출금액 차감
            OSSemPend(AccountSem, 0, &err); // 계좌 정보 동기화
            accounts[acc_index].balance -= trans->amount;
            accounts[acc_index].daily_used += trans->amount;
            SaveAccountsToFile("accounts.txt");
            OSSemPost(AccountSem); // 계좌 정보 동기화

            printf("[현금출납] 현금 준비 중... %ld원 출금 완료\n", trans->amount);
        
            trans->status = 1; // 성공
            GetTimestamp(trans->transaction_time); // 거래 시간 저장
            OSSemPost(CashBoxSem); // 현금 상자 동기화
        }
        OSQPost(LogQ, (void*)trans); // LogTask로 전달
    }
}

// 로그 관리 Task
// 모든 거래 결과를 log.txt에 기록
void LogTask(void *pdata) {
    void *msg;
    INT8U err;
    FILE *log_fp;

    while (1) {
        // LogQ로부터 거래 결과 수신
        msg = OSQPend(LogQ, 0, &err);
        TransactionInfo *trans = (TransactionInfo*)msg;

        log_fp = fopen("log.txt", "a");
        if (!log_fp) {
            printf("[로그관리] log.txt 파일을 열 수 없습니다.\n");
            free(trans);
            continue;
        }

        // 거래 내용 파일에 기록
        fprintf(log_fp, "[거래기록]\n");
        fprintf(log_fp, "계좌번호    : %s\n", trans->account_number);
        fprintf(log_fp, "거래종류    : %s\n",
                trans->transaction_type == 0 ? "출금" :
                trans->transaction_type == 1 ? "입금" :
                trans->transaction_type == 2 ? "잔액조회" : "이체");
        fprintf(log_fp, "거래금액    : %ld원\n", trans->amount);
        fprintf(log_fp, "거래시간    : %s\n", trans->transaction_time);
        fprintf(log_fp, "상태        : %s\n",
                trans->status == 0 ? "진행중" :
                trans->status == 1 ? "성공" : "실패");
        if (trans->status == 2) {
            fprintf(log_fp, "에러메시지  : %s\n", trans->error_msg);
        }
        fprintf(log_fp, "-----------------------------------\n");

        fclose(log_fp);

        printf("[시스템] 거래가 완료되었습니다. 카드를 회수하세요.\n");

        free(trans); // 거래 정보 동적 할당 해제
        OSTimeDlyHMSM(0, 0, 0, 500);
    }
}

// 일일 사용액 초기화 Task
// 일정 시간(1분)마다 모든 계좌의 일일 사용액을 초기화하고 파일에 저장
void ResetTask(void *pdata) {
    while (1) {
        OSTimeDlyHMSM(0, 1, 0, 0);  // 시연을 위해 1분에 한 번씩 일일 사용액 초기화
        OSResetDailyLimits(); // 커널 함수 호출, daily_used 초기화
        SaveAccountsToFile("accounts.txt"); // 계좌 정보 파일 저장
    }
}


// 메인 함수
int main(void) {
    OSInit();                               // uC/OS-II 커널 초기화
    srand(time(NULL));                      // 랜덤 초기화
    LoadAccountsFromFile("accounts.txt");   // 계좌 정보 파일 로드

    // 세마포어 생성
    AccountSem = OSSemCreate(1);              // 계좌 정보 동기화
    CashBoxSem = OSSemCreate(1);              // 현금 상자 동기화

    // 메시지 큐 생성
    void *cardBuf[3], *authBuf[3], *transBuf[3], *logBuf[10];
    CardToAuthQ = OSQCreate(&cardBuf[0], 3);            // 카드 Task -> 인증 Task
    AuthToTransQ = OSQCreate(&authBuf[0], 3);           // 인증 Task -> 거래 Task
    TransToCashQ = OSQCreate(&transBuf[0], 3);          // 거래 Task -> 현금 Task
    LogQ = OSQCreate(&logBuf[0], 10);                   // 거래 Task & 현금 Task -> 로그 Task

    // 태스크 생성
    OSTaskCreate(CardReaderTask, 0, &CardTaskStk[TASK_STK_SIZE-1], 6);      // 카드 인식
    OSTaskCreate(AuthTask, 0, &AuthTaskStk[TASK_STK_SIZE-1], 5);            // 인증
    OSTaskCreate(TransactionTask, 0, &TransTaskStk[TASK_STK_SIZE-1], 4);    // 거래
    OSTaskCreate(CashDispenserTask, 0, &CashTaskStk[TASK_STK_SIZE-1], 3);   // 현금
    OSTaskCreate(LogTask, 0, &LogTaskStk[TASK_STK_SIZE-1], 10);             // 로그
    OSTaskCreate(ResetTask, 0, &ResetTaskStk[TASK_STK_SIZE-1], 1);         // 일일 사용액 초기화

    OSStart(); // 멀티태스킹 시작
    return 0;
}
