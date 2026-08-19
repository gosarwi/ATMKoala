#include "atminit.h"
#include "config.h"
#include "vfs.h"
#include "catfs.h"
#include "sched.h"
#include "net.h"
#include "unm.h"
#include "catfs_vfs.h"
#include "util.h"
#include "vga.h"
#include <stdint.h>

#define RL_BIT(level) (1U << (uint32_t)(level))
#define INIT_CFG_PATH "/data/uiu/etc/init.conf"
#define INIT_LOG_DIR "/data/uiu/var/log"
#define INIT_LOG_PATH "/data/uiu/var/log/init.log"

typedef int (*atm_service_cb_t)(void);

typedef struct {
    atm_service_info_t info;
    atm_service_cb_t start;
    atm_service_cb_t stop;
    int visiting;
} service_entry_t;

static cfg_file_t g_initcfg;
static atm_init_log_entry_t g_logs[ATM_INIT_LOG_MAX];
static uint32_t g_log_head;
static uint32_t g_log_count;
static int g_log_persist_ready;
static atm_runlevel_t g_runlevel = ATM_RUNLEVEL_SINGLE;
static int g_initialised;

static uint32_t init_now(void) { return sched_uptime_ticks(); }

static void initlog_copy(char *dst,uint32_t size,const char *src) {
    if (!size) return;
    kstrncpy(dst,src ? src : "",size-1);
    dst[size-1]=0;
}

static void initlog_persist(const atm_init_log_entry_t *entry) {
    /* ext2_vfs is intentionally read-only. Persistent init logs use CatFS only
     * after its writable VFS mount is available; live/failed-mount boots retain
     * the bounded in-memory log instead of failing or mutating ext2. */
    if (!catfs_vfs_is_mounted()) return;
    if (!g_log_persist_ready) {
        (void)vfs_mkdir("/data/uiu",0755);
        (void)vfs_mkdir("/data/uiu/var",0755);
        (void)vfs_mkdir(INIT_LOG_DIR,0755);
        g_log_persist_ready=1;
    }
    char line[160];
    int len=ksnprintf(line,sizeof(line),"[%6us] %-7s %-12s %s\n",entry->tick/100U,entry->event,entry->service,entry->detail);
    if (len<=0) return;
    if ((uint32_t)len>=sizeof(line)) len=(int)sizeof(line)-1;
    int fd=vfs_open(INIT_LOG_PATH,O_WRONLY|O_CREAT|O_APPEND,0644);
    if (fd<0) return;
    (void)vfs_write(fd,line,(uint32_t)len);
    vfs_close(fd);
}

static void initlog(const char *event,const char *name,const char *detail) {
    atm_init_log_entry_t *entry=&g_logs[g_log_head];
    entry->tick=init_now();
    initlog_copy(entry->event,sizeof(entry->event),event);
    initlog_copy(entry->service,sizeof(entry->service),name);
    initlog_copy(entry->detail,sizeof(entry->detail),detail);
    g_log_head=(g_log_head+1U)%ATM_INIT_LOG_MAX;
    if (g_log_count<ATM_INIT_LOG_MAX) g_log_count++;
    kprintf("[init:%u] %-7s %-12s %s\n",entry->tick/100U,entry->event,entry->service,entry->detail);
    initlog_persist(entry);
}

static void service_state_set(service_entry_t *entry,atm_service_state_t state) {
    entry->info.state=state;
    entry->info.last_change_tick=init_now();
    if (state==ATM_SERVICE_STARTED) entry->info.start_tick=entry->info.last_change_tick;
}

static const char *initconf_get(const char *key) {
    return catfs_vfs_is_mounted() ? cfg_get(&g_initcfg,"init",key) : sysconf_get("init",key);
}

static void initconf_set(const char *key, const char *value) {
    if (!catfs_vfs_is_mounted()) { sysconf_set("init",key,value); sysconf_save(); return; }
    (void)vfs_mkdir("/data/uiu",0755);
    (void)vfs_mkdir("/data/uiu/etc",0755);
    cfg_set(&g_initcfg,"init",key,value);
    (void)cfg_save(&g_initcfg,INIT_CFG_PATH);
}

static int svc_filesystem_start(void) { return vfs_root() ? 0 : -1; }
static int svc_filesystem_stop(void) { if (catfs.mounted) catfs_sync(); return 0; }
static int svc_scheduler_start(void) { sched_init(); return 0; }
static int svc_noop_stop(void) { return 0; }
static int svc_network_start(void) { net_init(); return 0; }
static int svc_usernet_start(void) { unm_init(); return 0; }
static int svc_exp_start(void) { return 0; }

