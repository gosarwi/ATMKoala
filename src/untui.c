/*  untui.c — UserNet Manager TUI for atmkoala
 *
 *  Full-screen VGA text-mode network management interface.
 *  Navigation: arrow keys, Enter, Esc, Tab.
 *  No external dependencies beyond unm, net, vga, keyboard, pit.
 */

#include "untui.h"
#include "unm.h"
#include "net.h"
#include "vga.h"
#include "keyboard.h"
#include "pit.h"
#include "util.h"
#include <stdint.h>
#include <stddef.h>

/* ── Layout constants ───────────────────────────────────────── */
#define TUI_W   80
#define TUI_H   25

#define TUI_TITLE_ROW    0
#define TUI_STATUS_ROW   1
#define TUI_DIVIDER_ROW  2
#define TUI_CONTENT_TOP  3
#define TUI_CONTENT_BOT  22
#define TUI_HINT_ROW     23
#define TUI_FOOTER_ROW   24

/* ── Color scheme ───────────────────────────────────────────── */
#define C_TITLE_FG   VGA_BLACK
#define C_TITLE_BG   VGA_LIGHT_CYAN
#define C_STATUS_FG  VGA_WHITE
#define C_STATUS_BG  VGA_BLUE
#define C_NORMAL_FG  VGA_LIGHT_GREY
#define C_NORMAL_BG  VGA_BLACK
#define C_SELECT_FG  VGA_BLACK
#define C_SELECT_BG  VGA_LIGHT_CYAN
#define C_HINT_FG    VGA_DARK_GREY
#define C_HINT_BG    VGA_BLACK
#define C_OK_FG      VGA_LIGHT_GREEN
#define C_ERR_FG     VGA_LIGHT_RED
#define C_LABEL_FG   VGA_YELLOW
#define C_FRAME_FG   VGA_CYAN
#define C_INPUT_FG   VGA_WHITE
#define C_INPUT_BG   VGA_DARK_GREY

/* ── IP parse helper (local copy of unm.c static) ──────────── */
static void unm_ip_parse_pub(const char *s, uint8_t out[4]) {
    int o = 0; uint32_t v = 0;
    for (; *s && o < 4; s++) {
        if (*s >= '0' && *s <= '9') v = v * 10 + (uint32_t)(*s - '0');
        else if (*s == '.') { out[o++] = (uint8_t)(v & 0xFF); v = 0; }
    }
    if (o < 4) out[o] = (uint8_t)(v & 0xFF);
}

/* ── Low-level drawing ──────────────────────────────────────── */
static void tui_setcol(uint8_t fg, uint8_t bg) {
    terminal_set_color(fg, bg);
}

static void tui_goto(int row, int col) {
    terminal_set_cursor(row, col);
}

static void tui_putch(char c) {
    terminal_putchar(c);
}

static void tui_write(const char *s) {
    terminal_write(s);
}

static void tui_fill_row(int row, char c, uint8_t fg, uint8_t bg) {
    tui_setcol(fg, bg);
    tui_goto(row, 0);
    for (int i = 0; i < TUI_W; i++) tui_putch(c);
}

/* Write a string centered on a row */
static void tui_write_centered(int row, const char *s, uint8_t fg, uint8_t bg) {
    int len = (int)kstrlen(s);
    int col = (TUI_W - len) / 2;
    if (col < 0) col = 0;
    tui_setcol(fg, bg);
    tui_goto(row, 0);
    for (int i = 0; i < col; i++) tui_putch(' ');
    tui_write(s);
    for (int i = col + len; i < TUI_W; i++) tui_putch(' ');
}

/* Write a padded line at (row, col) with fixed width */
static void tui_write_field(int row, int col, const char *s, int width,
                              uint8_t fg, uint8_t bg) {
    tui_setcol(fg, bg);
    tui_goto(row, col);
    int len = (int)kstrlen(s);
    for (int i = 0; i < width; i++) {
        tui_putch((i < len) ? s[i] : ' ');
    }
}

/* Clear content area */
static void tui_clear_content(void) {
    tui_setcol(C_NORMAL_FG, C_NORMAL_BG);
    for (int r = TUI_CONTENT_TOP; r <= TUI_CONTENT_BOT; r++) {
        tui_goto(r, 0);
        for (int c = 0; c < TUI_W; c++) tui_putch(' ');
    }
}

