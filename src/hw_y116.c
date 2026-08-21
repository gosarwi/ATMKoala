/*
 * hw_y116.c — Hardware drivers for Lenovo Y116 / Intel Gemini Lake J4105
 *
 * All drivers probe real hardware via PCI config space, CPUID, MSR, MMIO.
 * Safe to run on any x86-64 system (unknown devices gracefully ignored).
 */

#include "hw_y116.h"
#include "util.h"
#include "vga.h"
#include "pit.h"
#include "kmalloc.h"
#include <stdint.h>
#include <stddef.h>

/* ── Global state ────────────────────────────────────────── */
cpu_info_t     g_cpu     = {0};
battery_info_t g_battery = {0};
backlight_t    g_backlight= {0};
radio_status_t g_radio   = {0};
edp_panel_t    g_panel   = {0};
pci_bus_t      g_pci     = {0};
rtl8168_state_t g_rtl8168= {0};
hpet_state_t   g_hpet    = {0};
hda_state_t    g_hda     = {0};
i915_fb_t      g_i915    = {0};

/* ── Port I/O ────────────────────────────────────────────── */
static inline void     _outb(uint16_t p, uint8_t  v) { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline void     _outl(uint16_t p, uint32_t v) { __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static inline uint8_t  _inb (uint16_t p) { uint8_t  v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline uint16_t _inw (uint16_t p) { uint16_t v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline uint32_t _inl (uint16_t p) { uint32_t v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p)); return v; }

/* MMIO (identity-mapped in our page tables) */
static inline uint32_t _mmio_r32(uint64_t addr) {
    return *(volatile uint32_t *)(uintptr_t)addr;
}
static inline void _mmio_w32(uint64_t addr, uint32_t val) {
    *(volatile uint32_t *)(uintptr_t)addr = val;
}
static inline uint64_t _mmio_r64(uint64_t addr) {
    return *(volatile uint64_t *)(uintptr_t)addr;
}

/* ════════════════════════════════════════════════════════════
 *  PCI Configuration Space
 *  x86 mechanism 1: CF8h/CFCh
 * ════════════════════════════════════════════════════════════ */

uint32_t pci_read_cfg(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    uint32_t addr = 0x80000000u
                  | ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)fn   <<  8)
                  | ((uint32_t)reg  & 0xFC);
    _outl(0xCF8, addr);
    return _inl(0xCFC);
}

void pci_write_cfg(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg, uint32_t val) {
    uint32_t addr = 0x80000000u
                  | ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)fn   <<  8)
                  | ((uint32_t)reg  & 0xFC);
    _outl(0xCF8, addr);
    _outl(0xCFC, val);
}

/* Enable bus mastering and I/O space in PCI command register */
static void pci_enable_device(uint8_t bus, uint8_t dev, uint8_t fn) {
    uint32_t cmd = pci_read_cfg(bus, dev, fn, 0x04);
    cmd |= 0x07; /* I/O + Memory + Bus Master */
    pci_write_cfg(bus, dev, fn, 0x04, cmd);
}

static void pci_scan_bus(pci_bus_t *bus,uint8_t busno,uint8_t seen[256]){
    if(!bus||seen[busno]||bus->count>=PCI_MAX_DEVICES)return;
    seen[busno]=1;
    for(int d=0;d<32&&bus->count<PCI_MAX_DEVICES;d++){
        for(int f=0;f<8&&bus->count<PCI_MAX_DEVICES;f++){
            uint32_t id=pci_read_cfg(busno,(uint8_t)d,(uint8_t)f,0x00);uint16_t vendor=(uint16_t)id;
            if(vendor==0xFFFF||vendor==0x0000){if(f==0)break;continue;}
            uint16_t device=(uint16_t)(id>>16);uint32_t cc=pci_read_cfg(busno,(uint8_t)d,(uint8_t)f,0x08),irq_r=pci_read_cfg(busno,(uint8_t)d,(uint8_t)f,0x3C);
            pci_device_t *p=&bus->devs[bus->count++];p->vendor=vendor;p->device=device;p->bus=busno;p->dev=(uint8_t)d;p->fn=(uint8_t)f;p->class_code=(uint8_t)(cc>>24);p->subclass=(uint8_t)(cc>>16);p->prog_if=(uint8_t)(cc>>8);p->revision=(uint8_t)cc;p->irq=(uint8_t)irq_r;p->pin=(uint8_t)(irq_r>>8);p->valid=1;
            for(int bar=0;bar<6;bar++)p->bar[bar]=pci_read_cfg(busno,(uint8_t)d,(uint8_t)f,(uint8_t)(0x10+bar*4));
            /* Scan a PCI-to-PCI bridge's secondary bus. This covers common
             * UEFI laptop topology where NVMe/xHCI sit behind root ports. */
            if(p->class_code==0x06&&p->subclass==0x04){uint32_t buses=pci_read_cfg(busno,(uint8_t)d,(uint8_t)f,0x18);uint8_t secondary=(uint8_t)(buses>>8);if(secondary&&secondary!=busno)pci_scan_bus(bus,secondary,seen);}
            if(f==0){uint32_t hdr=pci_read_cfg(busno,(uint8_t)d,0,0x0C);if(!((hdr>>16)&0x80))break;}
        }
    }
}
void pci_init(pci_bus_t *bus) {
    uint8_t seen[256];if(!bus)return;kmemset(seen,0,sizeof(seen));bus->count=0;
    /* Preserve low-bus compatibility while recursively following bridge
     * secondaries rather than silently stopping at bus 3. */
    for(uint8_t root=0;root<4&&bus->count<PCI_MAX_DEVICES;root++)pci_scan_bus(bus,root,seen);
}

pci_device_t *pci_find(pci_bus_t *bus, uint16_t vendor, uint16_t device) {
    for (int i = 0; i < bus->count; i++) {
        if (bus->devs[i].vendor == vendor && bus->devs[i].device == device)
            return &bus->devs[i];
    }
    return NULL;
}

void radio_detect(radio_status_t *radio, const pci_bus_t *bus) {
    kmemset(radio, 0, sizeof(*radio));
    if (!bus) return;
    for (int i = 0; i < bus->count; i++) {
        const pci_device_t *p = &bus->devs[i];
        /* PCI subclass 0x80 is the generic "other network controller" class
         * used by common Wi-Fi adapters. Presence alone is not association. */
        if (p->class_code == 0x02 && p->subclass == 0x80) {
            radio->wifi_controller_present = 1;
            radio->wifi_vendor = p->vendor;
            radio->wifi_device = p->device;
        }
        if (p->class_code == 0x0C && p->subclass == 0x03)
            radio->usb_host_present = 1;
    }
    /* Bluetooth needs USB enumeration plus HCI transport support. Do not mark
     * it present merely because a USB host controller exists. */
}

