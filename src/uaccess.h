#ifndef ATM_UACCESS_H
#define ATM_UACCESS_H

#include <stddef.h>
#include <stdint.h>
#include "paging.h"

int copy_from_user(const user_space_t *space, void *dst, const void *user_src, size_t len);
int copy_to_user(const user_space_t *space, void *user_dst, const void *src, size_t len);
/* Validate a user range without reading or writing its contents. write_access
 * is nonzero for a destination that the kernel intends to modify. */
int user_range_valid(const user_space_t *space, const void *user_ptr, size_t len,
                     int write_access);
int strnlen_user(const user_space_t *space, const char *user_src, size_t max, size_t *len_out);
int copy_string_from_user(const user_space_t *space, char *dst, size_t dst_size,
                          const char *user_src);
int uaccess_selftest(void);

#endif