/* Draw chrome: title bar, divider, hint bar, footer */
static void tui_draw_chrome(const char *screen_name) {
    /* Title bar */
    tui_fill_row(TUI_TITLE_ROW, ' ', C_TITLE_FG, C_TITLE_BG);
    {
        char buf[80];
        ksnprintf(buf, sizeof(buf), " atmkoala  UserNet Manager  |  %s", screen_name);
        tui_goto(TUI_TITLE_ROW, 0);
        tui_setcol(C_TITLE_FG, C_TITLE_BG);
        tui_write(buf);
    }
    /* Version right-align */
    tui_goto(TUI_TITLE_ROW, TUI_W - 10);
    tui_write("  v1.0    ");

    /* Status bar */
    tui_fill_row(TUI_STATUS_ROW, ' ', C_STATUS_FG, C_STATUS_BG);
    tui_goto(TUI_STATUS_ROW, 1);
    tui_setcol(C_STATUS_FG, C_STATUS_BG);

    const char *states[] = { "DOWN", "CONNECTING...", "UP", "FAILED" };
    tui_write("eth0: ");
    tui_write(states[g_unm.state]);
    tui_write("  |  ");

    if (net.initialized) {
        tui_write("MAC: ");
        tui_write(net_mac_str());
        tui_write("  |  ");
        if (g_unm.state == UNM_STATE_UP) {
            char ipbuf[24];
            unm_ip_str(g_unm.leased_ip, ipbuf, sizeof(ipbuf));
            tui_write("IP: ");
            tui_write(ipbuf);
        } else {
            tui_write("IP: ---");
        }
    } else {
        tui_write("No NIC detected — start QEMU with -device rtl8139");
    }

    /* Divider */
    tui_setcol(C_FRAME_FG, C_NORMAL_BG);
    tui_goto(TUI_DIVIDER_ROW, 0);
    for (int i = 0; i < TUI_W; i++) tui_putch('-');

    /* Hint bar */
    tui_fill_row(TUI_HINT_ROW, ' ', C_HINT_FG, C_HINT_BG);
    tui_goto(TUI_HINT_ROW, 0);
    tui_setcol(C_HINT_FG, C_HINT_BG);
    tui_write(" Arrows/Enter=select  Esc=back  Tab=next  Q=quit");

    /* Footer */
    tui_fill_row(TUI_FOOTER_ROW, ' ', C_HINT_FG, VGA_BLUE);
    tui_goto(TUI_FOOTER_ROW, 0);
    tui_setcol(VGA_WHITE, VGA_BLUE);
    tui_write(" atmkoala Network Manager  |  UNM v1.0  |  DHCP / Static IP support");
}

/* ── Inline readline for TUI (echoes, single line) ─────────── */
static void tui_readline(int row, int col, int width, char *out, int maxlen) {
    int len = 0, pos = 0;
    out[0] = 0;

    while (1) {
        /* Render */
        tui_setcol(C_INPUT_FG, C_INPUT_BG);
        tui_goto(row, col);
        for (int i = 0; i < width; i++) {
            tui_putch((i < len) ? out[i] : ' ');
        }
        tui_goto(row, col + pos);

        int k = keyboard_getkey();
        if (k == '\n' || k == '\r') break;
        if (k == KEY_ESC) { out[0] = 0; break; }
        if ((k == '\b' || k == 127) && pos > 0) {
            for (int i = pos - 1; i < len - 1; i++) out[i] = out[i+1];
            pos--; len--; out[len] = 0;
        } else if (k == KEY_LEFT  && pos > 0)   pos--;
        else if (k == KEY_RIGHT && pos < len)  pos++;
        else if (k >= ' ' && k <= '~' && len < maxlen - 1) {
            for (int i = len; i > pos; i--) out[i] = out[i-1];
            out[pos++] = (char)k;
            len++; out[len] = 0;
        }
    }
}

/* ── Confirm dialog ─────────────────────────────────────────── */
static int tui_confirm(int row, const char *msg) {
    tui_setcol(VGA_YELLOW, VGA_BLACK);
    tui_goto(row, 2);
    tui_write(msg);
    tui_write(" [Y/n]: ");
    int k = keyboard_getkey();
    return (k == 'y' || k == 'Y' || k == '\n' || k == '\r');
}