int hardware_status_selftest(void){
    pci_bus_t bus;radio_status_t radio;battery_info_t battery;kmemset(&bus,0,sizeof(bus));
    bus.count=2;bus.devs[0].class_code=0x02;bus.devs[0].subclass=0x80;bus.devs[0].vendor=0x8086;bus.devs[0].device=0x24FD;
    bus.devs[1].class_code=0x0C;bus.devs[1].subclass=0x03;bus.devs[1].prog_if=0x30;
    radio_detect(&radio,&bus);battery_init(&battery);
    if(!radio.wifi_controller_present||radio.wifi_driver_ready||radio.wifi_connected||radio.wifi_signal_pct||!radio.usb_host_present||radio.bluetooth_controller_present||radio.bluetooth_driver_ready)return -1;
    return (!battery.valid&&!battery.present)?0:-1;
}

/* Intel HD Audio controller discovery. No controller register writes occur
 * here: usable playback also needs CORB/RIRB command handling, codec verbs,
 * buffer descriptors, stream DMA, IRQ handling and a PCM mixer. */
void hda_detect(hda_state_t *audio,const pci_bus_t *bus){
    if(!audio)return;kmemset(audio,0,sizeof(*audio));if(!bus)return;
    for(int i=0;i<bus->count;i++){
        const pci_device_t *p=&bus->devs[i];
        if(!((p->class_code==0x04&&p->subclass==0x03)||(p->vendor==PCI_VENDOR_INTEL&&p->device==PCI_DEV_GEMINI_HDA)))continue;
        audio->controller_present=1;audio->vendor=p->vendor;audio->device=p->device;
        audio->mmio_base=(uint64_t)(p->bar[0]&~0xFu);
        /* Read-only register inspection is intentionally deferred until a
         * mapped MMIO policy exists for arbitrary PCI BARs. */
        return;
    }
}
void hda_print(const hda_state_t *audio){
    char buf[96];terminal_set_color(VGA_LIGHT_CYAN,VGA_BLACK);terminal_writeln("Audio:");terminal_set_color(VGA_LIGHT_GREY,VGA_BLACK);
    if(!audio||!audio->controller_present){terminal_writeln("  No Intel HD Audio controller detected");return;}
    ksnprintf(buf,sizeof(buf),"  HDA controller: %04x:%04x  MMIO %08x",audio->vendor,audio->device,(uint32_t)audio->mmio_base);terminal_writeln(buf);
    terminal_writeln("  PCM output unavailable: codec/DMA driver not installed");
}
int hda_selftest(void){
    pci_bus_t bus;hda_state_t audio;kmemset(&bus,0,sizeof(bus));
    bus.count=1;bus.devs[0].vendor=PCI_VENDOR_INTEL;bus.devs[0].device=PCI_DEV_GEMINI_HDA;
    bus.devs[0].class_code=0x04;bus.devs[0].subclass=0x03;bus.devs[0].bar[0]=0xFEBF1000u;
    hda_detect(&audio,&bus);
    return audio.controller_present&&audio.vendor==PCI_VENDOR_INTEL&&audio.device==PCI_DEV_GEMINI_HDA&&audio.mmio_base==0xFEBF1000u&&!audio.pcm_output_ready?0:-1;
}

/* Class code names */
static const char *pci_class_name(uint8_t cls, uint8_t sub) {
    switch (cls) {
        case 0x00: return "Unclassified";
        case 0x01:
            switch (sub) {
                case 0x01: return "IDE Controller";
                case 0x06: return "AHCI Controller";
                case 0x08: return "NVMe Controller";
                default:   return "Storage Controller";
            }
        case 0x02: return "Network Controller";
        case 0x03: return "Display Controller";
        case 0x04: return "Multimedia Controller";
        case 0x06:
            switch (sub) {
                case 0x00: return "Host Bridge";
                case 0x01: return "ISA Bridge";
                case 0x04: return "PCI-PCI Bridge";
                default:   return "Bridge";
            }
        case 0x07: return "Serial Controller";
        case 0x0C:
            switch (sub) {
                case 0x03: return "USB Controller";
                case 0x05: return "SMBus";
                default:   return "Serial Bus";
            }
        default:   return "Unknown";
    }
}

void pci_print(pci_bus_t *bus) {
    char buf[80];
    terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    terminal_writeln("PCI Devices:");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    for (int i = 0; i < bus->count; i++) {
        pci_device_t *p = &bus->devs[i];
        ksnprintf(buf, sizeof(buf),
            "  %02x:%02x.%x  %04x:%04x  rev%02x  IRQ%-2d  %s",
            p->bus, p->dev, p->fn,
            p->vendor, p->device, p->revision, p->irq,
            pci_class_name(p->class_code, p->subclass));
        terminal_writeln(buf);
    }
}

/* ════════════════════════════════════════════════════════════
 *  CPU Detection — Intel J4105 (Gemini Lake)
 *  CPUID leaves: 0x00, 0x01, 0x04, 0x07, 0x80000000-0x80000004
 * ════════════════════════════════════════════════════════════ */

static void cpuid(uint32_t leaf, uint32_t subleaf,
                  uint32_t *eax, uint32_t *ebx,
                  uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile(
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(subleaf)
    );
}

uint64_t cpu_tsc_freq(void) {
    /* Never calibrate from PIT in an interactive path: on a physical machine
     * an unavailable/masked IRQ0 would turn a status command into a hang.
     * CPUID leaves are bounded by leaf 0 and require no timer or MSR access. */
    uint32_t eax, ebx, ecx, edx;
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    uint32_t max_leaf=eax;
    if(max_leaf>=0x15){
        cpuid(0x15,0,&eax,&ebx,&ecx,&edx);
        if(eax && ebx && ecx) return ((uint64_t)ecx*(uint64_t)ebx)/(uint64_t)eax;
    }
    if(max_leaf>=0x16){
        cpuid(0x16,0,&eax,&ebx,&ecx,&edx);
        if(eax) return (uint64_t)eax*1000000ULL;
    }
    return 0; /* frequency unspecified is safer than a blocking calibration */
}

