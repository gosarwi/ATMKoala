#ifndef ATM_LOCALE_H
#define ATM_LOCALE_H

/* Minimal, extensible localisation layer.  English is the only shipped
 * catalogue; further catalogues can be added without changing Exp logic. */
#include <stdint.h>

typedef enum {
    L10N_LANG_EN = 0,
    L10N_LANG_RU,
    L10N_LANG_DE,
    L10N_LANG_MAX
} l10n_language_t;

typedef enum {
    L10N_LANGUAGE,
    L10N_LANGUAGE_DESC,
    L10N_ENGLISH,
    L10N_RUSSIAN,
    L10N_GERMAN,
    L10N_ONLY_ENGLISH,
    L10N_APPEARANCE,
    L10N_SYSTEM,
    L10N_ABOUT,
    L10N_EXP_MENU,
    L10N_SETTINGS,
    L10N_COUNT
} l10n_key_t;

void l10n_init(void);
const char *l10n_get(l10n_key_t key);
const char *l10n_current_code(void);
const char *l10n_current_name(void);
uint32_t l10n_available_count(void);
const char *l10n_available_code(uint32_t index);
const char *l10n_available_name(uint32_t index);
int l10n_set(const char *code);

#endif