/* ── Progress animation ─────────────────────────────────────── */
static void tui_progress(int row, const char *label, int steps) {
    tui_setcol(C_OK_FG, C_NORMAL_BG);
    tui_goto(row, 2);
    tui_write(label);
    tui_write(" [");
    for (int i = 0; i < steps; i++) {
        tui_putch('=');
        pit_sleep(8);
    }
    tui_putch(']');
}

/* ─────────────────────────────────────────────────────────────
 *  SCREEN: Main Menu
 * ───────────────────────────────────────────────────────────── */
typedef enum {
    SCREEN_MAIN = 0,
    SCREEN_CONNECT,
    SCREEN_PROFILES,
    SCREEN_STATIC,
    SCREEN_DNS,
    SCREEN_STATS,
    SCREEN_QUIT,
} tui_screen_t;

static tui_screen_t tui_screen_main(void) {
    static const char *items[] = {
        "  [ Connect / Reconnect ]     DHCP or saved profile",
        "  [ Disconnect ]              Bring interface down",
        "  [ Profiles ]                Manage network profiles",
        "  [ Configure Static IP ]     Manual IP / Gateway / DNS",
        "  [ DNS Lookup ]              Resolve hostname",
        "  [ Statistics ]              RX/TX packet counters",
        "  [ Save Profile ]            Persist current settings",
        "  [ Quit ]                    Return to shell",
        NULL
    };
    tui_screen_t actions[] = {
        SCREEN_CONNECT, SCREEN_MAIN /* disconnect inline */,
        SCREEN_PROFILES, SCREEN_STATIC, SCREEN_DNS, SCREEN_STATS,
        SCREEN_MAIN /* save inline */, SCREEN_QUIT
    };
    int n = 0; while (items[n]) n++;
    int sel = 0;

    while (1) {
        terminal_clear();
        tui_draw_chrome("Main Menu");
        tui_clear_content();

        /* Banner */
        tui_write_centered(TUI_CONTENT_TOP + 1,
            "=== atmkoala UserNet Manager ===",
            VGA_LIGHT_CYAN, C_NORMAL_BG);

        /* Menu items */
        for (int i = 0; i < n; i++) {
            int row = TUI_CONTENT_TOP + 3 + i;
            if (i == sel)
                tui_write_field(row, 1, items[i], TUI_W-2, C_SELECT_FG, C_SELECT_BG);
            else
                tui_write_field(row, 1, items[i], TUI_W-2, C_NORMAL_FG, C_NORMAL_BG);
        }

        int k = keyboard_getkey();
        if (k == KEY_UP)   { if (sel > 0) sel--; }
        if (k == KEY_DOWN) { if (sel < n-1) sel++; }
        if (k == 'q' || k == 'Q' || k == KEY_ESC) return SCREEN_QUIT;

        if (k == '\n' || k == '\r') {
            /* Special inline actions */
            if (sel == 1) { /* Disconnect */
                unm_disconnect();
                tui_write_centered(TUI_CONTENT_TOP + 13,
                    "Disconnected.", VGA_YELLOW, C_NORMAL_BG);
                pit_sleep(100);
                continue;
            }
            if (sel == 6) { /* Save */
                unm_save_profile();
                tui_write_centered(TUI_CONTENT_TOP + 13,
                    "Profile saved to /uiu/etc/network.conf",
                    VGA_LIGHT_GREEN, C_NORMAL_BG);
                pit_sleep(120);
                continue;
            }
            return actions[sel];
        }
    }
}

/* ─────────────────────────────────────────────────────────────
 *  SCREEN: Connect
 * ───────────────────────────────────────────────────────────── */