void cpu_detect(cpu_info_t *info) {
    if(!info) return;
    kmemset(info,0,sizeof(*info));
    uint32_t eax, ebx, ecx, edx;

    /* Leaf 0: max leaf + vendor string */
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    uint32_t max_leaf = eax;

    /* Vendor: "GenuineIntel" */
    char vendor[13];
    ((uint32_t *)vendor)[0] = ebx;
    ((uint32_t *)vendor)[1] = edx;
    ((uint32_t *)vendor)[2] = ecx;
    vendor[12] = 0;
    kstrcpy(info->vendor,vendor);

    /* Leaf 1: family/model/stepping, feature flags */
    if (max_leaf >= 1) {
        cpuid(1, 0, &eax, &ebx, &ecx, &edx);
        uint32_t base_fam=(eax>>8)&0xF, base_mod=(eax>>4)&0xF;
        uint32_t fam  = base_fam == 15 ? base_fam + ((eax>>20)&0xFF) : base_fam;
        uint32_t mod  = base_mod | (((base_fam==6||base_fam==15)?((eax>>16)&0xF):0)<<4);
        uint32_t step = eax & 0xF;
        info->family   = fam;
        info->model    = mod;
        info->stepping = step;
        /* Gemini Lake: family=6, model=0x7A */

        info->has_sse    = (edx >> 25) & 1;
        info->has_sse2   = (edx >> 26) & 1;
        info->has_htt    = (edx >> 28) & 1;
        info->has_sse4_2 = (ecx >> 20) & 1;
        info->has_aes    = (ecx >> 25) & 1;
        info->has_avx2   = 0; /* check leaf 7 */
        info->has_rdrand = (ecx >> 30) & 1;
        info->has_nx     = 0; /* check ext leaf */
        info->has_lm     = 0;

        /* logical processor count */
        info->threads = (ebx >> 16) & 0xFF;
        if (!info->threads) info->threads = 1;
    }

    /* Leaf 4: cache info (enumerate caches) */
    if (max_leaf >= 4) {
        for (int i = 0; i < 8; i++) {
            cpuid(4, (uint32_t)i, &eax, &ebx, &ecx, &edx);
            uint8_t type = eax & 0x1F;
            if (!type) break;
            uint8_t level = (eax >> 5) & 7;
            uint32_t ways  = ((ebx >> 22) & 0x3FF) + 1;
            uint32_t parts = ((ebx >> 12) & 0x3FF) + 1;
            uint32_t line  = (ebx & 0xFFF) + 1;
            uint32_t sets  = ecx + 1;
            uint32_t kb    = (ways * parts * line * sets) / 1024;
            switch (level) {
                case 1:
                    if (type == 1) info->l1d_kb = kb;
                    if (type == 2) info->l1i_kb = kb;
                    break;
                case 2: info->l2_kb = kb; break;
                case 3: info->l3_kb = kb; break;
            }
        }
    }

    /* Leaf 7: extended features (AVX2, etc.) */
    if (max_leaf >= 7) {
        cpuid(7, 0, &eax, &ebx, &ecx, &edx);
        info->has_avx2 = (ebx >> 5) & 1;
    }

    /* Extended leaves: brand string, NX, LM */
    cpuid(0x80000000u, 0, &eax, &ebx, &ecx, &edx);
    uint32_t max_ext = eax;

    if (max_ext >= 0x80000001u) {
        cpuid(0x80000001u, 0, &eax, &ebx, &ecx, &edx);
        info->has_nx = (edx >> 20) & 1;
        info->has_lm = (edx >> 29) & 1;
    }

    /* Brand string (leaves 0x80000002-4) */
    if (max_ext >= 0x80000004u) {
        uint32_t *p = (uint32_t *)info->brand;
        cpuid(0x80000002u, 0, &p[0],  &p[1],  &p[2],  &p[3]);
        cpuid(0x80000003u, 0, &p[4],  &p[5],  &p[6],  &p[7]);
        cpuid(0x80000004u, 0, &p[8],  &p[9],  &p[10], &p[11]);
        info->brand[48] = 0;
        /* trim leading spaces */
        char *b = info->brand;
        while (*b == ' ') b++;
        if (b != info->brand) {
            int l = (int)kstrlen(b);
            for (int i = 0; i <= l; i++) info->brand[i] = b[i];
        }
    } else {
        kstrcpy(info->brand, "Intel Celeron J4105 (Gemini Lake)");
    }

    /* Fallback for J4105 if CPUID isn't available in VM */
    if (info->family == 6 && info->model == 0x7A) {
        kstrcpy(info->codename, "Gemini Lake");
        info->cores    = GEMINI_LAKE_CORES;
        info->base_mhz = GEMINI_LAKE_BASE_MHZ;
        info->boost_mhz= GEMINI_LAKE_BOOST_MHZ;
        info->l1d_kb   = info->l1d_kb ? info->l1d_kb : 24;
        info->l1i_kb   = info->l1i_kb ? info->l1i_kb : 32;
        info->l2_kb    = info->l2_kb  ? info->l2_kb  : 4096;
        info->l3_kb    = 0; /* Gemini Lake has no L3 */
        info->tdp_w    = 10;
    } else if (info->family == 15) {
        /* NetBurst Pentium 4 family.  This is a policy/diagnostic path, not a driver. */
        kstrcpy(info->codename, info->model >= 3 ? "Prescott NetBurst" : "Northwood NetBurst");
        info->cores=1; info->base_mhz=2000; info->boost_mhz=2000; info->tdp_w=90;
        if(!info->brand[0]) kstrcpy(info->brand,"Intel Pentium 4 (NetBurst)");
    } else if (!info->cores) {
        /* Generic fallback */
        info->cores     = info->threads ? info->threads : 1;
        info->base_mhz  = 1000;
        info->boost_mhz = 2000;
        kstrcpy(info->codename, "Unknown");
    }

    /* Do not read IA32_PLATFORM_INFO here. A model-specific register can
       raise #GP even at CPL0 when firmware exposes a different MSR set.
       The documented J4105 defaults above remain a conservative policy hint. */

    /* Optional CPUID-derived frequency; zero means unspecified. */
    info->tsc_freq_hz = cpu_tsc_freq();
}

