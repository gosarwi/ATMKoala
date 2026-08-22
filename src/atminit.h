#ifndef ATMKOALA_ATMINIT_H
#define ATMKOALA_ATMINIT_H

#include <stdint.h>
#include "util.h"

#define ATM_SERVICE_MAX 6
#define ATM_INIT_LOG_MAX 96
#define ATM_INIT_LOG_EVENT_MAX 12
#define ATM_INIT_LOG_NAME_MAX 24
#define ATM_INIT_LOG_DETAIL_MAX 72

typedef enum {
    ATM_RUNLEVEL_HALT       = 0,
    ATM_RUNLEVEL_SINGLE     = 1,
    ATM_RUNLEVEL_MULTI      = 3,
    ATM_RUNLEVEL_GRAPHICAL  = 5
} atm_runlevel_t;

typedef enum {
    ATM_SERVICE_STOPPED = 0,
    ATM_SERVICE_STARTED,
    ATM_SERVICE_FAILED
} atm_service_state_t;

typedef struct {
    const char          *name;
    const char          *description;
    const char          *dependencies; /* comma-separated built-in names */
    uint32_t             runlevel_mask;
    atm_service_state_t  state;
    int                  enabled;
    uint32_t             start_tick;       /* scheduler ticks; zero if never started */
    uint32_t             last_change_tick; /* scheduler ticks for current state */
} atm_service_info_t;

typedef struct {
    uint32_t tick;
    char event[ATM_INIT_LOG_EVENT_MAX];
    char service[ATM_INIT_LOG_NAME_MAX];
    char detail[ATM_INIT_LOG_DETAIL_MAX];
} atm_init_log_entry_t;

typedef enum {
    ATM_MEMORY_OK = 0,
    ATM_MEMORY_WARN,
    ATM_MEMORY_CRITICAL
} atm_memory_pressure_t;
typedef struct {
    uint32_t heap_used;
    uint32_t heap_free;
    uint64_t task_resident;
    uint32_t task_count;
    uint8_t heap_percent;
    atm_memory_pressure_t pressure;
} atm_memory_status_t;

/* Register native services and load the configured default runlevel. */
void                atminit_init(void);
int                 atminit_boot(void);
int                 atminit_set_runlevel(atm_runlevel_t runlevel);
atm_runlevel_t      atminit_runlevel(void);
const char         *atminit_runlevel_name(atm_runlevel_t runlevel);
int                 atminit_parse_runlevel(const char *text, atm_runlevel_t *out);

/* Native service operations.  They are callback-driven, not fork/exec based. */
int                 atminit_start(const char *name);
int                 atminit_stop(const char *name);
int                 atminit_restart(const char *name);
int                 atminit_set_enabled(const char *name, int enabled);
int                 atminit_service_count(void);
const atm_service_info_t *atminit_service_at(int index);
const atm_service_info_t *atminit_service_find(const char *name);
int                 atminit_log_count(void);
const atm_init_log_entry_t *atminit_log_at(int index); /* chronological, oldest first */
const char         *atminit_log_path(void); /* writable CatFS/VFS sink; Viewer-readable */
/* Actual kprintf records retained by the formatter ring, oldest first. */
int                 atminit_kernel_log_count(void);
const atm_kernel_log_entry_t *atminit_kernel_log_at(int index);
/* Successful GUI/native app spawn event; no user-controlled body is logged. */
void                atminit_note_app_launch(const char *name,const char *detail);
/* Heap allocator plus scheduler residency only; this is not total physical RAM. */
void                atminit_memory_status(atm_memory_status_t *out);
const char         *atminit_memory_pressure_name(atm_memory_pressure_t pressure);
/* Non-destructive runtime diagnostic regression for kprintf capture and memory status. */
int                 atminit_selftest(void);
void                atminit_shutdown(void);

#endif
