#ifndef ATM_TZIF_H
#define ATM_TZIF_H

#include <stdint.h>
#include "rtc.h"

/* Reads a selected TZif v1/v2/v3 file from /data/uiu/tzif/<IANA-name>.
 * The parser retains a bounded transition/type subset for one active zone. */
int tzif_load(const char *zone);
int tzif_convert(const char *zone,const rtc_datetime_t *utc,rtc_datetime_t *local,
                 int *offset_minutes,int *dst_active);
const char *tzif_active_name(void);
int tzif_is_loaded(const char *zone);
int tzif_import(const char *source_path,const char *zone);
int tzif_remove(const char *zone);
void tzif_clear_active(void);
int tzif_selftest(void);

#endif