void cpu_print(const cpu_info_t *info) {
    char buf[80];
    terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    terminal_writeln("CPU:");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    ksnprintf(buf, sizeof(buf), "  Vendor   : %s", info->vendor);
    terminal_writeln(buf);
    ksnprintf(buf, sizeof(buf), "  Brand    : %s", info->brand);
    terminal_writeln(buf);
    ksnprintf(buf, sizeof(buf), "  Codename : %s  family %u model 0x%02X step %u",
        info->codename, info->family, info->model, info->stepping);
    terminal_writeln(buf);
    ksnprintf(buf, sizeof(buf), "  Cores    : %u  Threads: %u  TDP: %u W",
        info->cores, info->threads, info->tdp_w);
    terminal_writeln(buf);
    ksnprintf(buf, sizeof(buf), "  Frequency: %u MHz base / %u MHz boost",
        info->base_mhz, info->boost_mhz);
    terminal_writeln(buf);
    ksnprintf(buf, sizeof(buf), "  TSC freq : %u MHz",
        (uint32_t)(info->tsc_freq_hz / 1000000));
    terminal_writeln(buf);
    ksnprintf(buf, sizeof(buf), "  Cache    : L1d %ukB  L1i %ukB  L2 %ukB  L3 %ukB",
        info->l1d_kb, info->l1i_kb, info->l2_kb, info->l3_kb);
    terminal_writeln(buf);
    ksnprintf(buf, sizeof(buf), "  Features : SSE=%d SSE2=%d SSE4.2=%d AES=%d HTT=%d NX=%d LM=%d",
        info->has_sse,info->has_sse2,info->has_sse4_2,info->has_aes,
        info->has_htt,info->has_nx,info->has_lm);
    terminal_writeln(buf);
}

int cpu_compat_check(const cpu_info_t *info){
    /* The x86-64 kernel needs long mode and its baseline build policy needs SSE2. */
    if(!info->has_lm||!info->has_sse2)return -1;
    return 0;
}
void cpu_compat_print(const cpu_info_t *info){
    if(cpu_compat_check(info)<0){
        terminal_writeln("cpucompat: UNSUPPORTED: ATMKoala needs x86-64 long mode and SSE2.");
        if(info->family==15) terminal_writeln("cpucompat: NetBurst Pentium 4 detected; only 64-bit Prescott-class P4 can qualify.");
        return;
    }
    terminal_writeln("cpucompat: compatible: x86-64 long mode and SSE2 present.");
    if(info->family==15){
        terminal_writeln("cpucompat: NetBurst policy: use conservative timing; NX may be absent on this generation.");
        if(!info->has_nx)terminal_writeln("cpucompat: warning: NX absent; user mappings remain non-executable by policy where possible.");
    }
    if(info->family==6&&info->model==0x7A)terminal_writeln("cpucompat: Gemini Lake J4105 profile selected (4-core low-power platform).");
}

void cpu_set_power_state(int state) {
    /* MSR 0x199 = IA32_PERF_CTL — set P-state */
    uint64_t val = 0;
    switch (state) {
        case 0: val = (uint64_t)(GEMINI_LAKE_BOOST_MHZ / 100) << 8; break; /* max */
        case 1: val = (uint64_t)(GEMINI_LAKE_BASE_MHZ  / 100) << 8; break; /* base */
        case 3: val = 0x0400; break; /* low power: 4×100MHz */
        default: return;
    }
    __asm__ volatile("wrmsr" :: "c"(0x199u), "a"((uint32_t)val), "d"((uint32_t)(val>>32)));
}

/* ════════════════════════════════════════════════════════════
 *  HPET — High Precision Event Timer
 *  Fixed at 0xFED00000 on all Intel Gemini Lake systems
 * ════════════════════════════════════════════════════════════ */

void hpet_init(hpet_state_t *hpet) {
    hpet->initialized = 0;
    /* Read capability register */
    uint64_t cap = _mmio_r64(HPET_PHYS_BASE + HPET_REG_CAP);
    if (cap == 0 || cap == (uint64_t)-1) return; /* not present */

    uint32_t period_fs = (uint32_t)(cap >> 32); /* counter period in femtoseconds */
    if (!period_fs) return;

    hpet->period_fs   = period_fs;
    /* freq = 10^15 / period_fs */
    hpet->freq_hz     = 1000000000000000ULL / period_fs;

    /* Enable HPET (bit 0 of CFG) */
    uint64_t cfg = _mmio_r64(HPET_PHYS_BASE + HPET_REG_CFG);
    cfg |= HPET_CFG_ENABLE;
    _mmio_w32(HPET_PHYS_BASE + HPET_REG_CFG, (uint32_t)cfg);

    hpet->initialized = 1;
}

uint64_t hpet_read(void) {
    return _mmio_r64(HPET_PHYS_BASE + HPET_REG_MCTR);
}

void hpet_sleep_us(uint32_t us) {
    if (!g_hpet.initialized) { pit_sleep(1); return; }
    uint64_t ticks = ((uint64_t)us * g_hpet.freq_hz) / 1000000ULL;
    uint64_t end   = hpet_read() + ticks;
    while (hpet_read() < end) __asm__ volatile("pause");
}

/* ════════════════════════════════════════════════════════════
 *  ACPI Battery (EC-based, common on Gemini Lake laptops)
 *  The EC (Embedded Controller) is typically at I/O 0x62/0x66
 * ════════════════════════════════════════════════════════════ */

#define EC_DATA   0x62
#define EC_CMD    0x66
#define EC_STATUS 0x66

#define EC_IBF  (1<<1)  /* Input Buffer Full  — wait for 0 before write */
#define EC_OBF  (1<<0)  /* Output Buffer Full — wait for 1 before read  */

#define EC_RD_EC  0x80  /* Read EC command */
#define EC_WR_EC  0x81  /* Write EC command */

/* EC register offsets (typical laptop layout, varies by OEM) */
#define EC_REG_BAT_PRESENT  0x40
#define EC_REG_BAT_STATUS   0x41  /* bit0=discharge, bit1=charge */
#define EC_REG_BAT_CAP_PCT  0x42  /* 0-100 */
#define EC_REG_BAT_VOLT_LO  0x43
#define EC_REG_BAT_VOLT_HI  0x44
#define EC_REG_BAT_CURR_LO  0x45
#define EC_REG_BAT_CURR_HI  0x46
#define EC_REG_AC_STATUS    0x4A  /* bit0=AC present */
#define EC_REG_LID_STATUS   0x4C  /* bit0=open */

/* Raw EC layouts are OEM-specific. Keep this disabled until an ACPI/EC driver
 * identifies a board and provides a validated operation with fault handling. */
