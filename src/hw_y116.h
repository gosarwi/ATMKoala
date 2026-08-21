#ifndef HW_Y116_H
#define HW_Y116_H
/*
 * hw_y116.h — Hardware drivers for Lenovo Y116 / Intel Gemini Lake
 *
 * Targeted hardware (from fastfetch):
 *   CPU  : Intel Celeron J4105  — Gemini Lake, 4 cores, 2.50 GHz
 *   GPU  : Intel UHD Graphics 600 (GeminiLake GT2, PCI 8086:3185)
 *   RAM  : 5.62 GiB LPDDR4
 *   Disk : SATA/NVMe (468 GiB)
 *   Net  : Realtek 8168/8111 GbE  (PCI 10EC:8168) — onboard
 *   Panel: BOE091D 1920x1080 14" 60Hz eDP
 *   ACPI : battery BASE-BAT, lid, AC adapter
 *   Timer: HPET + TSC
 *
 * Drivers in this file:
 *   pci       — full PCI config space scan (bus 0-255, dev 0-31, fn 0-7)
 *   cpu_j4105 — CPUID info, frequency, core count, feature flags
 *   msr       — Model Specific Registers (TSC, APIC base, power)
 *   hpet      — High Precision Event Timer (100ns resolution)
 *   acpi_bat  — ACPI battery gauge (EC registers / MMIO)
 *   acpi_lid  — Lid open/close state
 *   backlight — Intel i915-style backlight via MMIO or ACPI _BCM
 *   rtl8168   — Realtek RTL8168/8111 GbE driver (replaces RTL8139 stub)
 *   i915_stub — Intel UHD 600 framebuffer driver (uses Multiboot2 FB)
 *   edp       — eDP panel timing (BOE091D 1920x1080 60Hz)
 */

#include <stdint.h>
#include <stddef.h>

/* ── PCI ─────────────────────────────────────────────────── */
#define PCI_VENDOR_INTEL  0x8086
#define PCI_VENDOR_REALTEK 0x10EC

/* Intel Gemini Lake devices */
#define PCI_DEV_J4105_HOST   0x31F0  /* Host Bridge */
#define PCI_DEV_UHD600       0x3185  /* Intel UHD Graphics 600 */
#define PCI_DEV_GEMINI_SMB   0x31D4  /* SMBus */
#define PCI_DEV_GEMINI_UART  0x31BC  /* UART */
#define PCI_DEV_GEMINI_USB3  0x31A8  /* xHCI USB3 */
#define PCI_DEV_GEMINI_SATA  0x31E3  /* AHCI SATA */
#define PCI_DEV_GEMINI_PCIE  0x31D8  /* PCIe Root Port */
#define PCI_DEV_GEMINI_LPC   0x31E8  /* LPC/eSPI */
#define PCI_DEV_GEMINI_HDA   0x3198  /* Intel HD Audio */

/* Realtek */
#define PCI_DEV_RTL8168      0x8168
#define PCI_DEV_RTL8111      0x8111
#define PCI_DEV_RTL8169      0x8169

typedef struct {
    uint16_t vendor;
    uint16_t device;
    uint8_t  bus, dev, fn;
    uint8_t  class_code;   /* PCI class */
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  revision;
    uint32_t bar[6];       /* Base Address Registers */
    uint8_t  irq;
    uint8_t  pin;
    int      valid;
} pci_device_t;

#define PCI_MAX_DEVICES 64
typedef struct {
    pci_device_t devs[PCI_MAX_DEVICES];
    int          count;
} pci_bus_t;

/* ── CPU J4105 ────────────────────────────────────────────── */
#define GEMINI_LAKE_CORES    4
#define GEMINI_LAKE_BASE_MHZ 1500
#define GEMINI_LAKE_BOOST_MHZ 2500

typedef struct {
    char     brand[64];
    char     vendor[13];
    uint32_t family, model, stepping;
    uint32_t cores, threads;
    uint32_t base_mhz, boost_mhz;
    uint64_t tsc_freq_hz;
    /* feature flags */
    int has_sse, has_sse2, has_sse4_2, has_avx2, has_aes, has_rdrand, has_htt;
    int has_nx, has_lm;   /* 64-bit, NX */
    /* L-caches */
    uint32_t l1d_kb, l1i_kb, l2_kb, l3_kb;
    /* power */
    uint32_t tdp_w;
    char     codename[32];
} cpu_info_t;