static tui_screen_t tui_screen_connect(void) {
    terminal_clear();
    tui_draw_chrome("Connect");
    tui_clear_content();

    /* Choose mode */
    static const char *modes[] = {
        "  DHCP  (automatic — recommended for QEMU)",
        "  Use saved profile",
        "  Cancel",
        NULL
    };
    int n = 0; while (modes[n]) n++;
    int sel = 0;

    while (1) {
        tui_write_centered(TUI_CONTENT_TOP + 1,
            "Select connection mode:", C_LABEL_FG, C_NORMAL_BG);
        for (int i = 0; i < n; i++) {
            int row = TUI_CONTENT_TOP + 3 + i;
            if (i == sel)
                tui_write_field(row, 4, modes[i], TUI_W-8, C_SELECT_FG, C_SELECT_BG);
            else
                tui_write_field(row, 4, modes[i], TUI_W-8, C_NORMAL_FG, C_NORMAL_BG);
        }
        int k = keyboard_getkey();
        if (k == KEY_UP)   { if (sel > 0) sel--; }
        if (k == KEY_DOWN) { if (sel < n-1) sel++; }
        if (k == KEY_ESC)  return SCREEN_MAIN;
        if (k == '\n' || k == '\r') {
            if (sel == 2) return SCREEN_MAIN;
            break;
        }
    }

    /* Run connection */
    tui_clear_content();
    tui_write_centered(TUI_CONTENT_TOP + 1,
        "Connecting...", VGA_YELLOW, C_NORMAL_BG);

    int row = TUI_CONTENT_TOP + 3;

    tui_setcol(C_NORMAL_FG, C_NORMAL_BG);
    tui_goto(row, 2); tui_write("Interface: "); tui_write(net.initialized ? "eth0 (RTL8139)" : "NOT FOUND"); row++;

    if (!net.initialized) {
        tui_goto(row+1, 2);
        tui_setcol(C_ERR_FG, C_NORMAL_BG);
        tui_write("ERROR: No NIC detected. Start QEMU with:");
        tui_goto(row+2, 2);
        tui_write("  -netdev user,id=net0 -device rtl8139,netdev=net0");
        tui_goto(row+4, 2);
        tui_setcol(C_HINT_FG, C_NORMAL_BG);
        tui_write("Press any key...");
        keyboard_getkey();
        return SCREEN_MAIN;
    }

    tui_progress(row, "Sending DHCP Discover", 10); row++;

    /* Use saved profile or force DHCP */
    int result;
    if (sel == 1 && g_unm.active_profile >= 0) {
        result = unm_connect();
    } else {
        /* Force DHCP regardless of profile */
        unm_mode_t saved_mode = UNM_MODE_DHCP;
        int old_idx = g_unm.active_profile;
        if (old_idx >= 0 && old_idx < g_unm.profile_count)
            saved_mode = g_unm.profiles[old_idx].mode;
        if (old_idx >= 0 && old_idx < g_unm.profile_count)
            g_unm.profiles[old_idx].mode = UNM_MODE_DHCP;
        result = unm_dhcp_request();
        if (old_idx >= 0 && old_idx < g_unm.profile_count)
            g_unm.profiles[old_idx].mode = saved_mode;
    }

    tui_goto(row, 2);
    if (result == 0) {
        tui_setcol(C_OK_FG, C_NORMAL_BG);
        tui_write("Connected successfully!");
        row += 2;
        tui_setcol(C_LABEL_FG, C_NORMAL_BG);
        char buf[32];
        tui_goto(row,   2); unm_ip_str(g_unm.leased_ip,  buf, sizeof(buf));
                            tui_write("IP      : "); tui_write(buf); row++;
        tui_goto(row,   2); unm_ip_str(g_unm.leased_gw,  buf, sizeof(buf));
                            tui_write("Gateway : "); tui_write(buf); row++;
        tui_goto(row,   2); unm_ip_str(g_unm.leased_dns, buf, sizeof(buf));
                            tui_write("DNS     : "); tui_write(buf); row++;
    } else {
        tui_setcol(C_ERR_FG, C_NORMAL_BG);
        tui_write("Connection FAILED.");
        row++;
        tui_goto(row, 2);
        tui_setcol(C_HINT_FG, C_NORMAL_BG);
        tui_write("Is QEMU running with -device rtl8139,netdev=net0 ?");
    }

    row += 2;
    tui_goto(row, 2);
    tui_setcol(C_HINT_FG, C_NORMAL_BG);
    tui_write("Press any key to return...");
    keyboard_getkey();
    return SCREEN_MAIN;
}

