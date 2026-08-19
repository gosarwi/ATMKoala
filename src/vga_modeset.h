#ifndef VGA_MODESET_H
#define VGA_MODESET_H
/*
 * vga_modeset.h — VGA CRTC/Sequencer/Graphics/Attribute controller
 * mode-setting for atmkoala.
 *
 * PROVENANCE: the timing-to-register conversion algorithm in
 * vga_modeset.c (vga_compute_regs()) is adapted from NetBSD's
 *   sys/dev/ic/vga_raster.c   (vga_setup_regs() / vga_set_mode())
 *   sys/dev/ic/vgareg.h       (register port offsets)
 *   sys/dev/ic/mc6845reg.h    (CRTC register count)
 * Original NetBSD authors: Bang Jun-Young, Julio M. Merino Vidal,
 * Matthias Drochner, and Carnegie Mellon University (Chris G.
 * Demetriou) — see the license block at the top of vga_modeset.c
 * for the exact, unmodified copyright notices these contributions
 * require to be retained (3-Clause BSD / CMU permissive license,
 * both non-copyleft). This is NOT a verbatim copy of any of those
 * files — the WSCONS device-attach glue, autoconf integration, and
 * raster-console text rendering have all been left out; only the
 * register-value arithmetic itself (the genuinely hardware-specific,
 * non-trivial part) was ported, then adapted to call into our own
 * bus_space-shim (bsd_compat.h) instead of NetBSD's bus_space(9) and
 * device tree.
 *
 * This is what gives atmkoala auto-resolution: instead of a single
 * hardcoded 800x600 VBE mode requested at boot, the kernel can now
 * compute correct CRTC timings for an arbitrary text-mode resolution
 * supplied by EDID or the user, the same way NetBSD's vga(4) driver
 * does for its raster console.
 */

#include "bsd_compat.h"
#include <stdint.h>

/* Register block sizes — straight from mc6845reg.h / vgareg.h,
 * these are fixed VGA hardware constants, not implementation
 * choices, so they're unlikely to ever need to change. */
#define VGA_CRTC_NREGS   25
#define VGA_ATC_NREGS    21
#define VGA_TS_NREGS      5
#define VGA_GDC_NREGS     9

/* Port offsets, relative to the VGA I/O base (0x3C0 for color mode) */
#define VGA_MISC_DATAW   0x12   /* 0x3C2 = 0x3C0 + 0x12 misc output write  */
#define VGA_MISC_DATAR   0x1C   /* 0x3CC = 0x3C0 + 0x1C misc output read   */
#define VGA_TS_INDEX     0x04   /* 0x3C4 sequencer index   */
#define VGA_TS_DATA      0x05   /* 0x3C5 sequencer data    */
#define VGA_GDC_INDEX    0x0E   /* 0x3CE graphics index    */
#define VGA_GDC_DATA     0x0F   /* 0x3CF graphics data     */
#define VGA_ATC_INDEX    0x00   /* 0x3C0 attribute index   */
#define VGA_ATC_DATAW    0x00   /* 0x3C0 attribute data (same port, toggles) */
#define VGA_CRTC_INDEX   0x14   /* 0x3D4 = 0x3C0+0x14, CRTC index (color)  */
#define VGA_CRTC_DATA    0x15   /* 0x3D5 CRTC data (color mode)            */

#define VGA_TS_MODE_BLANK 0x20  /* sequencer clocking-mode "screen off" bit */

/* Sync polarity flags, matching NetBSD's <dev/videomode/videomode.h>
 * VID_* flag names so a ported mode table reads identically. */
#define VID_PHSYNC   0x0001
#define VID_NHSYNC   0x0002
#define VID_PVSYNC   0x0004
#define VID_NVSYNC   0x0008
#define VID_DBLSCAN  0x0010
#define VID_CLKDIV2  0x0020

/* A requested display timing, in the same field order/units as
 * NetBSD's struct videomode (all values in pixels/lines, dot_clock
 * in kHz) — this lets a mode line ported from elsewhere (e.g. an
 * EDID-derived detailed timing, or a VESA standard mode table) be
 * dropped in with field names intact. */
typedef struct {
    uint32_t dot_clock;     /* pixel clock, kHz */
    uint32_t hdisplay, hsync_start, hsync_end, htotal;
    uint32_t vdisplay, vsync_start, vsync_end, vtotal;
    uint32_t flags;         /* VID_* */
} vga_videomode_t;

/* Computed hardware register values for one mode */
typedef struct {
    uint8_t miscout;
    uint8_t crtc[VGA_CRTC_NREGS];
    uint8_t atc[VGA_ATC_NREGS];
    uint8_t ts[VGA_TS_NREGS];
    uint8_t gdc[VGA_GDC_NREGS];
} vga_moderegs_t;

/* Compute register values for `mode` into `regs`. Pure computation,
 * touches no hardware — safe to call to validate a mode before
 * committing to it. */
void vga_compute_regs(const vga_videomode_t *mode, vga_moderegs_t *regs);

/* Program the VGA hardware with already-computed `regs`, via the
 * given I/O-port bus_space handle (use bus_space_map() with
 * BUS_SPACE_IO and base 0x3C0 to build it). Blanks the display
 * during the register writes and unblanks at the end, exactly like
 * the NetBSD sequence this is adapted from. */
void vga_apply_regs(const bus_space_handle_t *io, const vga_moderegs_t *regs);

/* Convenience: compute + apply in one call, using port 0x3C0 as the
 * VGA I/O base (the standard ISA/PCI VGA color-mode base). */
void vga_set_mode(const vga_videomode_t *mode);

/* A small built-in table of common modes, expressed as
 * vga_videomode_t, for use when no EDID is available (mirrors the
 * style — not the literal numbers — of NetBSD's
 * vga_console_modes[] fallback table). Timings below are the
 * well-known industry-standard VESA values for these resolutions
 * (VESA is a vendor-neutral standards body; these numbers are not
 * proprietary to NetBSD or any single project). */
extern const vga_videomode_t vga_mode_640x480_60;
extern const vga_videomode_t vga_mode_800x600_60;
extern const vga_videomode_t vga_mode_1024x768_60;

#endif /* VGA_MODESET_H */
