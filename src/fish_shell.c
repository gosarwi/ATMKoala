/* fish_shell.c — atmkoala v0.5 Fish-style shell */
#include "fish_shell.h"
#include "vga.h"
#include "vfs.h"
#include "keyboard.h"
#include "util.h"
#include "pit.h"
#include "ossdk.h"
#include <stdint.h>
#include <stddef.h>

/* ── Built-in command list for validation ─────────────────── */
static const char *BUILTIN_CMDS[] = {
    "ls","ll","la","cat","view","less","head","tail","grep","wc","sort","uniq",
    "cut","tr","tee","find","tree","stat","file","hd","hexdump",
    "cd","pwd","mkdir","rmdir","touch","rm","cp","mv","write","append","ln",
    "chmod","chown",
    "ps","kill","sleep","wait","sh","source",
    "uname","info","hwinfo","lscpu","uptime","mem","free","dmesg","date",
    "hostname","whoami","id","env","set","unset","export","printenv",
    "lsblk","df","du","mount","umount","mkfs","sync","live",
    "ifconfig","netstat","ping","arp","route",
    "sysconf",
    "sudo","su","login","logout","adduser","deluser","usermod","passwd","users","openrc","rc","rc-status","rc-service","service","rc-update",
    "pkg","readelf","exec",
    "sdk","serial","kmod","modules",
    "ai","de","gui","notepad","pong","snake","tetris",
    "echo","printf","clear","reset","logo","history","hist","man","help","posix","syscall",
    "reboot","halt","poweroff","aiy",
    "malloc","which","whereis","fish","abbr",
    NULL
};

/* ── Abbreviations (like fish 'abbr') ─────────────────────── */
#define MAX_ABBR 32
static sh_abbr_t abbrevs[MAX_ABBR];
static int abbr_count = 0;

/* ── History ──────────────────────────────────────────────── */
#define FISH_HIST_SIZE 512
#define FISH_HIST_LEN  256
static char fish_history[FISH_HIST_SIZE][FISH_HIST_LEN];
static int  fish_hist_count = 0;
static int  fish_hist_idx   = 0;

/* ── Last command result ──────────────────────────────────── */
static int last_exit_code = 0;
static uint32_t last_cmd_time = 0;   /* ticks */

/* ── Suggestions cache ────────────────────────────────────── */
static char last_suggestion[FISH_HIST_LEN] = {0};

/* ─────────────────────────────────────────────────────────── */

void fish_init(void) {
    abbr_count = 0; fish_hist_count = 0; fish_hist_idx = 0;
    last_exit_code = 0;

    /* Default abbreviations (like fish --write-abbr) */
    fish_abbr_add("l",   "ls -la");
    fish_abbr_add("ll",  "ls -l");
    fish_abbr_add("la",  "ls -a");
    fish_abbr_add("g",   "grep");
    fish_abbr_add("h",   "hist");
    fish_abbr_add("m",   "man");
    fish_abbr_add("cls", "clear");
    fish_abbr_add("q",   "exit");
    fish_abbr_add(".",   "source");
    fish_abbr_add("md",  "mkdir");
    fish_abbr_add("rf",  "rm -rf");
    fish_abbr_add("ka",  "kill");
    fish_abbr_add("hd",  "hexdump");
    fish_abbr_add("...", "cd ../..");
    fish_abbr_add("..",  "cd ..");
}

void fish_abbr_add(const char *abbr, const char *expansion) {
    if (abbr_count >= MAX_ABBR) return;
    kstrncpy(abbrevs[abbr_count].abbr, abbr, 15);
    kstrncpy(abbrevs[abbr_count].expansion, expansion, 63);
    abbr_count++;
}

int fish_abbr_expand(const char *word, char *out, int outsz) {
    for (int i = 0; i < abbr_count; i++) {
        if (kstrcmp(abbrevs[i].abbr, word) == 0) {
            kstrncpy(out, abbrevs[i].expansion, outsz-1);
            return 1;
        }
    }
    return 0;
}

