#include "includes.h"

#include "atm_domain.h"
#include "atm_storage.h"
#include "atm_types.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TASK_STK_SIZE 512
#define CARD_QUEUE_SIZE 3
#define AUTH_QUEUE_SIZE 3
#define CASH_QUEUE_SIZE 3
#define LOG_QUEUE_SIZE 10

#define CARD_POOL_BLOCKS 4
#define AUTH_POOL_BLOCKS 4
#define TRANS_POOL_BLOCKS 12

#define ACCOUNT_MUTEX_PRIO 2
#define CASH_MUTEX_PRIO 3

#define CASH_TASK_PRIO 4
#define TRANSACTION_TASK_PRIO 5
#define AUTH_TASK_PRIO 6
#define CARD_TASK_PRIO 7
#define RESET_TASK_PRIO 11
#define LOG_TASK_PRIO 12

#define INITIAL_CASH_STOCK 200000L

#define ACCOUNTS_FILE "data/accounts.txt"
#define CARDS_FILE "data/cards.txt"
#define CARD_MAP_FILE "data/card_map.txt"
#define LOG_FILE "data/log.txt"

static OS_STK card_stk[TASK_STK_SIZE];
static OS_STK auth_stk[TASK_STK_SIZE];
static OS_STK trans_stk[TASK_STK_SIZE];
static OS_STK cash_stk[TASK_STK_SIZE];
static OS_STK log_stk[TASK_STK_SIZE];
static OS_STK reset_stk[TASK_STK_SIZE];

static OS_EVENT *card_to_auth_q;
static OS_EVENT *auth_to_trans_q;
static OS_EVENT *trans_to_cash_q;
static OS_EVENT *log_q;

static void *card_q_storage[CARD_QUEUE_SIZE];
static void *auth_q_storage[AUTH_QUEUE_SIZE];
static void *cash_q_storage[CASH_QUEUE_SIZE];
static void *log_q_storage[LOG_QUEUE_SIZE];

static OS_EVENT *account_lock;
static OS_EVENT *cash_lock;

static AccountInfo accounts[ATM_MAX_ACCOUNTS];
static size_t account_count;
static CardInfo cards[ATM_MAX_CARDS];
static size_t card_count;
static long cash_stock = INITIAL_CASH_STOCK;

#if OS_MEM_EN > 0
typedef union { void *align; CardInfo value; } CardBlock;
typedef union { void *align; AuthResult value; } AuthBlock;
typedef union { void *align; TransactionInfo value; } TransactionBlock;

static CardBlock card_pool[CARD_POOL_BLOCKS];
static AuthBlock auth_pool[AUTH_POOL_BLOCKS];
static TransactionBlock trans_pool[TRANS_POOL_BLOCKS];

static OS_MEM *card_mem;
static OS_MEM *auth_mem;
static OS_MEM *trans_mem;
#endif

static int read_line(const char *prompt, char *buf, size_t size)
{
    size_t len;

    if (buf == NULL || size == 0U) {
        return 0;
    }

    if (prompt != NULL) {
        printf("%s", prompt);
        fflush(stdout);
    }

    if (fgets(buf, (int)size, stdin) == NULL) {
        return 0;
    }

    len = strcspn(buf, "\r\n");
    buf[len] = '\0';
    return 1;
}

static int read_long_value(const char *prompt, long *value)
{
    char buf[64];
    char *end;
    long parsed;

    if (value == NULL || !read_line(prompt, buf, sizeof(buf))) {
        return 0;
    }

    errno = 0;
    parsed = strtol(buf, &end, 10);

    while (*end == ' ' || *end == '\t') {
        ++end;
    }

    if (errno != 0 || end == buf || *end != '\0') {
        return 0;
    }

    *value = parsed;
    return 1;
}

static int create_lock(OS_EVENT **lock, INT8U ceiling)
{
    INT8U err;

#if OS_MUTEX_EN > 0
    *lock = OSMutexCreate(ceiling, &err);
    return *lock != NULL && err == OS_NO_ERR;
#else
    (void)ceiling;
    *lock = OSSemCreate(1);
    return *lock != NULL;
#endif
}

static int lock_take(OS_EVENT *lock)
{
    INT8U err;

#if OS_MUTEX_EN > 0
    OSMutexPend(lock, 0, &err);
#else
    OSSemPend(lock, 0, &err);
#endif

    return err == OS_NO_ERR;
}

