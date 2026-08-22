#ifndef ATMKOALA_USERS_H
#define ATMKOALA_USERS_H

#include <stdint.h>

#define USER_MAX            15
#define USER_NAME_MAX       31
#define USER_HOME_MAX       95
#define USER_SHELL_MAX      31
#define USER_ROLE_MAX       15

#define USER_UID_ROOT       0u
#define USER_UID_FIRST      1000u
#define USER_UID_GUEST      2000u

typedef enum {
    ROLE_GUEST = 0,
    ROLE_USER,
    ROLE_OPERATOR,
    ROLE_ADMIN
} user_role_t;

typedef struct {
    char        name[USER_NAME_MAX + 1];
    uint32_t    uid;
    uint32_t    gid;
    user_role_t role;
    char        home[USER_HOME_MAX + 1];
    char        shell[USER_SHELL_MAX + 1];
    char        salt[9];
    char        passhash[65];
    int         active;
} user_account_t;

/* Initialise users.conf and make root the boot session. legacy_pin is migrated
 * only when no users.conf exists yet. */
int                 user_init(const char *legacy_pin);
int                 user_save(void);

/* Account operations. Return 0 on success, negative errno-style code on error. */
/* Pure syntax validation for installer/UI staging; it never touches storage. */
int                 user_name_is_valid(const char *name);
int                 user_add(const char *name, user_role_t role, const char *password);
int                 user_del(const char *name);
int                 user_set_password(const char *name, const char *password);
int                 user_set_role(const char *name, user_role_t role);
int                 user_auth(const char *name, const char *password);
int                 user_login(const char *name, const char *password);
void                user_logout(void);

/* Session / query helpers. */
const user_account_t *user_current(void);
const user_account_t *user_find(const char *name);
const user_account_t *user_at(int index);
int                 user_count(void);
int                 user_is_admin(void);
int                 user_can_sudo(void);
const char          *user_role_name(user_role_t role);
int                 user_parse_role(const char *name, user_role_t *out);

#endif