void fish_abbr_list(void) {
    terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    terminal_writeln("  Abbreviations (abbr --list):");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    for (int i = 0; i < abbr_count; i++) {
        terminal_write("    "); terminal_write(abbrevs[i].abbr);
        terminal_write("  ->  "); terminal_writeln(abbrevs[i].expansion);
    }
}

int fish_cmd_exists(const char *cmd) {
    for (int i = 0; BUILTIN_CMDS[i]; i++)
        if (kstrcmp(BUILTIN_CMDS[i], cmd) == 0) return 1;
    /* Check SDK custom commands */
    extern sdk_cmd_t *sdk_cmd_find(const char *name);
    if (sdk_cmd_find(cmd)) return 1;
    /* Check VFS /syls/bin */
    char p[128]; kstrcpy(p, "/syls/bin/"); kstrcat(p, cmd);
    vfs_stat_t st; if (vfs_stat(p, &st) == 0) return 1;
    /* Check /bin */
    kstrcpy(p, "/bin/"); kstrcat(p, cmd);
    if (vfs_stat(p, &st) == 0) return 1;
    return 0;
}

/* ── Token colors ─────────────────────────────────────────── */
uint8_t fish_token_color(token_type_t t) {
    switch(t) {
        case TOK_VALID_CMD:   return VGA_LIGHT_GREEN;
        case TOK_INVALID_CMD: return VGA_LIGHT_RED;
        case TOK_ARGUMENT:    return VGA_LIGHT_GREY;
        case TOK_FLAG:        return VGA_LIGHT_CYAN;
        case TOK_STRING:      return VGA_YELLOW;
        case TOK_PATH_OK:     return VGA_LIGHT_BLUE;
        case TOK_PATH_BAD:    return VGA_DARK_GREY;
        case TOK_PIPE:        return VGA_LIGHT_MAGENTA;
        case TOK_REDIRECT:    return VGA_LIGHT_MAGENTA;
        case TOK_VARIABLE:    return VGA_LIGHT_CYAN;
        case TOK_COMMENT:     return VGA_DARK_GREY;
        case TOK_NUMBER:      return VGA_LIGHT_MAGENTA;
        case TOK_SEMICOLON:   return VGA_LIGHT_MAGENTA;
        default:              return VGA_LIGHT_GREY;
    }
}

