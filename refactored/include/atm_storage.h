#ifndef ATM_STORAGE_H
#define ATM_STORAGE_H

#include <stddef.h>

#include "atm_types.h"

int atm_load_accounts(
    const char *filename,
    AccountInfo *accounts,
    size_t capacity,
    size_t *out_count
);

int atm_save_accounts(
    const char *filename,
    const AccountInfo *accounts,
    size_t account_count
);

int atm_load_cards(
    const char *filename,
    CardInfo *cards,
    size_t capacity,
    size_t *out_count
);

int atm_find_account_number_by_card(
    const char *filename,
    const char *card_number,
    char *account_number,
    size_t account_number_size
);

int atm_is_card_expired(const char *expire_date);
int atm_get_timestamp(char *buffer, size_t buffer_size);

#endif