static void lock_give(OS_EVENT *lock)
{
#if OS_MUTEX_EN > 0
    (void)OSMutexPost(lock);
#else
    (void)OSSemPost(lock);
#endif
}

static CardInfo *card_alloc(void)
{
#if OS_MEM_EN > 0
    INT8U err;
    CardInfo *p = (CardInfo *)OSMemGet(card_mem, &err);
    return err == OS_NO_ERR ? p : NULL;
#else
    return (CardInfo *)malloc(sizeof(CardInfo));
#endif
}

static void card_free(CardInfo *p)
{
    if (p == NULL) return;
#if OS_MEM_EN > 0
    (void)OSMemPut(card_mem, p);
#else
    free(p);
#endif
}

static AuthResult *auth_alloc(void)
{
#if OS_MEM_EN > 0
    INT8U err;
    AuthResult *p = (AuthResult *)OSMemGet(auth_mem, &err);
    return err == OS_NO_ERR ? p : NULL;
#else
    return (AuthResult *)malloc(sizeof(AuthResult));
#endif
}

static void auth_free(AuthResult *p)
{
    if (p == NULL) return;
#if OS_MEM_EN > 0
    (void)OSMemPut(auth_mem, p);
#else
    free(p);
#endif
}

static TransactionInfo *trans_alloc(void)
{
    TransactionInfo *p;
#if OS_MEM_EN > 0
    INT8U err;
    p = (TransactionInfo *)OSMemGet(trans_mem, &err);
    if (err != OS_NO_ERR) p = NULL;
#else
    p = (TransactionInfo *)malloc(sizeof(TransactionInfo));
#endif

    if (p != NULL) memset(p, 0, sizeof(*p));
    return p;
}

static void trans_free(TransactionInfo *p)
{
    if (p == NULL) return;
#if OS_MEM_EN > 0
    (void)OSMemPut(trans_mem, p);
#else
    free(p);
#endif
}

static int post(OS_EVENT *queue, void *message)
{
    return queue != NULL && message != NULL &&
           OSQPost(queue, message) == OS_NO_ERR;
}

static void stamp(TransactionInfo *t)
{
    (void)atm_get_timestamp(t->transaction_time, sizeof(t->transaction_time));
}

static void log_or_free(TransactionInfo *t)
{
    if (!post(log_q, t)) {
        printf("[시스템] 로그 큐 전송 실패\n");
        trans_free(t);
    }
}

static void fail_transaction(TransactionInfo *t, const char *reason)
{
    t->status = ATM_STATUS_FAILED;
    snprintf(t->error_msg, sizeof(t->error_msg), "%s", reason);
    stamp(t);
    log_or_free(t);
}

static int account_index(const char *number)
{
    return atm_find_account(accounts, account_count, number);
}

static int save_accounts(void)
{
    return atm_save_accounts(ACCOUNTS_FILE, accounts, account_count);
}

static void CardReaderTask(void *pdata)
{
    (void)pdata;

    for (;;) {
        CardInfo *card;
        size_t selected;

        if (card_count == 0U) {
            OSTimeDlyHMSM(0, 0, 2, 0);
            continue;
        }

        card = card_alloc();
        if (card == NULL) {
            printf("[카드인식] 메시지 메모리 부족\n");
            OSTimeDlyHMSM(0, 0, 1, 0);
            continue;
        }

        selected = (size_t)(rand() % (int)card_count);
        *card = cards[selected];

        printf("\n[카드인식] 카드 삽입: %s\n", card->card_number);

        card->is_valid = !atm_is_card_expired(card->expire_date);
        if (!card->is_valid) {
            printf("[카드인식] 유효기간 만료 → 카드 반환\n");
            card_free(card);
        } else if (!post(card_to_auth_q, card)) {
            printf("[카드인식] 인증 큐 Full → 요청 폐기\n");
            card_free(card);
        }

        OSTimeDlyHMSM(0, 0, 5, 0);
    }
}