static service_entry_t g_services[ATM_SERVICE_MAX] = {
    {{"filesystem", "VFS and mounted storage", "", RL_BIT(1)|RL_BIT(3)|RL_BIT(5), ATM_SERVICE_STOPPED, 1, 0, 0}, svc_filesystem_start, svc_filesystem_stop, 0},
    {{"scheduler",  "Cooperative task scheduler", "filesystem", RL_BIT(1)|RL_BIT(3)|RL_BIT(5), ATM_SERVICE_STOPPED, 1, 0, 0}, svc_scheduler_start, svc_noop_stop, 0},
    {{"network",    "Network driver discovery", "filesystem", RL_BIT(3)|RL_BIT(5), ATM_SERVICE_STOPPED, 1, 0, 0}, svc_network_start, svc_noop_stop, 0},
    {{"usernet",    "UserNet network manager", "network", RL_BIT(3)|RL_BIT(5), ATM_SERVICE_STOPPED, 1, 0, 0}, svc_usernet_start, svc_noop_stop, 0},
    {{"exp",        "Exp graphical desktop", "scheduler", RL_BIT(5), ATM_SERVICE_STOPPED, 1, 0, 0}, svc_exp_start, svc_noop_stop, 0}
};
static const int g_service_count = 5;

static service_entry_t *entry_find(const char *name) {
    if (!name) return NULL;
    for (int i=0;i<g_service_count;i++)
        if (!kstrcmp(g_services[i].info.name,name)) return &g_services[i];
    return NULL;
}

static int dependency_present(const char *list, const char *name) {
    if (!list || !name) return 0;
    const char *p=list;
    while (*p) {
        const char *begin=p;
        while (*p && *p!=',') p++;
        uint32_t len=(uint32_t)(p-begin);
        if (kstrlen(name)==len && !kmemcmp(begin,name,len)) return 1;
        if (*p==',') p++;
    }
    return 0;
}

static int start_entry(service_entry_t *entry, int manual) {
    if (!entry) return -1;
    if (entry->info.state==ATM_SERVICE_STARTED) return 0;
    initlog("start",entry->info.name,manual ? "manual request" : "dependency/runlevel request");
    if (entry->visiting) {
        service_state_set(entry,ATM_SERVICE_FAILED);
        initlog("failed",entry->info.name,"dependency cycle");
        return -1;
    }
    /* A disabled dependency must never be silently treated as running. */
    if (!manual && !entry->info.enabled) {
        initlog("skipped",entry->info.name,"disabled by configuration");
        return -1;
    }
    entry->visiting=1;
    const char *p=entry->info.dependencies;
    while (p && *p) {
        char dep[32]; uint32_t n=0;
        while (*p && *p!=',' && n+1<sizeof(dep)) dep[n++]=*p++;
        dep[n]=0;
        if (*p==',') p++;
        if (dep[0] && start_entry(entry_find(dep),0)<0) {
            entry->visiting=0;
            service_state_set(entry,ATM_SERVICE_FAILED);
            initlog("failed",entry->info.name,"dependency unavailable");
            return -1;
        }
    }
    entry->visiting=0;
    if (entry->start && entry->start()<0) {
        service_state_set(entry,ATM_SERVICE_FAILED);
        initlog("failed",entry->info.name,"start callback");
        return -1;
    }
    service_state_set(entry,ATM_SERVICE_STARTED);
    initlog("started",entry->info.name,entry->info.description);
    return 0;
}

static int has_started_dependent(const service_entry_t *target) {
    for (int i=0;i<g_service_count;i++) {
        if (g_services[i].info.state==ATM_SERVICE_STARTED &&
            dependency_present(g_services[i].info.dependencies,target->info.name)) return 1;
    }
    return 0;
}

const char *atminit_runlevel_name(atm_runlevel_t runlevel) {
    if (runlevel==ATM_RUNLEVEL_HALT) return "halt";
    if (runlevel==ATM_RUNLEVEL_SINGLE) return "single";
    if (runlevel==ATM_RUNLEVEL_MULTI) return "multi";
    if (runlevel==ATM_RUNLEVEL_GRAPHICAL) return "graphical";
    return "unknown";
}

int atminit_parse_runlevel(const char *text, atm_runlevel_t *out) {
    if (!text || !out) return -1;
    if (!kstrcmp(text,"0") || !kstrcmp(text,"halt")) *out=ATM_RUNLEVEL_HALT;
    else if (!kstrcmp(text,"1") || !kstrcmp(text,"single") || !kstrcmp(text,"maintenance")) *out=ATM_RUNLEVEL_SINGLE;
    else if (!kstrcmp(text,"3") || !kstrcmp(text,"multi") || !kstrcmp(text,"default")) *out=ATM_RUNLEVEL_MULTI;
    else if (!kstrcmp(text,"5") || !kstrcmp(text,"graphical")) *out=ATM_RUNLEVEL_GRAPHICAL;
    else return -1;
    return 0;
}