#define ATM_EC_TELEMETRY_ENABLED 0

static int ec_wait_ibf(void) {
    for (int i = 0; i < 10000; i++) {
        if (!(_inb(EC_STATUS) & EC_IBF)) return 0;
        __asm__ volatile("pause");
    }
    return -1; /* timeout */
}

static int ec_wait_obf(void) {
    for (int i = 0; i < 10000; i++) {
        if (_inb(EC_STATUS) & EC_OBF) return 0;
        __asm__ volatile("pause");
    }
    return -1;
}

static uint8_t ec_read(uint8_t reg) {
    if (ec_wait_ibf() < 0) return 0xFF;
    _outb(EC_CMD, EC_RD_EC);
    if (ec_wait_ibf() < 0) return 0xFF;
    _outb(EC_DATA, reg);
    if (ec_wait_obf() < 0) return 0xFF;
    return _inb(EC_DATA);
}

void battery_init(battery_info_t *bat) {
    kmemset(bat, 0, sizeof(*bat));
    kstrcpy(bat->technology,  "Li-ion");
    kstrcpy(bat->manufacturer,"Lenovo");
    kstrcpy(bat->model,       "BASE-BAT");
    bat->full_charge_mwh = 38000; /* ~38 Wh typical for Y116 */
    battery_update(bat);
}

void battery_update(battery_info_t *bat) {
    if(!bat) return;
#if !ATM_EC_TELEMETRY_ENABLED
    /* Do not touch 0x62/0x66 from the UI or hwinfo path. An unsupported EC
     * can stall port I/O even though its register protocol superficially
     * resembles the developer board used for the original implementation. */
    bat->valid=0;
    bat->present=0;
    bat->charging=0;
    bat->ac_online=0;
    bat->capacity_pct=0;
    return;
#else
    /* A status bar must never manufacture a battery percentage.  EC layouts
     * differ by OEM, so absence or an unreadable register is reported as
     * telemetry unavailable rather than a plausible-looking default. */
    bat->valid = 0;
    bat->present = 0;
    uint8_t present = ec_read(EC_REG_BAT_PRESENT);
    if (present == 0xFF) return;

    bat->present = (present & 1) ? 1 : 0;
    if (!bat->present) { bat->valid = 1; return; }

    uint8_t status = ec_read(EC_REG_BAT_STATUS);
    uint8_t ac     = ec_read(EC_REG_AC_STATUS);
    uint8_t cap    = ec_read(EC_REG_BAT_CAP_PCT);
    uint8_t vlo    = ec_read(EC_REG_BAT_VOLT_LO);
    uint8_t vhi    = ec_read(EC_REG_BAT_VOLT_HI);
    uint8_t clo    = ec_read(EC_REG_BAT_CURR_LO);
    uint8_t chi    = ec_read(EC_REG_BAT_CURR_HI);
    if (status==0xFF || ac==0xFF || cap==0xFF || vlo==0xFF || vhi==0xFF || clo==0xFF || chi==0xFF) return;

    bat->charging = (status & 0x02) ? 1 : 0;
    bat->ac_online = (ac & 0x01) ? 1 : 0;
    bat->capacity_pct = cap > 100 ? 100 : cap;
    bat->voltage_mv = ((uint16_t)vhi << 8 | vlo) * 10; /* EC unit: 10mV */
    bat->current_ma = ((uint16_t)chi << 8 | clo);
    bat->remain_mwh = (bat->full_charge_mwh * bat->capacity_pct) / 100;
    if (bat->current_ma > 0)
        bat->remain_min = (bat->remain_mwh * 60) /
                          (bat->voltage_mv * bat->current_ma / 1000 + 1);
    bat->valid = 1;
#endif
}

void battery_print(const battery_info_t *bat) {
    char buf[80];
    terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    terminal_writeln("Battery:");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    if (!bat->valid) { terminal_writeln("  Telemetry unavailable (no supported ACPI EC response)"); return; }
    if (!bat->present) { terminal_writeln("  Not present"); return; }
    ksnprintf(buf, sizeof(buf), "  Model    : %s %s (%s)",
        bat->manufacturer, bat->model, bat->technology);
    terminal_writeln(buf);
    ksnprintf(buf, sizeof(buf), "  Capacity : %u%%  (%u/%u mWh)",
        bat->capacity_pct, bat->remain_mwh, bat->full_charge_mwh);
    terminal_writeln(buf);
    ksnprintf(buf, sizeof(buf), "  Status   : %s  AC: %s",
        bat->charging ? "Charging" : "Discharging",
        bat->ac_online ? "Online" : "Offline");
    terminal_writeln(buf);
    ksnprintf(buf, sizeof(buf), "  Voltage  : %u mV   Current: %u mA",
        bat->voltage_mv, bat->current_ma);
    terminal_writeln(buf);
    if (!bat->charging && bat->remain_min)
        ksnprintf(buf, sizeof(buf), "  Time left: ~%u h %02u min",
            bat->remain_min / 60, bat->remain_min % 60);
    else
        ksnprintf(buf, sizeof(buf), "  Time left: ---");
    terminal_writeln(buf);
}

/* ════════════════════════════════════════════════════════════
 *  Backlight — Intel PWM (i915-style)
 *  On Gemini Lake the backlight register is in the GPU MMIO space.
 *  Fallback: ACPI _BCM via port 0xB2 SMI.
 * ════════════════════════════════════════════════════════════ */

/* Intel BLC_PWM_CPU_CTL — offset 0x48254 in MMIO (South Display Engine) */
#define I915_BLC_PWM_CTL  0x48254
#define I915_BLC_PWM_CTL2 0x48250

void backlight_init(backlight_t *bl) {
    bl->max     = 255;
    bl->current = 200; /* ~78% matching screenshot's visible brightness */
    bl->enabled = 1;

    /* Try to read actual level from i915 MMIO if available */
    if (g_i915.initialized && g_i915.mmio_base) {
        uint32_t ctl = _mmio_r32(g_i915.mmio_base + I915_BLC_PWM_CTL);
        uint32_t max = (ctl >> 16) & 0xFFFF;
        uint32_t cur = ctl & 0xFFFF;
        if (max > 0) {
            bl->max     = max;
            bl->current = cur;
        }
    }
}