static void AuthTask(void *pdata)
{
    INT8U err;
    (void)pdata;

    for (;;) {
        CardInfo *card = (CardInfo *)OSQPend(card_to_auth_q, 0, &err);
        char mapped_account[ATM_ACCOUNT_NUMBER_LEN];
        char pin[32];
        int idx;
        int attempt;
        int authenticated = 0;
        AuthResult *result;

        if (err != OS_NO_ERR || card == NULL) continue;

        if (!atm_find_account_number_by_card(
                CARD_MAP_FILE,
                card->card_number,
                mapped_account,
                sizeof(mapped_account))) {
            printf("[PIN인증] 카드 매핑 없음\n");
            card_free(card);
            continue;
        }

        if (!lock_take(account_lock)) {
            card_free(card);
            continue;
        }
        idx = account_index(mapped_account);
        lock_give(account_lock);

        if (idx < 0) {
            card_free(card);
            continue;
        }

        for (attempt = 1; attempt <= 3; ++attempt) {
            if (read_line("[PIN인증] PIN 4자리: ", pin, sizeof(pin)) &&
                atm_is_valid_pin(pin, accounts[idx].pin)) {
                authenticated = 1;
                break;
            }
            printf("[PIN인증] 오류 (%d/3)\n", attempt);
        }

        card_free(card);

        if (!authenticated) {
            printf("[PIN인증] 3회 실패 → 카드 반환\n");
            continue;
        }

        result = auth_alloc();
        if (result == NULL) continue;

        memset(result, 0, sizeof(*result));
        snprintf(result->account_number, sizeof(result->account_number),
                 "%s", mapped_account);
        result->auth_ok = 1;

        if (!post(auth_to_trans_q, result)) {
            auth_free(result);
        }
    }
}

static void TransactionTask(void *pdata)
{
    INT8U err;
    (void)pdata;

    for (;;) {
        AuthResult *auth = (AuthResult *)OSQPend(auth_to_trans_q, 0, &err);
        TransactionInfo *t;
        long choice;
        int idx;

        if (err != OS_NO_ERR || auth == NULL) continue;

        printf("[거래처리] 1.출금 2.입금 3.잔액조회 4.이체\n");
        if (!read_long_value("[거래처리] 선택: ", &choice) ||
            choice < 1 || choice > 4) {
            printf("[거래처리] 잘못된 메뉴 입력\n");
            auth_free(auth);
            continue;
        }

        t = trans_alloc();
        if (t == NULL) {
            auth_free(auth);
            continue;
        }

        t->transaction_type = (TransactionType)(choice - 1);
        t->status = ATM_STATUS_PENDING;
        snprintf(t->account_number, sizeof(t->account_number),
                 "%s", auth->account_number);
        auth_free(auth);

        if (t->transaction_type == ATM_TRANS_WITHDRAW) {
            if (!read_long_value("[거래처리] 출금액: ", &t->amount) ||
                !atm_is_valid_amount(t->amount)) {
                fail_transaction(t, "출금액은 0보다 커야 합니다.");
            } else if (!post(trans_to_cash_q, t)) {
                fail_transaction(t, "현금 출납 큐가 가득 찼습니다.");
            }
            continue;
        }

        if (t->transaction_type == ATM_TRANS_DEPOSIT) {
            AtmResult result;

            if (!read_long_value("[거래처리] 입금액: ", &t->amount) ||
                !atm_is_valid_amount(t->amount)) {
                fail_transaction(t, "입금액은 0보다 커야 합니다.");
                continue;
            }

            if (!lock_take(account_lock)) {
                fail_transaction(t, "계좌 잠금 실패");
                continue;
            }

            idx = account_index(t->account_number);
            result = idx >= 0
                ? atm_deposit(&accounts[idx], t->amount)
                : ATM_ERR_ACCOUNT_NOT_FOUND;

            if (result == ATM_OK) {
                (void)save_accounts();
                printf("[거래처리] 입금 완료. 잔액: %ld원\n",
                       accounts[idx].balance);
            }

            lock_give(account_lock);

            if (result != ATM_OK) {
                fail_transaction(t, atm_result_message(result));
            } else {
                t->status = ATM_STATUS_SUCCESS;
                stamp(t);
                log_or_free(t);
            }
            continue;
        }

        if (t->transaction_type == ATM_TRANS_BALANCE) {
            long balance;

            if (!lock_take(account_lock)) {
                fail_transaction(t, "계좌 잠금 실패");
                continue;
            }

            idx = account_index(t->account_number);
            if (idx < 0) {
                lock_give(account_lock);
                fail_transaction(t, "계좌를 찾을 수 없습니다.");
                continue;
            }

            balance = accounts[idx].balance;
            lock_give(account_lock);

            printf("[거래처리] 현재 잔액: %ld원\n", balance);
            t->status = ATM_STATUS_SUCCESS;
            stamp(t);
            log_or_free(t);
            continue;
        }

        if (t->transaction_type == ATM_TRANS_TRANSFER) {
            char target[ATM_ACCOUNT_NUMBER_LEN];
            int target_idx;
            AtmResult result;

            if (!read_line("[거래처리] 대상 계좌번호: ", target, sizeof(target)) ||
                !read_long_value("[거래처리] 이체액: ", &t->amount) ||
                !atm_is_valid_amount(t->amount)) {
                fail_transaction(t, "잘못된 이체 입력");
                continue;
            }

            snprintf(t->target_account, sizeof(t->target_account), "%s", target);

            if (!lock_take(account_lock)) {
                fail_transaction(t, "계좌 잠금 실패");
                continue;
            }

            idx = account_index(t->account_number);
            target_idx = account_index(t->target_account);

            if (idx < 0 || target_idx < 0) {
                result = ATM_ERR_ACCOUNT_NOT_FOUND;
            } else {
                result = atm_transfer(&accounts[idx], &accounts[target_idx],
                                      t->amount);
            }

            if (result == ATM_OK) {
                (void)save_accounts();
                printf("[거래처리] 이체 완료. 잔액: %ld원\n",
                       accounts[idx].balance);
            }

            lock_give(account_lock);

            if (result != ATM_OK) {
                fail_transaction(t, atm_result_message(result));
            } else {
                t->status = ATM_STATUS_SUCCESS;
                stamp(t);
                log_or_free(t);
            }
        }
    }
}

