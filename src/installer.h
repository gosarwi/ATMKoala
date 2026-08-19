#ifndef ATM_INSTALLER_H
#define ATM_INSTALLER_H

/* Runs only from the `installer` Multiboot command line boot mode.
 * Returns after success, cancellation or a fatal installer error. */
void installer_run(void);

#endif
