#include "users.h"
#include "config.h"
#include "util.h"
#include "vfs.h"
#include "catfs_vfs.h"
#include <stdint.h>
#include <stddef.h>

#define USER_CFG_PATH_RAM  "/uiu/etc/users.conf"
#define USER_CFG_PATH_DISK "/data/uiu/etc/users.conf"

static const char *user_cfg_path(void) {
    return catfs_vfs_is_mounted() ? USER_CFG_PATH_DISK : USER_CFG_PATH_RAM;
}

typedef struct {
    uint32_t state[8];
    uint64_t bits;
    uint8_t  block[64];
    uint32_t used;
} sha256_ctx_t;

static user_account_t g_users[USER_MAX];
static int g_user_count = 0;
static int g_current = -1;
static uint32_t g_salt_counter = 1;

static const uint32_t sha256_k[64] = {
    0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
    0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
    0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
    0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
    0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
    0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
    0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
    0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
};

static uint32_t rotr32(uint32_t v, uint32_t n) { return (v >> n) | (v << (32U - n)); }

static void sha256_block(sha256_ctx_t *ctx, const uint8_t *p) {
    uint32_t w[64];
    for (uint32_t i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) |
               ((uint32_t)p[i*4+2] << 8) | (uint32_t)p[i*4+3];
    for (uint32_t i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i-15],7) ^ rotr32(w[i-15],18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr32(w[i-2],17) ^ rotr32(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=ctx->state[0], b=ctx->state[1], c=ctx->state[2], d=ctx->state[3];
    uint32_t e=ctx->state[4], f=ctx->state[5], g=ctx->state[6], h=ctx->state[7];
    for (uint32_t i = 0; i < 64; i++) {
        uint32_t s1 = rotr32(e,6) ^ rotr32(e,11) ^ rotr32(e,25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + s1 + ch + sha256_k[i] + w[i];
        uint32_t s0 = rotr32(a,2) ^ rotr32(a,13) ^ rotr32(a,22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    ctx->state[0]+=a; ctx->state[1]+=b; ctx->state[2]+=c; ctx->state[3]+=d;
    ctx->state[4]+=e; ctx->state[5]+=f; ctx->state[6]+=g; ctx->state[7]+=h;
}

static void sha256_init(sha256_ctx_t *ctx) {
    ctx->state[0]=0x6a09e667U; ctx->state[1]=0xbb67ae85U; ctx->state[2]=0x3c6ef372U; ctx->state[3]=0xa54ff53aU;
    ctx->state[4]=0x510e527fU; ctx->state[5]=0x9b05688cU; ctx->state[6]=0x1f83d9abU; ctx->state[7]=0x5be0cd19U;
    ctx->bits=0; ctx->used=0;
}

static void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, uint32_t len) {
    ctx->bits += (uint64_t)len * 8U;
    while (len) {
        uint32_t n = 64U - ctx->used;
        if (n > len) n = len;
        for (uint32_t i=0; i<n; i++) ctx->block[ctx->used+i] = data[i];
        ctx->used += n; data += n; len -= n;
        if (ctx->used == 64U) { sha256_block(ctx, ctx->block); ctx->used=0; }
    }
}

static void sha256_final(sha256_ctx_t *ctx, uint8_t out[32]) {
    uint64_t bits = ctx->bits;
    ctx->block[ctx->used++] = 0x80;
    if (ctx->used > 56U) {
        while (ctx->used < 64U) ctx->block[ctx->used++] = 0;
        sha256_block(ctx, ctx->block); ctx->used=0;
    }
    while (ctx->used < 56U) ctx->block[ctx->used++] = 0;
    for (int i=7; i>=0; i--) ctx->block[ctx->used++] = (uint8_t)(bits >> (i*8));
    sha256_block(ctx, ctx->block);
    for (uint32_t i=0; i<8; i++) {
        out[i*4]=(uint8_t)(ctx->state[i] >> 24); out[i*4+1]=(uint8_t)(ctx->state[i] >> 16);
        out[i*4+2]=(uint8_t)(ctx->state[i] >> 8); out[i*4+3]=(uint8_t)ctx->state[i];
    }
}

static void copy_text(char *dst, const char *src, uint32_t cap) {
    uint32_t i=0;
    if (!src) src="";
    while (src[i] && i+1<cap) { dst[i]=src[i]; i++; }
    dst[i]=0;
}

static void password_hash(const char *password, const char *salt, char out[65]) {
    static const char hex[]="0123456789abcdef";
    sha256_ctx_t ctx; uint8_t digest[32];
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)salt, (uint32_t)kstrlen(salt));
    sha256_update(&ctx, (const uint8_t *)":", 1);
    sha256_update(&ctx, (const uint8_t *)password, (uint32_t)kstrlen(password));
    sha256_final(&ctx, digest);
    for (uint32_t i=0; i<32; i++) { out[i*2]=hex[digest[i]>>4]; out[i*2+1]=hex[digest[i]&15]; }
    out[64]=0;
}

static int secure_equal(const char *a, const char *b) {
    uint8_t diff=0;
    for (uint32_t i=0; i<64; i++) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

static int valid_name(const char *name) {
    uint32_t n=0;
    if (!name || !name[0]) return 0;
    if (!((name[0]>='a'&&name[0]<='z') || (name[0]>='A'&&name[0]<='Z'))) return 0;
    while (name[n]) {
        char c=name[n++];
        if (!((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_' || c=='-')) return 0;
        if (n>USER_NAME_MAX) return 0;
    }
    return 1;
}
int user_name_is_valid(const char *name) { return valid_name(name); }

static void make_salt(const char *name, uint32_t uid, char out[9]) {
    uint32_t h=2166136261U ^ g_salt_counter++;
    while (*name) { h ^= (uint8_t)*name++; h *= 16777619U; }
    h ^= uid * 2654435761U;
    static const char hex[]="0123456789abcdef";
    for (int i=0; i<8; i++) { out[i]=hex[(h >> ((i&7)*4)) & 15U]; h=(h<<5)|(h>>27); }
    out[8]=0;
}

static int index_of(const char *name) {
    for (int i=0; i<g_user_count; i++) if (g_users[i].active && !kstrcmp(g_users[i].name,name)) return i;
    return -1;
}

static uint32_t next_uid(user_role_t role) {
    uint32_t base=(role==ROLE_GUEST)?USER_UID_GUEST:USER_UID_FIRST, uid=base;
    for (;;) {
        int used=0;
        for (int i=0;i<g_user_count;i++) if (g_users[i].active && g_users[i].uid==uid) {used=1;break;}
        if (!used) return uid;
        uid++;
    }
}

const char *user_role_name(user_role_t role) {
    if (role==ROLE_GUEST) return "guest";
    if (role==ROLE_USER) return "user";
    if (role==ROLE_OPERATOR) return "operator";
    if (role==ROLE_ADMIN) return "admin";
    return "unknown";
}

int user_parse_role(const char *name, user_role_t *out) {
    if (!name || !out) return -1;
    if (!kstrcmp(name,"guest")) *out=ROLE_GUEST;
    else if (!kstrcmp(name,"user")) *out=ROLE_USER;
    else if (!kstrcmp(name,"operator")) *out=ROLE_OPERATOR;
    else if (!kstrcmp(name,"admin") || !kstrcmp(name,"root")) *out=ROLE_ADMIN;
    else return -1;
    return 0;
}

static void add_loaded(const cfg_section_t *section) {
    if (g_user_count>=USER_MAX || !valid_name(section->name)) return;
    const char *uid=cfg_get(&g_usercfg,section->name,"uid");
    const char *gid=cfg_get(&g_usercfg,section->name,"gid");
    const char *role=cfg_get(&g_usercfg,section->name,"role");
    const char *home=cfg_get(&g_usercfg,section->name,"home");
    const char *shell=cfg_get(&g_usercfg,section->name,"shell");
    const char *salt=cfg_get(&g_usercfg,section->name,"salt");
    const char *hash=cfg_get(&g_usercfg,section->name,"password");
    const char *active=cfg_get(&g_usercfg,section->name,"active");
    user_role_t parsed;
    if (!uid || !gid || !role || !salt || !hash || !user_parse_role(role,&parsed) || kstrlen(hash)!=64) return;
    user_account_t *u=&g_users[g_user_count++];
    kmemset(u,0,sizeof(*u));
    copy_text(u->name,section->name,sizeof(u->name)); u->uid=kstrtou(uid,10); u->gid=kstrtou(gid,10); u->role=parsed;
    copy_text(u->home,home?home:"/home",sizeof(u->home)); copy_text(u->shell,shell?shell:"/bin/sh",sizeof(u->shell));
    copy_text(u->salt,salt,sizeof(u->salt)); copy_text(u->passhash,hash,sizeof(u->passhash));
    u->active=(!active || kstrcmp(active,"0"))?1:0;
}

int user_save(void) {
    g_usercfg.count=0; g_usercfg.dirty=0;
    cfg_set(&g_usercfg,"meta","format","1");
    for (int i=0; i<g_user_count; i++) {
        user_account_t *u=&g_users[i]; char number[16];
        if (!u->active) continue;
        kuitoa(u->uid,number,10); cfg_set(&g_usercfg,u->name,"uid",number);
        kuitoa(u->gid,number,10); cfg_set(&g_usercfg,u->name,"gid",number);
        cfg_set(&g_usercfg,u->name,"role",user_role_name(u->role));
        cfg_set(&g_usercfg,u->name,"home",u->home); cfg_set(&g_usercfg,u->name,"shell",u->shell);
        cfg_set(&g_usercfg,u->name,"salt",u->salt); cfg_set(&g_usercfg,u->name,"password",u->passhash);
        cfg_set(&g_usercfg,u->name,"active","1");
    }
    if (catfs_vfs_is_mounted()) {
        (void)vfs_mkdir("/data/uiu",0755);
        (void)vfs_mkdir("/data/uiu/etc",0755);
    }
    return cfg_save(&g_usercfg,user_cfg_path());
}

static void initialise_account(user_account_t *u, const char *name, user_role_t role, const char *password, uint32_t uid) {
    kmemset(u,0,sizeof(*u)); copy_text(u->name,name,sizeof(u->name)); u->uid=uid; u->gid=uid; u->role=role; u->active=1;
    if (uid==USER_UID_ROOT) { copy_text(u->home,"/root",sizeof(u->home)); u->gid=0; }
    else { copy_text(u->home,"/home/",sizeof(u->home)); kstrcat(u->home,name); }
    copy_text(u->shell,"/bin/sh",sizeof(u->shell)); make_salt(name,uid,u->salt); password_hash(password,u->salt,u->passhash);
}

int user_init(const char *legacy_pin) {
    /* Existing users.conf always wins. New development images intentionally use
     * the documented bootstrap password only until the owner changes it. */
    (void)legacy_pin;
    kmemset(g_users,0,sizeof(g_users)); g_user_count=0; g_current=-1;
    /* Config core keeps a RAMFS-compatible default.  Prefer the CatFS copy
     * whenever /data is mounted so account changes survive reboot. */
    if (catfs_vfs_is_mounted()) (void)cfg_load(&g_usercfg,user_cfg_path());
    for (int i=0; i<g_usercfg.count; i++) if (kstrcmp(g_usercfg.sections[i].name,"meta")) add_loaded(&g_usercfg.sections[i]);
    if (index_of("root")<0 && g_user_count<USER_MAX) {
        initialise_account(&g_users[g_user_count++],"root",ROLE_ADMIN,"atmkoala",USER_UID_ROOT);
    }
    if (index_of("user")<0 && g_user_count<USER_MAX) {
        initialise_account(&g_users[g_user_count++],"user",ROLE_USER,"atmkoala",USER_UID_FIRST);
    }
    if (index_of("guest")<0 && g_user_count<USER_MAX) {
        initialise_account(&g_users[g_user_count++],"guest",ROLE_GUEST,"atmkoala",USER_UID_GUEST);
    }
    user_save();
    int root=index_of("root");
    if (root<0) return -1;
    g_current=root; vfs_set_credentials(g_users[root].uid,g_users[root].gid);
    return 0;
}

int user_add(const char *name, user_role_t role, const char *password) {
    if (!valid_name(name) || !password || kstrlen(password)<4 || role<ROLE_GUEST || role>ROLE_ADMIN) return -1;
    if (g_user_count>=USER_MAX || index_of(name)>=0) return -1;
    if (role==ROLE_ADMIN) return -1; /* root is the sole UID 0 identity in v0.5 */
    uint32_t uid=next_uid(role); user_account_t *u=&g_users[g_user_count++];
    initialise_account(u,name,role,password,uid);
    if (role!=ROLE_GUEST) { (void)vfs_mkdir(u->home,0750); (void)vfs_chown(u->home,u->uid,u->gid); (void)vfs_chmod(u->home,0750); }
    return user_save();
}

int user_del(const char *name) {
    int idx=index_of(name);
    if (idx<0 || !kstrcmp(name,"root")) return -1;
    for (int i=idx;i+1<g_user_count;i++) g_users[i]=g_users[i+1];
    g_user_count--; kmemset(&g_users[g_user_count],0,sizeof(g_users[0]));
    if (g_current==idx) g_current=index_of("root"); else if (g_current>idx) g_current--;
    return user_save();
}

int user_set_password(const char *name, const char *password) {
    int idx=index_of(name);
    if (idx<0 || !password || kstrlen(password)<4) return -1;
    make_salt(name,g_users[idx].uid,g_users[idx].salt); password_hash(password,g_users[idx].salt,g_users[idx].passhash);
    return user_save();
}

int user_set_role(const char *name, user_role_t role) {
    int idx=index_of(name);
    if (idx<0 || !kstrcmp(name,"root") || role<ROLE_GUEST || role>ROLE_OPERATOR) return -1;
    g_users[idx].role=role;
    return user_save();
}

int user_auth(const char *name, const char *password) {
    int idx=index_of(name); char hash[65];
    if (idx<0 || !password || !g_users[idx].active) return 0;
    password_hash(password,g_users[idx].salt,hash);
    return secure_equal(hash,g_users[idx].passhash);
}

int user_login(const char *name, const char *password) {
    int idx=index_of(name);
    if (idx<0 || !user_auth(name,password)) return -1;
    g_current=idx; vfs_set_credentials(g_users[idx].uid,g_users[idx].gid);
    return 0;
}

void user_logout(void) {
    int guest=index_of("guest");
    if (guest>=0) { g_current=guest; vfs_set_credentials(g_users[guest].uid,g_users[guest].gid); }
}

const user_account_t *user_current(void) { return (g_current>=0 && g_current<g_user_count)?&g_users[g_current]:NULL; }
const user_account_t *user_find(const char *name) { int idx=index_of(name); return idx<0?NULL:&g_users[idx]; }
const user_account_t *user_at(int index) { return (index>=0 && index<g_user_count && g_users[index].active)?&g_users[index]:NULL; }
int user_count(void) { return g_user_count; }
int user_is_admin(void) { const user_account_t *u=user_current(); return u && u->role==ROLE_ADMIN; }
int user_can_sudo(void) { const user_account_t *u=user_current(); return u && (u->role==ROLE_OPERATOR || u->role==ROLE_ADMIN); }