/* ─────────────────────────────────────────────────────────────
 *  SCREEN: Profiles
 * ───────────────────────────────────────────────────────────── */
static tui_screen_t tui_screen_profiles(void) {
    while (1) {
        terminal_clear();
        tui_draw_chrome("Profiles");
        tui_clear_content();

        tui_write_centered(TUI_CONTENT_TOP + 1,
            "Network Profiles  (Enter=activate  N=new  D=delete  Esc=back)",
            C_LABEL_FG, C_NORMAL_BG);

        /* Header row */
        tui_setcol(VGA_CYAN, C_NORMAL_BG);
        tui_goto(TUI_CONTENT_TOP + 2, 2);
        tui_write("  #  Active  Name                Mode      IP");

        tui_setcol(C_FRAME_FG, C_NORMAL_BG);
        tui_goto(TUI_CONTENT_TOP + 3, 2);
        for (int i = 0; i < TUI_W-4; i++) tui_putch('-');

        if (g_unm.profile_count == 0) {
            tui_write_centered(TUI_CONTENT_TOP + 5,
                "No profiles. Press N to create one.",
                C_HINT_FG, C_NORMAL_BG);
        } else {
            for (int i = 0; i < g_unm.profile_count; i++) {
                unm_profile_t *p = &g_unm.profiles[i];
                char buf[80];
                char ipbuf[24] = "auto";
                if (p->mode == UNM_MODE_STATIC)
                    unm_ip_str(p->ip, ipbuf, sizeof(ipbuf));
                ksnprintf(buf, sizeof(buf), "  %d   %-5s   %-18s  %-7s   %s",
                    i,
                    (i == g_unm.active_profile) ? "[*]" : "[ ]",
                    p->name,
                    (p->mode == UNM_MODE_DHCP) ? "DHCP" : "Static",
                    ipbuf);

                int row = TUI_CONTENT_TOP + 4 + i;
                uint8_t fg = (i == g_unm.active_profile) ? VGA_LIGHT_GREEN : C_NORMAL_FG;
                tui_write_field(row, 2, buf, TUI_W-4, fg, C_NORMAL_BG);
            }
        }

        int k = keyboard_getkey();
        if (k == KEY_ESC || k == 'q' || k == 'Q') return SCREEN_MAIN;

        if (k == 'n' || k == 'N') {
            /* New profile wizard — just add a DHCP one with prompted name */
            tui_setcol(C_LABEL_FG, C_NORMAL_BG);
            tui_goto(TUI_CONTENT_TOP + 14, 2);
            tui_write("New profile name: ");
            char pname[UNM_PROFILE_NAME_LEN];
            tui_readline(TUI_CONTENT_TOP + 14, 20, 24, pname, sizeof(pname));
            if (pname[0]) {
                uint8_t z[4]={0}, nm[4]={255,255,255,0};
                int idx = unm_profile_add(pname, UNM_MODE_DHCP, z, nm, z, z);
                if (idx >= 0 && g_unm.active_profile < 0)
                    unm_profile_set_active(idx);
            }
            continue;
        }

        if ((k == 'd' || k == 'D') && g_unm.profile_count > 0) {
            tui_setcol(VGA_YELLOW, C_NORMAL_BG);
            tui_goto(TUI_CONTENT_TOP + 15, 2);
            tui_write("Delete profile #? (enter number): ");
            char nbuf[4];
            tui_readline(TUI_CONTENT_TOP + 15, 36, 4, nbuf, sizeof(nbuf));
            int idx = kstrtoi(nbuf);
            if (idx >= 0 && idx < g_unm.profile_count) {
                /* Shift profiles down */
                for (int i = idx; i < g_unm.profile_count - 1; i++)
                    g_unm.profiles[i] = g_unm.profiles[i+1];
                g_unm.profile_count--;
                if (g_unm.active_profile == idx)
                    g_unm.active_profile = (g_unm.profile_count > 0) ? 0 : -1;
                else if (g_unm.active_profile > idx)
                    g_unm.active_profile--;
            }
            continue;
        }

        if ((k == '\n' || k == '\r') && g_unm.profile_count > 0) {
            /* Activate profile 0 by default (simple selection) */
            tui_setcol(VGA_YELLOW, C_NORMAL_BG);
            tui_goto(TUI_CONTENT_TOP + 15, 2);
            tui_write("Activate profile #? (enter number): ");
            char nbuf[4];
            tui_readline(TUI_CONTENT_TOP + 15, 37, 4, nbuf, sizeof(nbuf));
            int idx = kstrtoi(nbuf);
            unm_profile_set_active(idx);
            continue;
        }
    }
}