/* ── Battery (ACPI) ──────────────────────────────────────── */
typedef struct {
    int      present;
    int      valid;         /* EC/ACPI telemetry was read successfully */
    int      charging;      /* 1=charging, 0=discharging */
    int      ac_online;
    uint32_t capacity_pct;  /* 0-100 */
    uint32_t voltage_mv;
    uint32_t current_ma;
    uint32_t full_charge_mwh;
    uint32_t remain_mwh;
    uint32_t remain_min;    /* estimated time left */
    char     technology[16]; /* Li-ion */
    char     manufacturer[32];
    char     model[32];
} battery_info_t;

/* ── Backlight ────────────────────────────────────────────── */
typedef struct {
    uint32_t current;    /* 0-max */
    uint32_t max;
    int      enabled;
} backlight_t;

/* Radio presence is split from link state. A PCI wireless controller can be
 * observed without claiming that a Wi-Fi driver, association or Bluetooth HCI
 * exists. Bluetooth commonly enumerates below USB and remains unavailable
 * until an actual USB/HCI driver reports it. */
typedef struct {
    int      wifi_controller_present;
    int      wifi_driver_ready;
    int      wifi_connected; /* true only after an association-capable driver reports link */
    uint8_t  wifi_signal_pct; /* 0 unless a real Wi-Fi driver supplies RSSI */
    uint16_t wifi_vendor, wifi_device;
    int      usb_host_present;
    int      bluetooth_controller_present;
    int      bluetooth_driver_ready;
    int      bluetooth_enabled;
    int      bluetooth_connected;
} radio_status_t;

/* ── eDP Panel ────────────────────────────────────────────── */
typedef struct {
    uint32_t width, height;
    uint32_t refresh_hz;
    uint32_t pclk_khz;    /* pixel clock */
    uint8_t  bpc;         /* bits per component */
    char     vendor[16];
    char     model[16];
} edp_panel_t;

/* ── RTL8168 NIC ─────────────────────────────────────────── */
#define RTL8168_REG_MAC0      0x00
#define RTL8168_REG_MAR0      0x08
#define RTL8168_REG_DTCCR     0x10   /* Dump Tally Counter Command */
#define RTL8168_REG_TNPDS     0x20   /* TX Normal Priority Desc Start */
#define RTL8168_REG_THPDS     0x28   /* TX High Priority Desc Start */
#define RTL8168_REG_CMD       0x37
#define RTL8168_REG_TPPoll    0x38
#define RTL8168_REG_IMR       0x3C
#define RTL8168_REG_ISR       0x3E
#define RTL8168_REG_TCR       0x40
#define RTL8168_REG_RCR       0x44
#define RTL8168_REG_RDSAR     0xE4   /* RX Desc Start Address */
#define RTL8168_REG_MTPS      0xEC   /* Max TX Packet Size */
#define RTL8168_REG_C_PlusCmd 0xE0

#define RTL8168_CMD_RESET     0x10
#define RTL8168_CMD_RX_ENB    0x08
#define RTL8168_CMD_TX_ENB    0x04

typedef struct {
    uint32_t flags;    /* descriptor flags */
    uint32_t vlan;
    uint64_t addr;     /* physical buffer address */
} rtl8168_desc_t;

#define RTL8168_DESC_OWN   (1u << 31)
#define RTL8168_DESC_EOR   (1u << 30)  /* End Of Ring */
#define RTL8168_DESC_FS    (1u << 29)  /* First Segment */
#define RTL8168_DESC_LS    (1u << 28)  /* Last Segment */

#define RTL8168_RX_RING_SIZE  64
#define RTL8168_TX_RING_SIZE  64
#define RTL8168_BUF_SIZE     2048

typedef struct {
    uint16_t io_base;
    uint8_t  mac[6];
    rtl8168_desc_t *rx_ring;
    rtl8168_desc_t *tx_ring;
    uint8_t  *rx_bufs[RTL8168_RX_RING_SIZE];
    uint8_t  *tx_bufs[RTL8168_TX_RING_SIZE];
    uint32_t rx_cur, tx_cur;
    int      initialized;
    uint64_t rx_packets, tx_packets;
    uint64_t rx_bytes, tx_bytes;
} rtl8168_state_t;

/* ── Intel HD Audio discovery ────────────────────────────── */
typedef struct {
    int      controller_present;
    int      pcm_output_ready; /* true only after future codec + DMA setup */
    uint16_t vendor, device;
    uint64_t mmio_base;
    uint16_t gcap;
    uint16_t codec_mask; /* STATESTS hardware-reported codec addresses */
} hda_state_t;