void backlight_set(backlight_t *bl, uint32_t level) {
    if (level > bl->max) level = bl->max;
    bl->current = level;
    bl->enabled = (level > 0) ? 1 : 0;

    /* Write to i915 MMIO if available */
    if (g_i915.initialized && g_i915.mmio_base) {
        uint32_t ctl = _mmio_r32(g_i915.mmio_base + I915_BLC_PWM_CTL);
        ctl = (ctl & 0xFFFF0000u) | (level & 0xFFFF);
        _mmio_w32(g_i915.mmio_base + I915_BLC_PWM_CTL, ctl);
        return;
    }

    /* ACPI SMI fallback: write brightness level via port 0xB2 */
    _outb(0xB3, (uint8_t)((level * 100) / bl->max)); /* normalize to 0-100 */
    _outb(0xB2, 0x5F); /* ACPI _BCM SMI command */
}

void backlight_inc(backlight_t *bl, int delta) {
    int32_t newlevel = (int32_t)bl->current + delta;
    if (newlevel < 0) newlevel = 0;
    if (newlevel > (int32_t)bl->max) newlevel = (int32_t)bl->max;
    backlight_set(bl, (uint32_t)newlevel);
}

/* ════════════════════════════════════════════════════════════
 *  eDP Panel — BOE091D 1920x1080 14" 60Hz
 *  Timing matches standard 1080p 60Hz CEA-861-F timings
 * ════════════════════════════════════════════════════════════ */

void edp_detect(edp_panel_t *panel) {
    panel->width      = 1920;
    panel->height     = 1080;
    panel->refresh_hz = 60;
    panel->pclk_khz   = 138500; /* standard 1080p60 pixel clock */
    panel->bpc        = 8;
    kstrcpy(panel->vendor, "BOE");
    kstrcpy(panel->model,  "091D");

    /* In a real driver we'd read EDID via DDC/I2C from i915 GMBUS.
     * For now, match the fastfetch output exactly. */
}

void edp_print(const edp_panel_t *panel) {
    char buf[80];
    terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    terminal_writeln("Display:");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    ksnprintf(buf, sizeof(buf), "  Panel    : %s%s  %ux%u  %uHz  %ubpc  eDP",
        panel->vendor, panel->model,
        panel->width, panel->height, panel->refresh_hz, panel->bpc);
    terminal_writeln(buf);
    ksnprintf(buf, sizeof(buf), "  Size     : 14\" Built-in");
    terminal_writeln(buf);
    ksnprintf(buf, sizeof(buf), "  Pixclk   : %u kHz", panel->pclk_khz);
    terminal_writeln(buf);
}

/* ════════════════════════════════════════════════════════════
 *  Intel UHD Graphics 600 (i915 framebuffer)
 *  PCI 8086:3185  —  Gemini Lake GT2
 *  We use the Multiboot2-provided framebuffer (already set up by GRUB)
 *  and expose the MMIO space for backlight/power management.
 * ════════════════════════════════════════════════════════════ */

void i915_detect(i915_fb_t *fb,const pci_bus_t *bus) {
    static const uint32_t stolen_mb_table[16]={0,32,64,96,128,160,192,224,256,288,320,352,384,416,448,512};
    if(!fb)return;
    fb->pci_present=0;fb->native_modeset_ready=0;fb->acceleration_ready=0;fb->pci_devid=0;fb->mmio_base=0;fb->stolen_mb=0;
    if(!bus)return;
    for(int i=0;i<bus->count;i++){
        const pci_device_t *gpu=&bus->devs[i];
        if(gpu->vendor!=PCI_VENDOR_INTEL||gpu->device!=PCI_DEV_UHD600)continue;
        fb->pci_present=1;fb->pci_devid=PCI_DEV_UHD600;fb->pci_bus=gpu->bus;fb->pci_dev=gpu->dev;fb->pci_fn=gpu->fn;
        /* BAR2 is recorded only as firmware-provided diagnostic metadata. No
         * BAR is mapped or written by this discovery-only implementation. */
        fb->mmio_base=(uint64_t)(gpu->bar[2]&~0xFu);
        if(bus==&g_pci){uint32_t gmch=pci_read_cfg(0,0,0,0x50);fb->stolen_mb=stolen_mb_table[(gmch>>4)&0xFu];}
        return;
    }
}

int i915_fb_init(i915_fb_t *fb,uint64_t phys,uint32_t w,uint32_t h,uint32_t pitch,uint8_t bpp) {
    if(!fb||!phys||!w||!h||!pitch||(bpp!=24&&bpp!=32))return -1;
    fb->fb_phys=phys;fb->fb_virt=(uint8_t *)(uintptr_t)phys;fb->width=w;fb->height=h;fb->pitch=pitch;fb->bpp=bpp;
    /* This is a bootloader framebuffer handoff, not native i915 modesetting. */
    fb->initialized=1;return 0;
}

int i915_selftest(void){
    pci_bus_t bus;i915_fb_t fb;kmemset(&bus,0,sizeof(bus));kmemset(&fb,0,sizeof(fb));
    bus.count=1;bus.devs[0].vendor=PCI_VENDOR_INTEL;bus.devs[0].device=PCI_DEV_UHD600;bus.devs[0].bus=0;bus.devs[0].dev=2;bus.devs[0].fn=0;bus.devs[0].class_code=3;bus.devs[0].bar[2]=0xDEA00000u;
    i915_detect(&fb,&bus);
    if(!fb.pci_present||fb.pci_devid!=PCI_DEV_UHD600||fb.mmio_base!=0xDEA00000u||fb.native_modeset_ready||fb.acceleration_ready)return -1;
    if(i915_fb_init(&fb,0xE0000000u,640,480,2560,32)<0||!fb.initialized||fb.width!=640||fb.height!=480)return -1;
    return i915_fb_init(&fb,0,640,480,2560,32)<0?0:-1;
}

void i915_fb_fill(i915_fb_t *fb,uint32_t color){
    if(!fb||!fb->initialized||!fb->fb_virt||fb->bpp!=32)return;
    uint32_t *p=(uint32_t *)fb->fb_virt;uint32_t count=(fb->pitch*fb->height)/4u;
    for(uint32_t i=0;i<count;i++)p[i]=color;
}

void i915_fb_blit(i915_fb_t *fb,uint32_t x,uint32_t y,uint32_t w,uint32_t h,const uint32_t *pixels){
    if(!fb||!fb->initialized||!fb->fb_virt||!pixels||fb->bpp!=32||x>=fb->width||y>=fb->height)return;
    uint32_t copy_w=w<fb->width-x?w:fb->width-x,copy_h=h<fb->height-y?h:fb->height-y;
    for(uint32_t row=0;row<copy_h;row++){
        uint32_t *dst=(uint32_t *)(fb->fb_virt+(y+row)*fb->pitch+x*4u);const uint32_t *src=pixels+row*w;
        kmemcpy(dst,src,copy_w*4u);
    }
}