/* ─────────────────────────────────────────────────────────────
 *  SCREEN: Static IP setup
 * ───────────────────────────────────────────────────────────── */
static tui_screen_t tui_screen_static(void) {
    terminal_clear();
    tui_draw_chrome("Configure Static IP");
    tui_clear_content();

    tui_write_centered(TUI_CONTENT_TOP + 1,
        "Enter static IP configuration  (leave blank = keep current)",
        C_LABEL_FG, C_NORMAL_BG);

    struct { const char *label; char buf[32]; } fields[] = {
        { "IP Address   ", "10.0.2.15"   },
        { "Netmask      ", "255.255.255.0" },
        { "Gateway      ", "10.0.2.2"    },
        { "DNS Server   ", "10.0.2.3"    },
        { "Profile name ", "static-1"    },
        { NULL, "" }
    };
    int nf = 0; while (fields[nf].label) nf++;

    /* Pre-fill with current leased values if UP */
    if (g_unm.state == UNM_STATE_UP) {
        unm_ip_str(g_unm.leased_ip,  fields[0].buf, sizeof(fields[0].buf));
        unm_ip_str(g_unm.leased_gw,  fields[2].buf, sizeof(fields[2].buf));
        unm_ip_str(g_unm.leased_dns, fields[3].buf, sizeof(fields[3].buf));
    }

    int sel = 0;
    while (1) {
        for (int i = 0; i < nf; i++) {
            int row = TUI_CONTENT_TOP + 3 + i * 2;
            tui_setcol(C_LABEL_FG, C_NORMAL_BG);
            tui_goto(row, 4);
            tui_write(fields[i].label);
            tui_write(": ");
            int col = (int)kstrlen(fields[i].label) + 6;
            tui_write_field(row, col, fields[i].buf, 22,
                (i == sel) ? C_INPUT_FG : C_NORMAL_FG,
                (i == sel) ? C_INPUT_BG : C_NORMAL_BG);
        }

        int hint_row = TUI_CONTENT_TOP + 3 + nf * 2 + 1;
        tui_goto(hint_row, 4);
        tui_setcol(C_HINT_FG, C_NORMAL_BG);
        tui_write("Tab/Down=next field  Enter=confirm  Esc=cancel");

        int k = keyboard_getkey();
        if (k == KEY_ESC) return SCREEN_MAIN;

        if (k == KEY_UP)   { if (sel > 0) sel--; continue; }
        if (k == KEY_DOWN || k == KEY_TAB) { if (sel < nf-1) sel++; continue; }

        if (k == '\n' || k == '\r') {
            if (sel < nf - 1) {
                /* Edit current field then move to next */
                int row = TUI_CONTENT_TOP + 3 + sel * 2;
                int col = (int)kstrlen(fields[sel].label) + 6;
                tui_readline(row, col, 22, fields[sel].buf, sizeof(fields[sel].buf));
                sel++;
                continue;
            } else {
                /* Last field — confirm */
                int row = TUI_CONTENT_TOP + 3 + sel * 2;
                int col = (int)kstrlen(fields[sel].label) + 6;
                tui_readline(row, col, 22, fields[sel].buf, sizeof(fields[sel].buf));
                break;
            }
        }

        /* Direct typing in selected field */
        if (k >= ' ' && k <= '~') {
            int row = TUI_CONTENT_TOP + 3 + sel * 2;
            int col = (int)kstrlen(fields[sel].label) + 6;
            /* Put char back as first char of readline */
            char tmp[32] = {0};
            tmp[0] = (char)k;
            /* Actually run readline with pre-seed — simplest: clear+retype */
            fields[sel].buf[0] = (char)k; fields[sel].buf[1] = 0;
            tui_readline(row, col + 1, 21, fields[sel].buf + 1, sizeof(fields[sel].buf) - 1);
            /* Merge: fields[sel].buf[0] already set, rest written after */
            /* Combine */
            int l1 = 1, l2 = (int)kstrlen(fields[sel].buf + 1);
            (void)l1; (void)l2; /* buf is already in place */
        }
    }

    /* Apply */
    uint8_t ip[4]={0}, nm[4]={255,255,255,0}, gw[4]={0}, dns[4]={0};
    unm_ip_parse_pub(fields[0].buf, ip);
    unm_ip_parse_pub(fields[1].buf, nm);
    unm_ip_parse_pub(fields[2].buf, gw);
    unm_ip_parse_pub(fields[3].buf, dns);

    tui_clear_content();
    tui_write_centered(TUI_CONTENT_TOP + 1, "Applying static IP...", VGA_YELLOW, C_NORMAL_BG);

    unm_set_static(ip, nm, gw, dns);

    /* Add as profile */
    int idx = unm_profile_add(fields[4].buf, UNM_MODE_STATIC, ip, nm, gw, dns);
    if (idx >= 0) unm_profile_set_active(idx);

    int row = TUI_CONTENT_TOP + 3;
    char buf[32];
    tui_setcol(C_OK_FG, C_NORMAL_BG);
    tui_goto(row, 2); unm_ip_str(ip,  buf, sizeof(buf)); tui_write("IP      : "); tui_write(buf); row++;
    tui_goto(row, 2); unm_ip_str(gw,  buf, sizeof(buf)); tui_write("Gateway : "); tui_write(buf); row++;
    tui_goto(row, 2); unm_ip_str(dns, buf, sizeof(buf)); tui_write("DNS     : "); tui_write(buf); row += 2;
    tui_setcol(C_OK_FG, C_NORMAL_BG);
    tui_goto(row, 2); tui_write("Static IP configured.");
    row += 2;
    tui_setcol(C_HINT_FG, C_NORMAL_BG);
    tui_goto(row, 2); tui_write("Press any key...");
    keyboard_getkey();
    return SCREEN_MAIN;
}