static void CashDispenserTask(void *pdata)
{
    INT8U err;
    (void)pdata;

    for (;;) {
        TransactionInfo *t =
            (TransactionInfo *)OSQPend(trans_to_cash_q, 0, &err);
        int idx;
        AtmResult result;

        if (err != OS_NO_ERR || t == NULL) continue;

        if (!lock_take(cash_lock)) {
            fail_transaction(t, "현금 상자 잠금 실패");
            continue;
        }

        if (t->amount > cash_stock) {
            lock_give(cash_lock);
            fail_transaction(t, "ATM 현금 부족");
            continue;
        }

        if (!lock_take(account_lock)) {
            lock_give(cash_lock);
            fail_transaction(t, "계좌 잠금 실패");
            continue;
        }

        idx = account_index(t->account_number);
        result = idx >= 0
            ? atm_withdraw(&accounts[idx], t->amount)
            : ATM_ERR_ACCOUNT_NOT_FOUND;

        if (result == ATM_OK) {
            cash_stock -= t->amount;
            (void)save_accounts();
        }

        lock_give(account_lock);
        lock_give(cash_lock);

        if (result != ATM_OK) {
            fail_transaction(t, atm_result_message(result));
            continue;
        }

        printf("[현금출납] %ld원 출금 완료, ATM 현금 %ld원\n",
               t->amount, cash_stock);
        t->status = ATM_STATUS_SUCCESS;
        stamp(t);
        log_or_free(t);
    }
}

static const char *transaction_name(TransactionType type)
{
    switch (type) {
    case ATM_TRANS_WITHDRAW: return "출금";
    case ATM_TRANS_DEPOSIT: return "입금";
    case ATM_TRANS_BALANCE: return "잔액조회";
    case ATM_TRANS_TRANSFER: return "이체";
    default: return "알 수 없음";
    }
}

static void LogTask(void *pdata)
{
    INT8U err;
    (void)pdata;

    for (;;) {
        TransactionInfo *t = (TransactionInfo *)OSQPend(log_q, 0, &err);
        FILE *fp;

        if (err != OS_NO_ERR || t == NULL) continue;

        fp = fopen(LOG_FILE, "a");
        if (fp != NULL) {
            fprintf(fp, "[거래기록]\n");
            fprintf(fp, "계좌번호 : %s\n", t->account_number);
            fprintf(fp, "거래종류 : %s\n", transaction_name(t->transaction_type));
            if (t->transaction_type == ATM_TRANS_TRANSFER) {
                fprintf(fp, "대상계좌 : %s\n", t->target_account);
            }
            fprintf(fp, "거래금액 : %ld원\n", t->amount);
            fprintf(fp, "거래시간 : %s\n", t->transaction_time);
            fprintf(fp, "상태     : %s\n",
                    t->status == ATM_STATUS_SUCCESS ? "성공" : "실패");
            if (t->status == ATM_STATUS_FAILED) {
                fprintf(fp, "오류     : %s\n", t->error_msg);
            }
            fprintf(fp, "------------------------------\n");
            fclose(fp);
        }

        trans_free(t);
    }
}