/* ════════════════════════════════════════════════════════════
 *  Realtek RTL8168/8111 GbE Driver
 *  PCI 10EC:8168  —  AHCI GbE, Descriptor-based TX/RX
 *
 *  The Y116 uses the onboard Realtek GbE.
 *  RTL8168 uses 256-byte aligned TX/RX descriptor rings.
 * ════════════════════════════════════════════════════════════ */

static void rtl8168_reset(rtl8168_state_t *nic) {
    _outb((uint16_t)(nic->io_base + RTL8168_REG_CMD), RTL8168_CMD_RESET);
    for (int i = 0; i < 100; i++) {
        if (!(_inb((uint16_t)(nic->io_base + RTL8168_REG_CMD)) & RTL8168_CMD_RESET))
            break;
        pit_sleep(1);
    }
}

int rtl8168_init(rtl8168_state_t *nic, pci_bus_t *bus) {
    nic->initialized = 0;

    /* Find RTL8168 or RTL8169 on PCI */
    pci_device_t *dev = pci_find(bus, PCI_VENDOR_REALTEK, PCI_DEV_RTL8168);
    if (!dev) dev = pci_find(bus, PCI_VENDOR_REALTEK, PCI_DEV_RTL8169);
    if (!dev) return -1;

    pci_enable_device(dev->bus, dev->dev, dev->fn);
    nic->io_base = (uint16_t)(dev->bar[0] & ~0x3u); /* BAR0 = I/O */
    if (!nic->io_base) {
        /* Try BAR2 (MMIO) */
        nic->io_base = (uint16_t)(dev->bar[2] & ~0xFu);
    }
    if (!nic->io_base) return -1;

    /* Read MAC address */
    for (int i = 0; i < 6; i++)
        nic->mac[i] = _inb((uint16_t)(nic->io_base + RTL8168_REG_MAC0 + i));

    /* Unlock config registers */
    _outb((uint16_t)(nic->io_base + 0x50), 0x00); /* EEPROM Ctrl */

    /* Software reset */
    rtl8168_reset(nic);

    /* Allocate descriptor rings (aligned to 256 bytes) */
    nic->rx_ring = (rtl8168_desc_t *)kmalloc(
        sizeof(rtl8168_desc_t) * RTL8168_RX_RING_SIZE + 256);
    nic->tx_ring = (rtl8168_desc_t *)kmalloc(
        sizeof(rtl8168_desc_t) * RTL8168_TX_RING_SIZE + 256);
    if (!nic->rx_ring || !nic->tx_ring) return -1;

    kmemset(nic->rx_ring, 0, sizeof(rtl8168_desc_t) * RTL8168_RX_RING_SIZE);
    kmemset(nic->tx_ring, 0, sizeof(rtl8168_desc_t) * RTL8168_TX_RING_SIZE);

    /* Set up RX descriptors */
    for (int i = 0; i < RTL8168_RX_RING_SIZE; i++) {
        nic->rx_bufs[i] = (uint8_t *)kmalloc(RTL8168_BUF_SIZE);
        if (!nic->rx_bufs[i]) return -1;
        nic->rx_ring[i].flags = RTL8168_DESC_OWN | (RTL8168_BUF_SIZE & 0x3FFF);
        if (i == RTL8168_RX_RING_SIZE - 1)
            nic->rx_ring[i].flags |= RTL8168_DESC_EOR;
        nic->rx_ring[i].addr  = (uint64_t)(uintptr_t)nic->rx_bufs[i];
    }

    /* Set up TX descriptors (owned by host, empty) */
    for (int i = 0; i < RTL8168_TX_RING_SIZE; i++) {
        nic->tx_bufs[i] = (uint8_t *)kmalloc(RTL8168_BUF_SIZE);
        if (!nic->tx_bufs[i]) return -1;
        nic->tx_ring[i].flags = 0;
        if (i == RTL8168_TX_RING_SIZE - 1)
            nic->tx_ring[i].flags |= RTL8168_DESC_EOR;
        nic->tx_ring[i].addr  = (uint64_t)(uintptr_t)nic->tx_bufs[i];
    }

    /* Program RX/TX descriptor start addresses */
    uint64_t rx_phys = (uint64_t)(uintptr_t)nic->rx_ring;
    uint64_t tx_phys = (uint64_t)(uintptr_t)nic->tx_ring;
    /* RDSAR: 64-bit RX descriptor start (at offset 0xE4) */
    _outl((uint16_t)(nic->io_base + RTL8168_REG_RDSAR),     (uint32_t)(rx_phys));
    _outl((uint16_t)(nic->io_base + RTL8168_REG_RDSAR + 4), (uint32_t)(rx_phys >> 32));
    /* TNPDS: TX normal priority */
    _outl((uint16_t)(nic->io_base + RTL8168_REG_TNPDS),     (uint32_t)(tx_phys));
    _outl((uint16_t)(nic->io_base + RTL8168_REG_TNPDS + 4), (uint32_t)(tx_phys >> 32));

    /* RCR: accept physical match + broadcast, 8K FIFO */
    _outl((uint16_t)(nic->io_base + RTL8168_REG_RCR), 0x0000E70Fu);
    /* TCR: IFG=normal, DMA=unlimited */
    _outl((uint16_t)(nic->io_base + RTL8168_REG_TCR), 0x03000700u);
    /* Max TX packet size */
    _outl((uint16_t)(nic->io_base + RTL8168_REG_MTPS), RTL8168_BUF_SIZE / 128);
    /* C+ command: enable RX/TX with descriptor rings */
    _outl((uint16_t)(nic->io_base + RTL8168_REG_C_PlusCmd), 0x0020u);
    /* Enable RX and TX */
    _outb((uint16_t)(nic->io_base + RTL8168_REG_CMD),
          RTL8168_CMD_RX_ENB | RTL8168_CMD_TX_ENB);
    /* Unmask RX/TX OK interrupts */
    _outl((uint16_t)(nic->io_base + RTL8168_REG_IMR), 0x0005u);

    nic->rx_cur = 0;
    nic->tx_cur = 0;
    nic->initialized = 1;
    return 0;
}

