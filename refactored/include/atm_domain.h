#ifndef ATM_DOMAIN_H
#define ATM_DOMAIN_H

#include "atm_types.h"

int atm_is_valid_amount(long amount);
int atm_is_valid_pin(const char *input, const char *expected_pin);

int atm_find_account(
    const AccountInfo *accounts,
    size_t account_count,
    const char *account_number
);

AtmResult atm_deposit(AccountInfo *account, long amount);
AtmResult atm_withdraw(AccountInfo *account, long amount);
AtmResult atm_transfer(AccountInfo *source, AccountInfo *target, long amount);

void atm_reset_daily_usage(AccountInfo *accounts, size_t account_count);
const char *atm_result_message(AtmResult result);

#endif