static void ResetTask(void *pdata)
{
    (void)pdata;

    for (;;) {
        /* Demo interval. Production code should use the real day boundary. */
        OSTimeDlyHMSM(0, 1, 0, 0);

        if (lock_take(account_lock)) {
            atm_reset_daily_usage(accounts, account_count);
            (void)save_accounts();
            lock_give(account_lock);
        }
    }
}

static int init_memory(void)
{
#if OS_MEM_EN > 0
    INT8U err;

    card_mem = OSMemCreate(card_pool, CARD_POOL_BLOCKS,
                           sizeof(card_pool[0]), &err);
    if (card_mem == NULL || err != OS_NO_ERR) return 0;

    auth_mem = OSMemCreate(auth_pool, AUTH_POOL_BLOCKS,
                           sizeof(auth_pool[0]), &err);
    if (auth_mem == NULL || err != OS_NO_ERR) return 0;

    trans_mem = OSMemCreate(trans_pool, TRANS_POOL_BLOCKS,
                            sizeof(trans_pool[0]), &err);
    if (trans_mem == NULL || err != OS_NO_ERR) return 0;
#endif
    return 1;
}

static int init_rtos_objects(void)
{
    if (!create_lock(&account_lock, ACCOUNT_MUTEX_PRIO) ||
        !create_lock(&cash_lock, CASH_MUTEX_PRIO)) {
        return 0;
    }

    card_to_auth_q = OSQCreate(card_q_storage, CARD_QUEUE_SIZE);
    auth_to_trans_q = OSQCreate(auth_q_storage, AUTH_QUEUE_SIZE);
    trans_to_cash_q = OSQCreate(cash_q_storage, CASH_QUEUE_SIZE);
    log_q = OSQCreate(log_q_storage, LOG_QUEUE_SIZE);

    return card_to_auth_q != NULL &&
           auth_to_trans_q != NULL &&
           trans_to_cash_q != NULL &&
           log_q != NULL &&
           init_memory();
}

int main(void)
{
    OSInit();
    srand((unsigned int)time(NULL));

    if (!atm_load_accounts(ACCOUNTS_FILE, accounts, ATM_MAX_ACCOUNTS,
                           &account_count) ||
        !atm_load_cards(CARDS_FILE, cards, ATM_MAX_CARDS, &card_count)) {
        printf("[시스템] 데이터 파일 로드 실패\n");
        return 1;
    }

    if (!init_rtos_objects()) {
        printf("[시스템] RTOS 객체 초기화 실패\n");
        return 1;
    }

    if (OSTaskCreate(CashDispenserTask, NULL,
                     &cash_stk[TASK_STK_SIZE - 1], CASH_TASK_PRIO) != OS_NO_ERR ||
        OSTaskCreate(TransactionTask, NULL,
                     &trans_stk[TASK_STK_SIZE - 1], TRANSACTION_TASK_PRIO) != OS_NO_ERR ||
        OSTaskCreate(AuthTask, NULL,
                     &auth_stk[TASK_STK_SIZE - 1], AUTH_TASK_PRIO) != OS_NO_ERR ||
        OSTaskCreate(CardReaderTask, NULL,
                     &card_stk[TASK_STK_SIZE - 1], CARD_TASK_PRIO) != OS_NO_ERR ||
        OSTaskCreate(ResetTask, NULL,
                     &reset_stk[TASK_STK_SIZE - 1], RESET_TASK_PRIO) != OS_NO_ERR ||
        OSTaskCreate(LogTask, NULL,
                     &log_stk[TASK_STK_SIZE - 1], LOG_TASK_PRIO) != OS_NO_ERR) {
        printf("[시스템] Task 생성 실패\n");
        return 1;
    }

    printf("[시스템] 계좌 %lu개, 카드 %lu개, ATM 현금 %ld원\n",
           (unsigned long)account_count,
           (unsigned long)card_count,
           cash_stock);

    OSStart();
    return 0;
}