void atminit_init(void) {
    kmemset(&g_initcfg,0,sizeof(g_initcfg));
    kmemset(g_logs,0,sizeof(g_logs));
    g_log_head=0; g_log_count=0; g_log_persist_ready=0;
    if (catfs_vfs_is_mounted()) (void)cfg_load(&g_initcfg,INIT_CFG_PATH);
    for (int i=0;i<g_service_count;i++) {
        service_state_set(&g_services[i],ATM_SERVICE_STOPPED);
        g_services[i].info.start_tick=0;
        g_services[i].visiting=0;
        const char *enabled=initconf_get(g_services[i].info.name);
        g_services[i].info.enabled=!(enabled && !kstrcmp(enabled,"off"));
    }
    g_initialised=1;
    initlog("ready","init","service manager initialized");
}

int atminit_set_runlevel(atm_runlevel_t runlevel) {
    if (!g_initialised) return -1;
    if (runlevel!=ATM_RUNLEVEL_HALT && runlevel!=ATM_RUNLEVEL_SINGLE &&
        runlevel!=ATM_RUNLEVEL_MULTI && runlevel!=ATM_RUNLEVEL_GRAPHICAL) return -1;
    uint32_t mask=RL_BIT(runlevel);
    initlog("runlevel",atminit_runlevel_name(runlevel),"transition");
    /* Stop dependents before their dependencies. */
    for (int i=g_service_count-1;i>=0;i--) {
        service_entry_t *entry=&g_services[i];
        if (entry->info.state==ATM_SERVICE_STARTED && !(entry->info.runlevel_mask&mask)) {
            if (entry->stop && entry->stop()<0) {
                service_state_set(entry,ATM_SERVICE_FAILED);
                initlog("failed",entry->info.name,"stop callback");
                return -1;
            }
            service_state_set(entry,ATM_SERVICE_STOPPED);
            initlog("stopped",entry->info.name,"runlevel transition");
        }
    }
    if (runlevel!=ATM_RUNLEVEL_HALT) {
        for (int i=0;i<g_service_count;i++) {
            service_entry_t *entry=&g_services[i];
            if ((entry->info.runlevel_mask&mask) && entry->info.enabled && start_entry(entry,0)<0)
                return -1;
        }
    }
    g_runlevel=runlevel;
    char number[2]; number[0]=(char)('0'+(int)runlevel); number[1]=0;
    initconf_set("runlevel",number);
    return 0;
}

int atminit_boot(void) {
    atm_runlevel_t target=ATM_RUNLEVEL_MULTI;
    const char *saved=initconf_get("runlevel");
    if (saved) (void)atminit_parse_runlevel(saved,&target);
    return atminit_set_runlevel(target);
}

atm_runlevel_t atminit_runlevel(void) { return g_runlevel; }
int atminit_start(const char *name) { return start_entry(entry_find(name),1); }

int atminit_stop(const char *name) {
    service_entry_t *entry=entry_find(name);
    if (!entry || entry->info.state!=ATM_SERVICE_STARTED) return -1;
    if (has_started_dependent(entry)) {
        initlog("blocked",entry->info.name,"started dependent exists");
        return -1;
    }
    if (entry->stop && entry->stop()<0) {
        service_state_set(entry,ATM_SERVICE_FAILED);
        initlog("failed",entry->info.name,"stop callback");
        return -1;
    }
    service_state_set(entry,ATM_SERVICE_STOPPED);
    initlog("stopped",entry->info.name,"manual request");
    return 0;
}

int atminit_restart(const char *name) {
    service_entry_t *entry=entry_find(name);
    if (!entry) return -1;
    if (entry->info.state==ATM_SERVICE_STARTED && atminit_stop(name)<0) return -1;
    return atminit_start(name);
}

int atminit_set_enabled(const char *name, int enabled) {
    service_entry_t *entry=entry_find(name);
    if (!entry) return -1;
    entry->info.enabled=enabled ? 1 : 0;
    initconf_set(entry->info.name,enabled ? "on" : "off");
    initlog("config",entry->info.name,enabled ? "autostart enabled" : "autostart disabled");
    return 0;
}

int atminit_service_count(void) { return g_service_count; }
const atm_service_info_t *atminit_service_at(int index) {
    return (index>=0 && index<g_service_count) ? &g_services[index].info : NULL;
}
const atm_service_info_t *atminit_service_find(const char *name) {
    service_entry_t *entry=entry_find(name);
    return entry ? &entry->info : NULL;
}
int atminit_log_count(void) { return (int)g_log_count; }
const atm_init_log_entry_t *atminit_log_at(int index) {
    if (index<0 || (uint32_t)index>=g_log_count) return NULL;
    return &g_logs[(g_log_head+ATM_INIT_LOG_MAX-g_log_count+(uint32_t)index)%ATM_INIT_LOG_MAX];
}
const char *atminit_log_path(void) { return INIT_LOG_PATH; }

void atminit_shutdown(void) { (void)atminit_set_runlevel(ATM_RUNLEVEL_HALT); }
