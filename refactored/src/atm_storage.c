#include "atm_storage.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static int get_local_time(time_t now, struct tm *out_tm)
{
    struct tm *tmp;

    if (out_tm == NULL) {
        return 0;
    }

#ifdef _WIN32
    return localtime_s(out_tm, &now) == 0;
#else
    tmp = localtime(&now);
    if (tmp == NULL) {
        return 0;
    }
    *out_tm = *tmp;
    return 1;
#endif
}

int atm_load_accounts(
    const char *filename,
    AccountInfo *accounts,
    size_t capacity,
    size_t *out_count
)
{
    FILE *fp;
    char line[256];
    size_t count = 0;

    if (filename == NULL || accounts == NULL || out_count == NULL) {
        return 0;
    }

    fp = fopen(filename, "r");
    if (fp == NULL) {
        return 0;
    }

    while (count < capacity && fgets(line, sizeof(line), fp) != NULL) {
        AccountInfo acc;
        int parsed = sscanf(
            line,
            "%6s %31s %ld %ld %ld %4s",
            acc.account_number,
            acc.owner_name,
            &acc.balance,
            &acc.daily_withdraw_limit,
            &acc.daily_used,
            acc.pin
        );

        if (parsed != 6) {
            continue;
        }

        accounts[count++] = acc;
    }

    fclose(fp);
    *out_count = count;
    return 1;
}

int atm_save_accounts(
    const char *filename,
    const AccountInfo *accounts,
    size_t account_count
)
{
    FILE *fp;
    char temp_filename[260];
    size_t i;

    if (filename == NULL || accounts == NULL) {
        return 0;
    }

    if (snprintf(temp_filename, sizeof(temp_filename), "%s.tmp", filename) < 0) {
        return 0;
    }

    fp = fopen(temp_filename, "w");
    if (fp == NULL) {
        return 0;
    }

    for (i = 0; i < account_count; ++i) {
        if (fprintf(
                fp,
                "%s %s %ld %ld %ld %s\n",
                accounts[i].account_number,
                accounts[i].owner_name,
                accounts[i].balance,
                accounts[i].daily_withdraw_limit,
                accounts[i].daily_used,
                accounts[i].pin
            ) < 0) {
            fclose(fp);
            remove(temp_filename);
            return 0;
        }
    }

    if (fclose(fp) != 0) {
        remove(temp_filename);
        return 0;
    }

    if (rename(temp_filename, filename) != 0) {
        remove(filename);
        if (rename(temp_filename, filename) != 0) {
            remove(temp_filename);
            return 0;
        }
    }

    return 1;
}

int atm_load_cards(
    const char *filename,
    CardInfo *cards,
    size_t capacity,
    size_t *out_count
)
{
    FILE *fp;
    char line[128];
    size_t count = 0;

    if (filename == NULL || cards == NULL || out_count == NULL) {
        return 0;
    }

    fp = fopen(filename, "r");
    if (fp == NULL) {
        return 0;
    }

    while (count < capacity && fgets(line, sizeof(line), fp) != NULL) {
        CardInfo card;
        int parsed = sscanf(
            line,
            "%8s %6s",
            card.card_number,
            card.expire_date
        );

        if (parsed != 2) {
            continue;
        }

        card.is_valid = 0;
        cards[count++] = card;
    }

    fclose(fp);
    *out_count = count;
    return 1;
}

int atm_find_account_number_by_card(
    const char *filename,
    const char *card_number,
    char *account_number,
    size_t account_number_size
)
{
    FILE *fp;
    char line[128];
    char stored_card[ATM_CARD_NUMBER_LEN];
    char stored_account[ATM_ACCOUNT_NUMBER_LEN];

    if (filename == NULL ||
        card_number == NULL ||
        account_number == NULL ||
        account_number_size < ATM_ACCOUNT_NUMBER_LEN) {
        return 0;
    }

    fp = fopen(filename, "r");
    if (fp == NULL) {
        return 0;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (sscanf(line, "%8s %6s", stored_card, stored_account) != 2) {
            continue;
        }

        if (strcmp(stored_card, card_number) == 0) {
            snprintf(account_number, account_number_size, "%s", stored_account);
            fclose(fp);
            return 1;
        }
    }

    fclose(fp);
    return 0;
}

int atm_is_card_expired(const char *expire_date)
{
    int year;
    int month;
    time_t now;
    struct tm local_tm;

    if (expire_date == NULL ||
        strlen(expire_date) != 6U ||
        sscanf(expire_date, "%4d%2d", &year, &month) != 2 ||
        month < 1 || month > 12) {
        return 1;
    }

    now = time(NULL);
    if (!get_local_time(now, &local_tm)) {
        return 1;
    }

    if (year < local_tm.tm_year + 1900) {
        return 1;
    }

    if (year == local_tm.tm_year + 1900 && month < local_tm.tm_mon + 1) {
        return 1;
    }

    return 0;
}

int atm_get_timestamp(char *buffer, size_t buffer_size)
{
    time_t now;
    struct tm local_tm;

    if (buffer == NULL || buffer_size < ATM_TIMESTAMP_LEN) {
        return 0;
    }

    now = time(NULL);
    if (!get_local_time(now, &local_tm)) {
        return 0;
    }

    return strftime(
        buffer,
        buffer_size,
        "%Y-%m-%d %H:%M",
        &local_tm
    ) > 0U;
}
