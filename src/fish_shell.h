#ifndef FISH_SHELL_H
#define FISH_SHELL_H
/*
 * fish_shell.h — atmkoala v0.5
 *
 * Fish-style interactive shell features:
 *   ─ Syntax highlighting as you type:
 *       valid command  → green
 *       unknown command → red
 *       argument       → white
 *       string "..."   → yellow
 *       flag -x/--xx   → cyan
 *       path /foo/bar  → blue (if exists) / dark (if not)
 *       pipe |         → magenta
 *       redirect > >>  → magenta
 *   ─ Auto-suggestions (dim ghost text showing most likely completion)
 *   ─ Tab completion with menu
 *   ─ History search: Ctrl+R
 *   ─ Multi-line editing (\ at end continues)
 *   ─ Fish abbreviations: type 'l' → expands to 'ls -la'
 *   ─ Syntax error markers ($? after failed command)
 *   ─ Prompt shows: user@host path [git-branch] $
 *   ─ Right-prompt shows: last exit code + duration
 */
#include <stdint.h>

/* Token types for syntax highlighting */
typedef enum {
    TOK_NONE = 0,
    TOK_COMMAND,     /* first word — checked against known commands */
    TOK_VALID_CMD,   /* command that exists */
    TOK_INVALID_CMD, /* command not found */
    TOK_ARGUMENT,    /* plain argument */
    TOK_FLAG,        /* -x or --long */
    TOK_STRING,      /* "quoted" or 'single' */
    TOK_PATH_OK,     /* /path/that/exists */
    TOK_PATH_BAD,    /* /path/not/found */
    TOK_PIPE,        /* | */
    TOK_REDIRECT,    /* > >> < */
    TOK_VARIABLE,    /* $VAR */
    TOK_COMMENT,     /* # ... */
    TOK_NUMBER,      /* 42 0x1F */
    TOK_SEMICOLON,   /* ; */
    TOK_BACKGROUND,  /* & */
} token_type_t;

/* Parsed token */
typedef struct {
    int          start;   /* offset in input string */
    int          len;
    token_type_t type;
} sh_token_t;

/* Fish abbreviation */
typedef struct {
    char abbr[16];
    char expansion[64];
} sh_abbr_t;

/* ── API ──────────────────────────────────────────────────── */

/* Initialize shell (call once) */
void fish_init(void);

/* Main readline with fish features.
 * Replaces readline_v6/v9 — call this instead.
 * out receives the final line. */
void fish_readline(char *out, int maxlen);

/* Tokenize input line for highlighting */
int fish_tokenize(const char *line, sh_token_t *tokens, int max_tokens);

/* Get color for token type */
uint8_t fish_token_color(token_type_t t);

/* Abbreviation management */
void fish_abbr_add(const char *abbr, const char *expansion);
int  fish_abbr_expand(const char *word, char *out, int outsz);
void fish_abbr_list(void);

/* Check if command exists (for highlighting) */
int  fish_cmd_exists(const char *cmd);

/* Auto-suggestion: fill suggestion[] with most likely completion */
int  fish_suggest(const char *input, char *suggestion, int sugsz);

/* Ctrl+R history search */
void fish_history_search(char *out, int maxlen);

/* Declared in kernel.c, used by fish_shell.c */
extern void print_prompt(void);

#endif