/* ── Tokenizer ────────────────────────────────────────────── */
int fish_tokenize(const char *line, sh_token_t *tokens, int max_tokens) {
    int count = 0;
    int i = 0, n = (int)kstrlen(line);
    int cmd_done = 0;   /* after first token, we're in arg mode */
    int after_pipe = 0; /* after | we expect another command */

    while (i < n && count < max_tokens) {
        /* skip spaces */
        while (i < n && (line[i]==' '||line[i]=='\t')) i++;
        if (i >= n) break;

        sh_token_t *tok = &tokens[count];
        tok->start = i;
        tok->type  = TOK_ARGUMENT;

        /* Comment */
        if (line[i] == '#') {
            tok->len = n - i;
            tok->type = TOK_COMMENT;
            count++; break;
        }
        /* Pipe */
        if (line[i] == '|') {
            tok->len = 1; tok->type = TOK_PIPE;
            after_pipe = 1; cmd_done = 0;
            i++; count++; continue;
        }
        /* Semicolon */
        if (line[i] == ';') {
            tok->len = 1; tok->type = TOK_SEMICOLON;
            after_pipe = 0; cmd_done = 0;
            i++; count++; continue;
        }
        /* Background */
        if (line[i] == '&') {
            tok->len = 1; tok->type = TOK_BACKGROUND;
            i++; count++; continue;
        }
        /* Redirect */
        if (line[i]=='>'||line[i]=='<') {
            int start2=i;
            if (line[i]=='>'&&line[i+1]=='>') i+=2;
            else i++;
            tok->len = i-start2; tok->type = TOK_REDIRECT;
            count++; continue;
        }
        /* String */
        if (line[i]=='"'||line[i]=='\'') {
            char q = line[i]; int start2 = i++;
            while (i<n && line[i]!=q) {
                if (line[i]=='\\') i++;
                i++;
            }
            if (i<n) i++;
            tok->len = i-start2; tok->type = TOK_STRING;
            count++; continue;
        }
        /* Variable */
        if (line[i]=='$') {
            int start2=i++;
            while (i<n && (kis_alnum(line[i])||line[i]=='_')) i++;
            tok->len = i-start2; tok->type = TOK_VARIABLE;
            count++; continue;
        }

        /* Regular word */
        int start2 = i;
        while (i<n && line[i]!=' ' && line[i]!='\t' && line[i]!='|' &&
               line[i]!=';' && line[i]!='>' && line[i]!='<' && line[i]!='&')
            i++;
        tok->len = i - start2;

        char word[64]; int wl = tok->len < 63 ? tok->len : 63;
        kstrncpy(word, line+start2, (size_t)wl); word[wl]=0;

        if (!cmd_done || after_pipe) {
            /* This is a command token */
            cmd_done = 1; after_pipe = 0;
            tok->type = fish_cmd_exists(word) ? TOK_VALID_CMD : TOK_INVALID_CMD;
        } else if (word[0]=='-') {
            tok->type = TOK_FLAG;
        } else if (word[0]=='/') {
            /* Path — check if exists */
            vfs_stat_t st;
            tok->type = (vfs_stat(word,&st)==0) ? TOK_PATH_OK : TOK_PATH_BAD;
        } else {
            /* Check if it's a number */
            int is_num = 1;
            for (int j=0; word[j]; j++)
                if (!kis_digit(word[j])&&word[j]!='x'&&!kis_hex(word[j])) { is_num=0; break; }
            tok->type = is_num ? TOK_NUMBER : TOK_ARGUMENT;
        }
        count++;
    }
    return count;
}

/* ── Auto-suggestion from history ────────────────────────── */
int fish_suggest(const char *input, char *suggestion, int sugsz) {
    if (!input[0]) { suggestion[0]=0; return 0; }
    int il = (int)kstrlen(input);
    /* Search history backwards for matching entry */
    for (int i = fish_hist_count-1; i >= 0; i--) {
        if (kstrncmp(fish_history[i], input, (size_t)il)==0 &&
            kstrlen(fish_history[i]) > (size_t)il) {
            kstrncpy(suggestion, fish_history[i]+il, (size_t)(sugsz-1));
            suggestion[sugsz-1]=0;
            return 1;
        }
    }
    /* No history match — try command completion */
    for (int i = 0; BUILTIN_CMDS[i]; i++) {
        if (kstrncmp(BUILTIN_CMDS[i], input, (size_t)il)==0 &&
            (int)kstrlen(BUILTIN_CMDS[i]) > il) {
            kstrncpy(suggestion, BUILTIN_CMDS[i]+il, (size_t)(sugsz-1));
            return 1;
        }
    }
    suggestion[0]=0;
    return 0;
}

