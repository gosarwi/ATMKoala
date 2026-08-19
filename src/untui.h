#ifndef UNTUI_H
#define UNTUI_H

/*  untui.h — Network TUI (text UI) for atmkoala
 *
 *  Interactive VGA text-mode interface for UNM.
 *  Invoked via shell command: untui
 *
 *  Screens:
 *    [0] Main menu     — status, quick connect, profiles, settings
 *    [1] Connect       — DHCP or static connect progress
 *    [2] Profiles      — list / create / delete profiles
 *    [3] Static setup  — wizard to enter IP / GW / DNS
 *    [4] DNS lookup    — resolve hostname
 *    [5] Stats         — RX/TX counters
 */

void untui_run(void);

#endif