/* ─────────────────────────────────────────────────────────────
 *  SCREEN: DNS Lookup
 * ───────────────────────────────────────────────────────────── */
static tui_screen_t tui_screen_dns(void) {
    while (1) {
        terminal_clear();
        tui_draw_chrome("DNS Lookup");
        tui_clear_content();

        tui_write_centered(TUI_CONTENT_TOP + 1,
            "Resolve hostname to IP address", C_LABEL_FG, C_NORMAL_BG);
        tui_goto(TUI_CONTENT_TOP + 3, 2);
        tui_setcol(C_LABEL_FG, C_NORMAL_BG);
        tui_write("Known hosts: gateway  dns  localhost  host  atmkoala");
        tui_goto(TUI_CONTENT_TOP + 4, 2);
        tui_setcol(C_HINT_FG, C_NORMAL_BG);
        tui_write("(Enter an IP directly to test connectivity)");

        tui_goto(TUI_CONTENT_TOP + 6, 2);
        tui_setcol(C_LABEL_FG, C_NORMAL_BG);
        tui_write("Hostname: ");
        char hostname[64] = {0};
        tui_readline(TUI_CONTENT_TOP + 6, 12, 40, hostname, sizeof(hostname));

        if (!hostname[0] || hostname[0] == KEY_ESC) return SCREEN_MAIN;

        uint8_t resolved[4] = {0};
        int row = TUI_CONTENT_TOP + 8;
        tui_progress(row, "Resolving", 6); row += 2;

        if (unm_dns_resolve(hostname, resolved) == 0) {
            char ipbuf[24];
            unm_ip_str(resolved, ipbuf, sizeof(ipbuf));
            tui_setcol(C_OK_FG, C_NORMAL_BG);
            tui_goto(row, 2); tui_write(hostname); tui_write("  ->  "); tui_write(ipbuf);
            row++;

            /* ARP probe */
            if (net.initialized && g_unm.state == UNM_STATE_UP) {
                row++;
                tui_setcol(C_HINT_FG, C_NORMAL_BG);
                tui_goto(row, 2); tui_write("Sending ARP probe...");
                net_arp_request(resolved);
                pit_sleep(50);
                tui_goto(row, 2); tui_write("ARP sent (probe complete). ");
            }
        } else {
            tui_setcol(C_ERR_FG, C_NORMAL_BG);
            tui_goto(row, 2); tui_write("Lookup failed: unknown host '");
            tui_write(hostname); tui_write("'");
        }

        row += 2;
        tui_setcol(C_HINT_FG, C_NORMAL_BG);
        tui_goto(row, 2); tui_write("Press any key (Q=quit to main)...");
        int k = keyboard_getkey();
        if (k == 'q' || k == 'Q' || k == KEY_ESC) return SCREEN_MAIN;
    }
}

