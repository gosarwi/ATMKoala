#include "locale.h"
#include "config.h"
#include "util.h"

static l10n_language_t current_language = L10N_LANG_EN;

static const char *const english_catalog[L10N_COUNT] = {
    [L10N_LANGUAGE]="Language",[L10N_LANGUAGE_DESC]="Choose the interface language.",
    [L10N_ENGLISH]="English",[L10N_RUSSIAN]="Russian",[L10N_GERMAN]="German",
    [L10N_ONLY_ENGLISH]="The choice is saved automatically.",[L10N_APPEARANCE]="Appearance",
    [L10N_SYSTEM]="System",[L10N_ABOUT]="About",[L10N_EXP_MENU]="Exp menu",[L10N_SETTINGS]="Settings",
};
static const char *const russian_catalog[L10N_COUNT] = {
    [L10N_LANGUAGE]="Язык",[L10N_LANGUAGE_DESC]="Выберите язык интерфейса.",
    [L10N_ENGLISH]="Английский",[L10N_RUSSIAN]="Русский",[L10N_GERMAN]="Немецкий",
    [L10N_ONLY_ENGLISH]="Выбор сохраняется автоматически.",[L10N_APPEARANCE]="Оформление",
    [L10N_SYSTEM]="Система",[L10N_ABOUT]="О системе",[L10N_EXP_MENU]="Меню Exp",[L10N_SETTINGS]="Настройки",
};
static const char *const german_catalog[L10N_COUNT] = {
    [L10N_LANGUAGE]="Sprache",[L10N_LANGUAGE_DESC]="Sprache der Oberflache auswahlen.",
    [L10N_ENGLISH]="Englisch",[L10N_RUSSIAN]="Russisch",[L10N_GERMAN]="Deutsch",
    [L10N_ONLY_ENGLISH]="Die Auswahl wird automatisch gespeichert.",[L10N_APPEARANCE]="Darstellung",
    [L10N_SYSTEM]="System",[L10N_ABOUT]="Info",[L10N_EXP_MENU]="Exp Menu",[L10N_SETTINGS]="Einstellungen",
};
static const char *const lang_codes[L10N_LANG_MAX]={"en","ru","de"};
static const char *const lang_names[L10N_LANG_MAX]={"English","Русский","Deutsch"};

void l10n_init(void){
    const char *saved=sysconf_get("ui","language");current_language=L10N_LANG_EN;
    for(uint32_t i=0;i<L10N_LANG_MAX;i++)if(saved&&kstrcmp(saved,lang_codes[i])==0){current_language=(l10n_language_t)i;return;}
    sysconf_set("ui","language","en");
}
const char *l10n_get(l10n_key_t key){
    if((uint32_t)key>=(uint32_t)L10N_COUNT)return "";const char *const *cat=english_catalog;
    if(current_language==L10N_LANG_RU)cat=russian_catalog;else if(current_language==L10N_LANG_DE)cat=german_catalog;
    return cat[key]?cat[key]:english_catalog[key];
}
const char *l10n_current_code(void){return lang_codes[current_language];}
const char *l10n_current_name(void){return lang_names[current_language];}
uint32_t l10n_available_count(void){return L10N_LANG_MAX;}
const char *l10n_available_code(uint32_t index){return index<L10N_LANG_MAX?lang_codes[index]:"";}
const char *l10n_available_name(uint32_t index){return index<L10N_LANG_MAX?lang_names[index]:"";}
int l10n_set(const char *code){
    if(!code)return -1;for(uint32_t i=0;i<L10N_LANG_MAX;i++)if(kstrcmp(code,lang_codes[i])==0){current_language=(l10n_language_t)i;sysconf_set("ui","language",lang_codes[i]);sysconf_save();return 0;}return -1;
}
