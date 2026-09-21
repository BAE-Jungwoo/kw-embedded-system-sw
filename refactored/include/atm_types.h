#ifndef ATM_TYPES_H
#define ATM_TYPES_H

#include <stddef.h>

#define ATM_MAX_ACCOUNTS 10
#define ATM_MAX_CARDS 10

#define ATM_ACCOUNT_NUMBER_LEN 7
#define ATM_CARD_NUMBER_LEN 9
#define ATM_EXPIRE_DATE_LEN 7
#define ATM_PIN_LEN 5
#define ATM_OWNER_NAME_LEN 32
#define ATM_TIMESTAMP_LEN 20
#define ATM_ERROR_MSG_LEN 64

typedef enum {
    ATM_TRANS_WITHDRAW = 0,
    ATM_TRANS_DEPOSIT,
    ATM_TRANS_BALANCE,
    ATM_TRANS_TRANSFER
} TransactionType;

typedef enum {
    ATM_STATUS_PENDING = 0,
    ATM_STATUS_SUCCESS,
    ATM_STATUS_FAILED
} TransactionStatus;

typedef enum {
    ATM_OK = 0,
    ATM_ERR_INVALID_ARGUMENT,
    ATM_ERR_INVALID_AMOUNT,
    ATM_ERR_INSUFFICIENT_BALANCE,
    ATM_ERR_DAILY_LIMIT,
    ATM_ERR_ACCOUNT_NOT_FOUND,
    ATM_ERR_SAME_ACCOUNT
} AtmResult;

typedef struct {
    char account_number[ATM_ACCOUNT_NUMBER_LEN];
    char owner_name[ATM_OWNER_NAME_LEN];
    long balance;
    long daily_withdraw_limit;
    long daily_used;
    char pin[ATM_PIN_LEN];
} AccountInfo;

typedef struct {
    char card_number[ATM_CARD_NUMBER_LEN];
    int is_valid;
    char expire_date[ATM_EXPIRE_DATE_LEN];
} CardInfo;

typedef struct {
    char account_number[ATM_ACCOUNT_NUMBER_LEN];
    int auth_ok;
} AuthResult;

typedef struct {
    char account_number[ATM_ACCOUNT_NUMBER_LEN];
    char target_account[ATM_ACCOUNT_NUMBER_LEN];
    TransactionType transaction_type;
    long amount;
    char transaction_time[ATM_TIMESTAMP_LEN];
    TransactionStatus status;
    char error_msg[ATM_ERROR_MSG_LEN];
} TransactionInfo;

#endif