/* ── History search (Ctrl+R) ─────────────────────────────── */
void fish_history_search(char *out, int maxlen) {
    char query[FISH_HIST_LEN] = {0};
    int  qpos = 0;

    terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    terminal_write("\n[history search] Ctrl+C=cancel: ");
    int start_col, start_row;
    terminal_get_cursor(&start_row, &start_col);

    while (1) {
        /* Show query */
        terminal_set_cursor(start_row, start_col);
        terminal_erase_eol();
        terminal_set_color(VGA_WHITE, VGA_BLACK);
        terminal_write(query);

        /* Find match */
        char match[FISH_HIST_LEN] = {0};
        if (qpos > 0) {
            for (int i = fish_hist_count-1; i >= 0; i--) {
                if (kstrstr(fish_history[i], query)) {
                    kstrncpy(match, fish_history[i], FISH_HIST_LEN-1);
                    break;
                }
            }
        }
        if (match[0]) {
            terminal_set_color(VGA_DARK_GREY, VGA_BLACK);
            terminal_write("  -> ");
            terminal_write(match);
        }

        int k = keyboard_getkey();
        if (k == 3 || k == KEY_ESC) { out[0]=0; terminal_putchar('\n'); return; }
        if (k == '\n' || k == '\r') {
            if (match[0]) kstrncpy(out, match, maxlen-1);
            else kstrncpy(out, query, maxlen-1);
            terminal_putchar('\n');
            return;
        }
        if ((k=='\b'||k==127) && qpos>0) { qpos--; query[qpos]=0; continue; }
        if (k>=0x20&&k<=0x7E&&qpos<FISH_HIST_LEN-1) {
            query[qpos++]=(char)k; query[qpos]=0;
        }
    }
}

/* ── Redraw input with syntax highlighting ────────────────── */
#define RL_MAX 512

static void fish_redraw(const char *buf, int len, int pos, int old_len,
                         int prompt_col, int prompt_row) {
    int cur_row, cur_col;
    terminal_get_cursor(&cur_row, &cur_col);

    /* Go to start of input */
    if (cur_row == prompt_row) {
        while (cur_col > prompt_col) { terminal_putchar('\b'); cur_col--; }
    } else {
        terminal_erase_eol();
        terminal_set_cursor(prompt_row, prompt_col);
    }

    /* Erase old content */
    terminal_erase_eol();
    if (prompt_col + old_len >= VGA_WIDTH && prompt_row+1 < VGA_HEIGHT) {
        int sr2, sc2; terminal_get_cursor(&sr2, &sc2);
        terminal_set_cursor(prompt_row+1, 0);
        terminal_erase_eol();
        terminal_set_cursor(sr2, sc2);
    }

    /* Tokenize and highlight */
    sh_token_t tokens[64];
    int ntok = fish_tokenize(buf, tokens, 64);

    /* Print with colors */
    int printed = 0;
    for (int t = 0; t < ntok; t++) {
        sh_token_t *tok = &tokens[t];
        /* Print spaces before token */
        while (printed < tok->start) {
            terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            terminal_putchar(buf[printed++]);
        }
        /* Print token with its color */
        terminal_set_color(fish_token_color(tok->type), VGA_BLACK);
        for (int i = 0; i < tok->len && printed < len; i++)
            terminal_putchar(buf[printed++]);
    }
    /* Print any remaining chars */
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    while (printed < len) terminal_putchar(buf[printed++]);

    /* Print auto-suggestion as dim ghost text */
    if (len > 0 && pos == len) {
        char sug[128];
        if (fish_suggest(buf, sug, sizeof(sug)) && sug[0]) {
            terminal_set_color(VGA_DARK_GREY, VGA_BLACK);
            for (int i = 0; sug[i]; i++) terminal_putchar(sug[i]);
            /* Erase rest */
            int extra = old_len - len - (int)kstrlen(sug);
            for (int i = 0; i < extra; i++) terminal_putchar(' ');
            /* Move back */
            int back = (int)kstrlen(sug) + (extra>0?extra:0);
            for (int i = 0; i < back; i++) terminal_putchar('\b');
            kstrcpy(last_suggestion, sug);
        } else {
            last_suggestion[0]=0;
        }
    }

    /* Erase any leftover from old_len */
    int disp_len = len + (int)kstrlen(last_suggestion);
    if (old_len > disp_len) {
        int extra2 = old_len - disp_len;
        for (int i = 0; i < extra2; i++) terminal_putchar(' ');
        for (int i = 0; i < extra2; i++) terminal_putchar('\b');
    }

    /* Move cursor to pos */
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    for (int i = len; i > pos; i--) terminal_putchar('\b');
}