/* ── Intel UHD 600 framebuffer ───────────────────────────── */
typedef struct {
    uint32_t width, height, pitch;
    uint8_t  bpp;
    uint64_t fb_phys;     /* physical base supplied by bootloader */
    uint8_t *fb_virt;     /* inherited boot framebuffer mapping */
    int      initialized; /* valid inherited framebuffer handoff */
    int      pci_present; /* exact PCI detection; no modeset ownership */
    int      native_modeset_ready; /* remains 0: no display-pipe/GTT driver */
    int      acceleration_ready;   /* remains 0: no GEM/3D driver */
    uint8_t  pci_bus,pci_dev,pci_fn;
    /* Gemini Lake GT2 specific */
    uint32_t pci_devid;   /* 0x3185 */
    uint64_t mmio_base;   /* observed BAR only; not programmed by discovery */
    uint32_t stolen_mb;   /* host-bridge aperture report */
} i915_fb_t;

/* ── HPET ────────────────────────────────────────────────── */
#define HPET_PHYS_BASE    0xFED00000UL
#define HPET_REG_CAP      0x000   /* General capabilities */
#define HPET_REG_CFG      0x010   /* General configuration */
#define HPET_REG_ISR      0x020   /* Interrupt status */
#define HPET_REG_MCTR     0x0F0   /* Main counter */
#define HPET_CFG_ENABLE   (1u << 0)
#define HPET_CFG_LEGACY   (1u << 1)

typedef struct {
    uint64_t  freq_hz;    /* counter frequency */
    uint32_t  period_fs;  /* period in femtoseconds */
    int       initialized;
} hpet_state_t;

/* ── Public API ──────────────────────────────────────────── */

/* PCI */
void         pci_init(pci_bus_t *bus);
pci_device_t *pci_find(pci_bus_t *bus, uint16_t vendor, uint16_t device);
void         pci_print(pci_bus_t *bus);
uint32_t     pci_read_cfg(uint8_t b, uint8_t d, uint8_t f, uint8_t reg);
void         pci_write_cfg(uint8_t b, uint8_t d, uint8_t f, uint8_t reg, uint32_t val);

/* CPU */
void         cpu_detect(cpu_info_t *info);
void         cpu_print(const cpu_info_t *info);
int          cpu_compat_check(const cpu_info_t *info);
void         cpu_compat_print(const cpu_info_t *info);
uint64_t     cpu_tsc_freq(void);
void         cpu_set_power_state(int state); /* 0=max perf, 3=powersave */

/* Battery */
void         battery_init(battery_info_t *bat);
void         battery_update(battery_info_t *bat);
void         battery_print(const battery_info_t *bat);
void         radio_detect(radio_status_t *radio, const pci_bus_t *bus);
int          hardware_status_selftest(void);

/* Intel HD Audio read-only controller/codec discovery. */
void         hda_detect(hda_state_t *audio,const pci_bus_t *bus);
void         hda_print(const hda_state_t *audio);
int          hda_selftest(void);

/* Backlight */
void         backlight_init(backlight_t *bl);
void         backlight_set(backlight_t *bl, uint32_t level);
void         backlight_inc(backlight_t *bl, int delta);

/* eDP */
void         edp_detect(edp_panel_t *panel);
void         edp_print(const edp_panel_t *panel);

/* RTL8168 */
int          rtl8168_init(rtl8168_state_t *nic, pci_bus_t *bus);
int          rtl8168_send(rtl8168_state_t *nic, const void *buf, uint32_t len);
int          rtl8168_recv(rtl8168_state_t *nic, void *buf, uint32_t maxlen);
void         rtl8168_print_stats(const rtl8168_state_t *nic);

/* HPET */
void         hpet_init(hpet_state_t *hpet);
uint64_t     hpet_read(void);
void         hpet_sleep_us(uint32_t us);

/* i915 framebuffer */
void         i915_detect(i915_fb_t *fb,const pci_bus_t *bus);
int          i915_fb_init(i915_fb_t *fb, uint64_t phys, uint32_t w,
                           uint32_t h, uint32_t pitch, uint8_t bpp);
int          i915_selftest(void);
void         i915_fb_fill(i915_fb_t *fb, uint32_t color);
void         i915_fb_blit(i915_fb_t *fb, uint32_t x, uint32_t y,
                           uint32_t w, uint32_t h, const uint32_t *pixels);

/* Top-level: detect all Y116 hardware */
void         hw_y116_init(void);
void         hw_y116_print(void);

/* Global state (accessible from kernel.c) */
extern cpu_info_t    g_cpu;
extern battery_info_t g_battery;
extern backlight_t   g_backlight;
extern radio_status_t  g_radio;
extern edp_panel_t     g_panel;

extern pci_bus_t     g_pci;
extern rtl8168_state_t g_rtl8168;
extern hpet_state_t  g_hpet;
extern hda_state_t   g_hda;
extern i915_fb_t     g_i915;

#endif /* HW_Y116_H */
