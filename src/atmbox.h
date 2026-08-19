#ifndef ATMBOX_H
#define ATMBOX_H

/* atm-box is a native multi-call command set.  It is intentionally not a
 * binary-compatible BusyBox port: its applets call ATMKoala VFS directly. */
int atmbox_dispatch(int argc, char *argv[]);
void atmbox_print_applets(void);

#endif