/* ── History management ───────────────────────────────────── */
static void hist_add(const char *line) {
    if (!line[0]) return;
    /* Don't add duplicates of last entry */
    if (fish_hist_count > 0 &&
        kstrcmp(fish_history[fish_hist_count-1], line)==0) return;
    if (fish_hist_count < FISH_HIST_SIZE) {
        kstrncpy(fish_history[fish_hist_count++], line, FISH_HIST_LEN-1);
    } else {
        /* Circular — drop oldest */
        for (int i = 0; i < FISH_HIST_SIZE-1; i++)
            kstrcpy(fish_history[i], fish_history[i+1]);
        kstrncpy(fish_history[FISH_HIST_SIZE-1], line, FISH_HIST_LEN-1);
    }
    fish_hist_idx = fish_hist_count;
}

/* ── Main readline ────────────────────────────────────────── */
void fish_readline(char *out, int maxlen) {
    char buf[RL_MAX]; kmemset(buf, 0, sizeof(buf));
    int len = 0, pos = 0;
    int hist_idx = fish_hist_count;
    int old_len  = 0;
    int prompt_row, prompt_col;

    terminal_get_cursor(&prompt_row, &prompt_col);
    fish_redraw(buf, 0, 0, 0, prompt_col, prompt_row);

    while (1) {
        int k = keyboard_getkey();

        /* PgUp/PgDn scrollback */
        if (k == KEY_PGUP) {
            terminal_scroll_up(5);
            /* redraw prompt */
            terminal_putchar('\n');
            extern void print_prompt(void);
            print_prompt();
            terminal_get_cursor(&prompt_row, &prompt_col);
            fish_redraw(buf, len, pos, old_len, prompt_col, prompt_row);
            continue;
        }
        if (k == KEY_PGDN) {
            terminal_scroll_down(5);
            terminal_putchar('\n');
            extern void print_prompt(void);
            print_prompt();
            terminal_get_cursor(&prompt_row, &prompt_col);
            fish_redraw(buf, len, pos, old_len, prompt_col, prompt_row);
            continue;
        }

        /* Enter */
        if (k == '\n' || k == '\r') {
            buf[len] = 0;
            /* Accept suggestion if cursor at end */
            if (pos == len && last_suggestion[0]) {
                int sl = (int)kstrlen(last_suggestion);
                if (len+sl < RL_MAX-1) {
                    kstrncat(buf, last_suggestion, (size_t)(RL_MAX-len-1));
                    len += sl;
                    last_suggestion[0] = 0;
                }
            }
            /* Expand abbreviation if single word */
            char expanded[256]; int is_abbr = 0;
            if (!kstrchr(buf, ' '))
                is_abbr = fish_abbr_expand(buf, expanded, sizeof(expanded));
            kstrncpy(out, is_abbr ? expanded : buf, maxlen-1);
            out[maxlen-1] = 0;
            terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            terminal_putchar('\n');
            hist_add(buf);
            return;
        }
        /* Ctrl+C */
        if (k == 3) {
            terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);
            terminal_write("^C");
            terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            terminal_putchar('\n');
            out[0] = 0; return;
        }
        /* Ctrl+L */
        if (k == 12) {
            terminal_clear();
            kstrncpy(out, "clear", maxlen-1); return;
        }
        /* Ctrl+R history search */
        if (k == 18) {
            fish_history_search(out, maxlen);
            if (out[0]) {
                /* Copy into buf and continue editing */
                kstrcpy(buf, out);
                len = pos = (int)kstrlen(buf);
                old_len = 0;
                terminal_get_cursor(&prompt_row, &prompt_col);
                fish_redraw(buf, len, pos, old_len, prompt_col, prompt_row);
                old_len = len;
            }
            continue;
        }
        /* Ctrl+A — beginning */
        if (k == 1) { pos = 0; fish_redraw(buf,len,pos,old_len,prompt_col,prompt_row); old_len=len; continue; }
        /* Ctrl+E — end */
        if (k == 5) { pos = len; fish_redraw(buf,len,pos,old_len,prompt_col,prompt_row); old_len=len; continue; }
        /* Ctrl+K — kill to end */
        if (k == 11) { old_len=len; len=pos; buf[len]=0; fish_redraw(buf,len,pos,old_len,prompt_col,prompt_row); old_len=len; continue; }
        /* Ctrl+U — kill to start */
        if (k == 21) {
            old_len=len; kmemmove(buf,buf+pos,(size_t)(len-pos)); len-=pos; pos=0; buf[len]=0;
            fish_redraw(buf,len,pos,old_len,prompt_col,prompt_row); old_len=len; continue;
        }
        /* Ctrl+W — kill word */
        if (k == 23) {
            if (pos > 0) {
                int e=pos;
                while(pos>0&&buf[pos-1]==' ') pos--;
                while(pos>0&&buf[pos-1]!=' ') pos--;
                old_len=len; kmemmove(buf+pos,buf+e,(size_t)(len-e));
                len-=(e-pos); buf[len]=0;
                fish_redraw(buf,len,pos,old_len,prompt_col,prompt_row); old_len=len;
            }
            continue;
        }
        /* Backspace */
        if (k == '\b' || k == 127) {
            if (pos > 0) {
                old_len=len; kmemmove(buf+pos-1,buf+pos,(size_t)(len-pos));
                pos--; len--; buf[len]=0;
                fish_redraw(buf,len,pos,old_len,prompt_col,prompt_row); old_len=len;
            }
            continue;
        }
        /* Delete */
        if (k == KEY_DEL) {
            if (pos < len) {
                old_len=len; kmemmove(buf+pos,buf+pos+1,(size_t)(len-pos-1));
                len--; buf[len]=0;
                fish_redraw(buf,len,pos,old_len,prompt_col,prompt_row); old_len=len;
            }
            continue;
        }
        /* Arrow keys */
        if (k == KEY_LEFT)  { if(pos>0){pos--;} fish_redraw(buf,len,pos,old_len,prompt_col,prompt_row); old_len=len; continue; }
        if (k == KEY_RIGHT) {
            /* Accept suggestion word by word */
            if (pos < len) { pos++; }
            else if (last_suggestion[0]) {
                /* Accept one word of suggestion */
                const char *s = last_suggestion;
                while (*s && *s == ' ' && len < RL_MAX-1) { buf[len++]=(char)*s++; pos++; }
                while (*s && *s != ' ' && len < RL_MAX-1) { buf[len++]=(char)*s++; pos++; }
                buf[len]=0;
                kstrncpy(last_suggestion, s, sizeof(last_suggestion)-1);
            }
            fish_redraw(buf,len,pos,old_len,prompt_col,prompt_row); old_len=len; continue;
        }
        if (k == KEY_HOME) { pos=0; fish_redraw(buf,len,pos,old_len,prompt_col,prompt_row); old_len=len; continue; }
        if (k == KEY_END)  { pos=len; fish_redraw(buf,len,pos,old_len,prompt_col,prompt_row); old_len=len; continue; }
        /* History */
        if (k == KEY_UP) {
            if (hist_idx > 0) {
                hist_idx--;
                old_len=len; kstrcpy(buf,fish_history[hist_idx]);
                len=pos=(int)kstrlen(buf);
                fish_redraw(buf,len,pos,old_len,prompt_col,prompt_row); old_len=len;
            }
            continue;
        }
        if (k == KEY_DOWN) {
            old_len=len;
            if (hist_idx < fish_hist_count) hist_idx++;
            if (hist_idx == fish_hist_count) { buf[0]=0; len=pos=0; }
            else { kstrcpy(buf,fish_history[hist_idx]); len=pos=(int)kstrlen(buf); }
            fish_redraw(buf,len,pos,old_len,prompt_col,prompt_row); old_len=len;
            continue;
        }
        /* Tab completion */
        if (k == '\t') {
            /* Find current word */
            int ws=pos; while(ws>0&&buf[ws-1]!=' ')ws--;
            char prefix[64]; int pl=pos-ws;
            kstrncpy(prefix,buf+ws,(size_t)pl); prefix[pl]=0;
            /* Try commands first */
            char best[64]={0}; int matches=0;
            if (ws == 0) {
                for (int i=0;BUILTIN_CMDS[i];i++) {
                    if (kstrncmp(BUILTIN_CMDS[i],prefix,(size_t)pl)==0) {
                        if (!matches) kstrcpy(best,BUILTIN_CMDS[i]);
                        matches++;
                    }
                }
            }
            if (!matches) {
                /* Try files */
                char names[32][VFS_NAME_MAX + 1]; int cnt=0;
                vfs_listdir("./",&names[0],&cnt);
                for (int i=0;i<cnt;i++) {
                    char nm[VFS_NAME_MAX + 1]; kstrcpy(nm,names[i]);
                    int nl=(int)kstrlen(nm); if(nl&&nm[nl-1]=='/')nm[nl-1]=0;
                    if (kstrncmp(nm,prefix,(size_t)pl)==0) {
                        if (!matches) kstrcpy(best,names[i]);
                        matches++;
                    }
                }
            }
            if (matches == 1) {
                int bl=(int)kstrlen(best), add=bl-pl;
                if (len+add < RL_MAX-1) {
                    old_len=len;
                    kmemmove(buf+pos+add,buf+pos,(size_t)(len-pos));
                    kmemcpy(buf+pos,best+pl,(size_t)add);
                    if (len+add < RL_MAX-1 && buf[len+add-1]!='/') {
                        buf[len+add]=' '; add++; /* add space after */
                    }
                    pos+=add; len+=add; buf[len]=0;
                    fish_redraw(buf,len,pos,old_len,prompt_col,prompt_row); old_len=len;
                }
            } else if (matches > 1) {
                terminal_putchar('\n');
                terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
                /* Print matches */
                for (int i=0;BUILTIN_CMDS[i];i++) {
                    if (kstrncmp(BUILTIN_CMDS[i],prefix,(size_t)pl)==0) {
                        terminal_write(BUILTIN_CMDS[i]); terminal_write("  ");
                    }
                }
                terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
                terminal_putchar('\n');
                extern void print_prompt(void);
                print_prompt();
                terminal_get_cursor(&prompt_row, &prompt_col);
                old_len=0;
                fish_redraw(buf,len,pos,0,prompt_col,prompt_row);
                old_len=len;
            }
            continue;
        }
        /* UTF-8 Russian */
        extern char keyboard_utf8_buf[4];
        if (k == 0x200) {
            int ul=(int)kstrlen(keyboard_utf8_buf);
            if (len+ul < RL_MAX-1) {
                old_len=len;
                kmemmove(buf+pos+ul,buf+pos,(size_t)(len-pos));
                kmemcpy(buf+pos,keyboard_utf8_buf,(size_t)ul);
                pos+=ul; len+=ul; buf[len]=0;
                fish_redraw(buf,len,pos,old_len,prompt_col,prompt_row); old_len=len;
            }
            continue;
        }
        /* Normal printable character */
        if (k >= 0x20 && k <= 0x7E && len < RL_MAX-1 && len < maxlen-1) {
            old_len=len;
            kmemmove(buf+pos+1,buf+pos,(size_t)(len-pos));
            buf[pos]=(char)k; pos++; len++; buf[len]=0;
            fish_redraw(buf,len,pos,old_len,prompt_col,prompt_row); old_len=len;
        }
    }
}