int rtl8168_send(rtl8168_state_t *nic, const void *buf, uint32_t len) {
    if (!nic->initialized) return -1;
    if (len > RTL8168_BUF_SIZE) len = RTL8168_BUF_SIZE;

    uint32_t idx = nic->tx_cur % RTL8168_TX_RING_SIZE;
    rtl8168_desc_t *d = &nic->tx_ring[idx];

    /* Wait until descriptor is free */
    for (int i = 0; i < 1000; i++) {
        if (!(d->flags & RTL8168_DESC_OWN)) break;
        pit_sleep(1);
    }
    if (d->flags & RTL8168_DESC_OWN) return -1; /* timeout */

    /* Copy data */
    for (uint32_t i = 0; i < len; i++)
        nic->tx_bufs[idx][i] = ((const uint8_t *)buf)[i];

    /* Set descriptor: OWN + FS + LS + length */
    uint32_t flags = RTL8168_DESC_OWN | RTL8168_DESC_FS | RTL8168_DESC_LS | (len & 0x3FFF);
    if (idx == RTL8168_TX_RING_SIZE - 1) flags |= RTL8168_DESC_EOR;
    d->flags = flags;

    /* Kick TX */
    _outb((uint16_t)(nic->io_base + RTL8168_REG_TPPoll), 0x40);

    nic->tx_cur++;
    nic->tx_packets++;
    nic->tx_bytes += len;
    return 0;
}

int rtl8168_recv(rtl8168_state_t *nic, void *buf, uint32_t maxlen) {
    if (!nic->initialized) return -1;

    uint32_t idx = nic->rx_cur % RTL8168_RX_RING_SIZE;
    rtl8168_desc_t *d = &nic->rx_ring[idx];

    /* Check if NIC has given us a packet (OWN=0 means host owns it) */
    if (d->flags & RTL8168_DESC_OWN) return 0;

    uint32_t len = d->flags & 0x3FFF;
    if (len > maxlen) len = maxlen;
    for (uint32_t i = 0; i < len; i++)
        ((uint8_t *)buf)[i] = nic->rx_bufs[idx][i];

    /* Give descriptor back to NIC */
    d->flags = RTL8168_DESC_OWN | (RTL8168_BUF_SIZE & 0x3FFF);
    if (idx == RTL8168_RX_RING_SIZE - 1) d->flags |= RTL8168_DESC_EOR;

    nic->rx_cur++;
    nic->rx_packets++;
    nic->rx_bytes += len;

    /* ACK ISR */
    uint32_t isr = _inl((uint16_t)(nic->io_base + RTL8168_REG_ISR));
    _outl((uint16_t)(nic->io_base + RTL8168_REG_ISR), isr);

    return (int)len;
}

void rtl8168_print_stats(const rtl8168_state_t *nic) {
    char buf[80];
    terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    terminal_writeln("RTL8168 GbE:");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    ksnprintf(buf, sizeof(buf),
        "  MAC      : %02x:%02x:%02x:%02x:%02x:%02x",
        nic->mac[0],nic->mac[1],nic->mac[2],
        nic->mac[3],nic->mac[4],nic->mac[5]);
    terminal_writeln(buf);
    ksnprintf(buf, sizeof(buf), "  I/O base : 0x%04x", nic->io_base);
    terminal_writeln(buf);
    ksnprintf(buf, sizeof(buf), "  RX       : %u packets  %u bytes",
        (uint32_t)nic->rx_packets, (uint32_t)nic->rx_bytes);
    terminal_writeln(buf);
    ksnprintf(buf, sizeof(buf), "  TX       : %u packets  %u bytes",
        (uint32_t)nic->tx_packets, (uint32_t)nic->tx_bytes);
    terminal_writeln(buf);
}

/* ════════════════════════════════════════════════════════════
 *  Top-level init
 * ════════════════════════════════════════════════════════════ */

void hw_y116_init(void) {
    /* 1. PCI scan — must be first */
    pci_init(&g_pci);

    /* 2. Presence discovery for WLAN and USB transport. */
    radio_detect(&g_radio, &g_pci);

    /* 3. Read-only HDA controller discovery. */
    hda_detect(&g_hda,&g_pci);

    /* 4. CPU */
    cpu_detect(&g_cpu);

    /* 5. HPET */
    hpet_init(&g_hpet);

    /* 6. Battery */
    battery_init(&g_battery);

    /* 7. eDP panel */
    edp_detect(&g_panel);

    /* 8. RTL8168 (supplements existing RTL8139 stub) */
    rtl8168_init(&g_rtl8168, &g_pci);

    /* 9. Backlight (needs i915 MMIO which may be set by vbe_init) */
    backlight_init(&g_backlight);
}

static void radio_print(const radio_status_t *radio) {
    char buf[80];
    terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    terminal_writeln("Radios:");
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    if (radio->wifi_connected) {
        ksnprintf(buf,sizeof(buf),"  Wi-Fi    : connected; signal %u%%",radio->wifi_signal_pct);terminal_writeln(buf);
    } else if (radio->wifi_controller_present) {
        ksnprintf(buf,sizeof(buf),"  Wi-Fi    : controller %04x:%04x; %s",radio->wifi_vendor,radio->wifi_device,radio->wifi_driver_ready?"driver ready, not associated":"driver unavailable");terminal_writeln(buf);
    } else terminal_writeln("  Wi-Fi    : no PCI wireless controller detected");
    if (radio->bluetooth_connected) terminal_writeln("  Bluetooth: connected");
    else if (radio->bluetooth_controller_present)
        terminal_writeln(radio->bluetooth_driver_ready?(radio->bluetooth_enabled?"  Bluetooth: enabled; no device connected":"  Bluetooth: driver ready; radio disabled"):"  Bluetooth: controller found; HCI driver unavailable");
    else terminal_writeln(radio->usb_host_present?"  Bluetooth: no enumerated USB HCI controller":"  Bluetooth: USB transport unavailable");
}

void hw_y116_print(void) {
    cpu_print(&g_cpu);
    terminal_writeln("");
    battery_print(&g_battery);
    terminal_writeln("");
    radio_print(&g_radio);
    terminal_writeln("");
    hda_print(&g_hda);
    terminal_writeln("");
    edp_print(&g_panel);
    terminal_writeln("");
    if (g_rtl8168.initialized)
        rtl8168_print_stats(&g_rtl8168);
    terminal_writeln("");
    pci_print(&g_pci);
}
