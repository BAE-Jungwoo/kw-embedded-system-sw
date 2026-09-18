#include "atm_domain.h"

#include <ctype.h>
#include <string.h>

int atm_is_valid_amount(long amount)
{
    return amount > 0;
}

int atm_is_valid_pin(const char *input, const char *expected_pin)
{
    size_t i;

    if (input == NULL || expected_pin == NULL) {
        return 0;
    }

    if (strlen(input) != 4U || strlen(expected_pin) != 4U) {
        return 0;
    }

    for (i = 0; i < 4U; ++i) {
        if (!isdigit((unsigned char)input[i])) {
            return 0;
        }
    }

    return strcmp(input, expected_pin) == 0;
}

int atm_find_account(
    const AccountInfo *accounts,
    size_t account_count,
    const char *account_number
)
{
    size_t i;

    if (accounts == NULL || account_number == NULL) {
        return -1;
    }

    for (i = 0; i < account_count; ++i) {
        if (strcmp(accounts[i].account_number, account_number) == 0) {
            return (int)i;
        }
    }

    return -1;
}

AtmResult atm_deposit(AccountInfo *account, long amount)
{
    if (account == NULL) {
        return ATM_ERR_INVALID_ARGUMENT;
    }

    if (!atm_is_valid_amount(amount)) {
        return ATM_ERR_INVALID_AMOUNT;
    }

    account->balance += amount;
    return ATM_OK;
}

AtmResult atm_withdraw(AccountInfo *account, long amount)
{
    if (account == NULL) {
        return ATM_ERR_INVALID_ARGUMENT;
    }

    if (!atm_is_valid_amount(amount)) {
        return ATM_ERR_INVALID_AMOUNT;
    }

    if (amount > account->balance) {
        return ATM_ERR_INSUFFICIENT_BALANCE;
    }

    if (account->daily_used + amount > account->daily_withdraw_limit) {
        return ATM_ERR_DAILY_LIMIT;
    }

    account->balance -= amount;
    account->daily_used += amount;
    return ATM_OK;
}

AtmResult atm_transfer(AccountInfo *source, AccountInfo *target, long amount)
{
    if (source == NULL || target == NULL) {
        return ATM_ERR_INVALID_ARGUMENT;
    }

    if (source == target ||
        strcmp(source->account_number, target->account_number) == 0) {
        return ATM_ERR_SAME_ACCOUNT;
    }

    if (!atm_is_valid_amount(amount)) {
        return ATM_ERR_INVALID_AMOUNT;
    }

    if (amount > source->balance) {
        return ATM_ERR_INSUFFICIENT_BALANCE;
    }

    source->balance -= amount;
    target->balance += amount;
    return ATM_OK;
}

void atm_reset_daily_usage(AccountInfo *accounts, size_t account_count)
{
    size_t i;

    if (accounts == NULL) {
        return;
    }

    for (i = 0; i < account_count; ++i) {
        accounts[i].daily_used = 0;
    }
}

const char *atm_result_message(AtmResult result)
{
    switch (result) {
    case ATM_OK:
        return "성공";
    case ATM_ERR_INVALID_ARGUMENT:
        return "잘못된 인자";
    case ATM_ERR_INVALID_AMOUNT:
        return "거래 금액은 0보다 커야 합니다.";
    case ATM_ERR_INSUFFICIENT_BALANCE:
        return "잔액 부족";
    case ATM_ERR_DAILY_LIMIT:
        return "일일 출금 한도 초과";
    case ATM_ERR_ACCOUNT_NOT_FOUND:
        return "계좌를 찾을 수 없습니다.";
    case ATM_ERR_SAME_ACCOUNT:
        return "동일 계좌로 이체할 수 없습니다.";
    default:
        return "알 수 없는 오류";
    }
}
