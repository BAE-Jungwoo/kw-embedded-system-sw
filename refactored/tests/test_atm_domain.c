#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "atm_domain.h"

static AccountInfo make_account(
    const char *number,
    long balance,
    long daily_limit,
    long daily_used
)
{
    AccountInfo account;

    memset(&account, 0, sizeof(account));
    snprintf(account.account_number, sizeof(account.account_number), "%s", number);
    account.balance = balance;
    account.daily_withdraw_limit = daily_limit;
    account.daily_used = daily_used;
    snprintf(account.pin, sizeof(account.pin), "%s", "1234");

    return account;
}

static void test_amount_validation(void)
{
    assert(atm_is_valid_amount(1));
    assert(!atm_is_valid_amount(0));
    assert(!atm_is_valid_amount(-1000));
}

static void test_pin_validation(void)
{
    assert(atm_is_valid_pin("1234", "1234"));
    assert(!atm_is_valid_pin("12a4", "1234"));
    assert(!atm_is_valid_pin("123", "1234"));
    assert(!atm_is_valid_pin("9999", "1234"));
}

static void test_withdraw(void)
{
    AccountInfo account = make_account("100001", 100000, 50000, 10000);

    assert(atm_withdraw(&account, 20000) == ATM_OK);
    assert(account.balance == 80000);
    assert(account.daily_used == 30000);

    assert(atm_withdraw(&account, -1000) == ATM_ERR_INVALID_AMOUNT);
    assert(account.balance == 80000);

    assert(atm_withdraw(&account, 30000) == ATM_ERR_DAILY_LIMIT);
    assert(account.balance == 80000);

    assert(atm_withdraw(&account, 90000) == ATM_ERR_INSUFFICIENT_BALANCE);
}

static void test_deposit(void)
{
    AccountInfo account = make_account("100001", 10000, 100000, 0);

    assert(atm_deposit(&account, 5000) == ATM_OK);
    assert(account.balance == 15000);

    assert(atm_deposit(&account, 0) == ATM_ERR_INVALID_AMOUNT);
    assert(atm_deposit(&account, -5000) == ATM_ERR_INVALID_AMOUNT);
    assert(account.balance == 15000);
}

static void test_transfer(void)
{
    AccountInfo source = make_account("100001", 50000, 100000, 0);
    AccountInfo target = make_account("100002", 10000, 100000, 0);

    assert(atm_transfer(&source, &target, 20000) == ATM_OK);
    assert(source.balance == 30000);
    assert(target.balance == 30000);

    assert(atm_transfer(&source, &target, -1) == ATM_ERR_INVALID_AMOUNT);
    assert(source.balance == 30000);
    assert(target.balance == 30000);

    assert(atm_transfer(&source, &target, 40000) == ATM_ERR_INSUFFICIENT_BALANCE);
    assert(atm_transfer(&source, &source, 1000) == ATM_ERR_SAME_ACCOUNT);
}

static void test_reset_and_find(void)
{
    AccountInfo accounts[2];

    accounts[0] = make_account("100001", 10000, 100000, 5000);
    accounts[1] = make_account("100002", 20000, 100000, 10000);

    assert(atm_find_account(accounts, 2, "100002") == 1);
    assert(atm_find_account(accounts, 2, "999999") == -1);

    atm_reset_daily_usage(accounts, 2);
    assert(accounts[0].daily_used == 0);
    assert(accounts[1].daily_used == 0);
}

int main(void)
{
    test_amount_validation();
    test_pin_validation();
    test_withdraw();
    test_deposit();
    test_transfer();
    test_reset_and_find();

    puts("All ATM domain tests passed.");
    return 0;
}