/* ─────────────────────────────────────────────────────────────
 *  SCREEN: Statistics
 * ───────────────────────────────────────────────────────────── */
static tui_screen_t tui_screen_stats(void) {
    terminal_clear();
    tui_draw_chrome("Network Statistics");
    tui_clear_content();

    tui_write_centered(TUI_CONTENT_TOP + 1,
        "eth0 — RTL8139 statistics", C_LABEL_FG, C_NORMAL_BG);

    tui_setcol(C_FRAME_FG, C_NORMAL_BG);
    tui_goto(TUI_CONTENT_TOP + 2, 4);
    for (int i = 0; i < 50; i++) tui_putch('-');

    int row = TUI_CONTENT_TOP + 3;
    char buf[40];

    #define STAT_ROW(label, val) \
        tui_setcol(C_LABEL_FG, C_NORMAL_BG); \
        tui_goto(row, 6); tui_write(label); \
        tui_setcol(VGA_WHITE, C_NORMAL_BG); \
        kuitoa((uint32_t)(val), buf, 10); tui_goto(row, 30); tui_write(buf); row++;

    if (net.initialized) {
        STAT_ROW("RX Packets :", net.rx_packets);
        STAT_ROW("TX Packets :", net.tx_packets);
        STAT_ROW("RX Bytes   :", net.rx_bytes);
        STAT_ROW("TX Bytes   :", net.tx_bytes);
        row++;
        STAT_ROW("IO Base    :", net.io_base);
    } else {
        tui_setcol(C_ERR_FG, C_NORMAL_BG);
        tui_goto(row, 6); tui_write("No NIC detected.");
    }
    #undef STAT_ROW

    row += 2;
    tui_setcol(C_LABEL_FG, C_NORMAL_BG);
    tui_goto(row, 6); tui_write("UNM State    :");
    const char *states[] = {"DOWN","CONNECTING","UP","FAILED"};
    tui_setcol(g_unm.state == UNM_STATE_UP ? C_OK_FG : C_ERR_FG, C_NORMAL_BG);
    tui_goto(row, 30); tui_write(states[g_unm.state]); row++;

    if (g_unm.state == UNM_STATE_UP) {
        unm_ip_str(g_unm.leased_ip,  buf, sizeof(buf));
        tui_setcol(C_LABEL_FG, C_NORMAL_BG);
        tui_goto(row, 6); tui_write("Assigned IP  :");
        tui_setcol(VGA_WHITE, C_NORMAL_BG);
        tui_goto(row, 30); tui_write(buf); row++;
    }

    row += 2;
    tui_setcol(C_HINT_FG, C_NORMAL_BG);
    tui_goto(row, 6); tui_write("Press any key to return...");
    keyboard_getkey();
    return SCREEN_MAIN;
}

/* ─────────────────────────────────────────────────────────────
 *  Main entry point
 * ───────────────────────────────────────────────────────────── */
void untui_run(void) {
    tui_screen_t screen = SCREEN_MAIN;

    while (screen != SCREEN_QUIT) {
        switch (screen) {
            case SCREEN_MAIN:     screen = tui_screen_main();     break;
            case SCREEN_CONNECT:  screen = tui_screen_connect();  break;
            case SCREEN_PROFILES: screen = tui_screen_profiles(); break;
            case SCREEN_STATIC:   screen = tui_screen_static();   break;
            case SCREEN_DNS:      screen = tui_screen_dns();      break;
            case SCREEN_STATS:    screen = tui_screen_stats();    break;
            default:              screen = SCREEN_QUIT;           break;
        }
    }

    terminal_clear();
    terminal_print_logo();
}
