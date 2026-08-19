/*
 * vga_modeset.c — VGA CRTC/Sequencer/Graphics/Attribute mode-setting
 * for atmkoala.
 *
 * The register-computation algorithm in vga_compute_regs() below is
 * adapted from vga_setup_regs() in NetBSD's sys/dev/ic/vga_raster.c,
 * and the register write sequence in vga_apply_regs() is adapted
 * from that file's vga_set_mode(). Per the license terms under which
 * that code is distributed, the original copyright notices are
 * retained verbatim here even though this is a reimplementation
 * against our own bus_space shim rather than a literal copy of the
 * NetBSD source tree (WSCONS glue, autoconf attachment, and the
 * raster-console text layer are NOT included — only the hardware
 * register arithmetic was ported):
 *
 * ---------------------------------------------------------------
 * Portions adapted from NetBSD sys/dev/ic/vga_raster.c:
 *
 * Copyright (c) 2001, 2002 Bang Jun-Young
 * Copyright (c) 2004 Julio M. Merino Vidal
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Also incorporates structure derived from:
 * Copyright (c) 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 * Author: Chris G. Demetriou
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION. CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Register port offsets adapted from NetBSD sys/dev/ic/vgareg.h and
 * sys/dev/ic/mc6845reg.h:
 * Copyright (c) 1998 Matthias Drochner. All rights reserved.
 * (same 3-Clause BSD terms as above, conditions 1-2 only, no clause 3)
 * ---------------------------------------------------------------
 *
 * Everything below this point — the atmkoala integration, the
 * bus_space-shim call shape, the built-in VESA-standard mode table,
 * and all original-to-this-project code — is new for atmkoala.
 */

#include "vga_modeset.h"
#include "util.h"

/* ── vga_compute_regs ──────────────────────────────────────────
 * Direct adaptation of vga_setup_regs() from vga_raster.c. Depth is
 * fixed at 4bpp/16-color planar mode here (matching the original
 * function's hardcoded "depth = 4" — this is what lets a single
 * generic VGA controller drive any resolution in 16-color planar
 * mode without needing a chip-specific SVGA extension; true
 * 256-color/24-bit modes go through the VBE linear-framebuffer path
 * in vbe.c instead, which is a completely different, simpler
 * mechanism since the firmware/GOP has already done the mode-set).
 */
void vga_compute_regs(const vga_videomode_t *mode, vga_moderegs_t *regs) {
    vga_videomode_t m = *mode;   /* local copy: vdisplay may be doubled below */
    int i;
    const uint8_t palette[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
        0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f
    };
    const int depth = 4;

    /* Sync polarity / misc output */
    if ((m.flags & (VID_PHSYNC | VID_NHSYNC)) &&
        (m.flags & (VID_PVSYNC | VID_NVSYNC))) {
        regs->miscout = 0x23;
        if (m.flags & VID_NHSYNC) regs->miscout |= 0x40;
        if (m.flags & VID_NVSYNC) regs->miscout |= 0x80;
    } else {
        if (m.flags & VID_DBLSCAN) m.vdisplay *= 2;
        if      (m.vdisplay < 400) regs->miscout = 0xa3;
        else if (m.vdisplay < 480) regs->miscout = 0x63;
        else if (m.vdisplay < 768) regs->miscout = 0xe3;
        else                       regs->miscout = 0x23;
    }

    /* Sequencer (timing/clocking) */
    regs->ts[0] = (depth == 4) ? 0x02 : 0x00;
    regs->ts[1] = (m.flags & VID_CLKDIV2) ? 0x09 : 0x01;
    regs->ts[2] = 0x0f;
    regs->ts[3] = 0x00;
    regs->ts[4] = (depth < 8) ? 0x06 : 0x0e;

    /* CRTC controller — this block is where the actual resolution
     * and refresh-rate timings turn into hardware register values.
     * The bit-packing here (overflow bits scattered into crtc[7],
     * crtc[9] etc.) is mandated by the MC6845-derived VGA CRTC
     * register layout itself, not a NetBSD design choice — every
     * VGA-compatible mode-setting driver on any OS does the same
     * bit placement because the silicon expects it there. */
    regs->crtc[0]  = (uint8_t)((m.htotal >> 3) - 5);
    regs->crtc[1]  = (uint8_t)((m.hdisplay >> 3) - 1);
    regs->crtc[2]  = (uint8_t)((m.hsync_start >> 3) - 1);
    regs->crtc[3]  = (uint8_t)((((m.hsync_end >> 3) - 1) & 0x1f) | 0x80);
    regs->crtc[4]  = (uint8_t)(m.hsync_start >> 3);
    regs->crtc[5]  = (uint8_t)((((( m.hsync_end >> 3) - 1) & 0x20) << 2)
                              | (((m.hsync_end >> 3)) & 0x1f));
    regs->crtc[6]  = (uint8_t)((m.vtotal - 2) & 0xff);
    regs->crtc[7]  = (uint8_t)((((m.vtotal - 2) & 0x100) >> 8)
                    | (((m.vdisplay - 1) & 0x100) >> 7)
                    | ((m.vsync_start & 0x100) >> 6)
                    | (((m.vsync_start - 1) & 0x100) >> 5)
                    | 0x10
                    | (((m.vtotal - 2) & 0x200) >> 4)
                    | (((m.vdisplay - 1) & 0x200) >> 3)
                    | ((m.vsync_start & 0x200) >> 2));
    regs->crtc[8]  = 0x00;
    regs->crtc[9]  = (uint8_t)((((m.vsync_start - 1) & 0x200) >> 4) | 0x40);
    if (m.flags & VID_DBLSCAN) regs->crtc[9] |= 0x80;
    regs->crtc[10] = 0x00;
    regs->crtc[11] = 0x00;
    regs->crtc[12] = 0x00;
    regs->crtc[13] = 0x00;
    regs->crtc[14] = 0x00;
    regs->crtc[15] = 0x00;
    regs->crtc[16] = (uint8_t)(m.vsync_start & 0xff);
    regs->crtc[17] = (uint8_t)((m.vsync_end & 0x0f) | 0x20);
    regs->crtc[18] = (uint8_t)((m.vdisplay - 1) & 0xff);
    regs->crtc[19] = (uint8_t)(m.hdisplay >> 4);
    regs->crtc[20] = 0x00;
    regs->crtc[21] = (uint8_t)((m.vsync_start - 1) & 0xff);
    regs->crtc[22] = (uint8_t)((m.vsync_end - 1) & 0xff);
    regs->crtc[23] = (depth < 8) ? 0xe3 : 0xc3;
    regs->crtc[24] = 0xff;

    /* Graphics display controller */
    regs->gdc[0] = 0x00;
    regs->gdc[1] = 0x00;
    regs->gdc[2] = 0x00;
    regs->gdc[3] = 0x00;
    regs->gdc[4] = 0x00;
    regs->gdc[5] = (depth == 4) ? 0x02 : 0x40;
    regs->gdc[6] = 0x01;
    regs->gdc[7] = 0x0f;
    regs->gdc[8] = 0xff;

    /* Attribute controller */
    for (i = 0; i < 16; i++) regs->atc[i] = palette[i];
    regs->atc[16] = (depth == 4) ? 0x01 : 0x41;
    regs->atc[17] = 0x00;
    regs->atc[18] = 0x0f;
    regs->atc[19] = 0x00;
    regs->atc[20] = 0x00;
}

/* ── vga_apply_regs ────────────────────────────────────────────
 * Adapted from vga_set_mode(). `io` must be a bus_space_handle_t
 * mapped over BUS_SPACE_IO with base 0x3C0 (the standard VGA color
 * I/O range) — see vga_set_mode() below for how that's built. */
void vga_apply_regs(const bus_space_handle_t *io, const vga_moderegs_t *regs) {
    int i;

    /* Blank the display during the mode switch (sequencer clocking
     * mode register, bit 5) — writing mid-mode-switch without this
     * can produce a garbled flash on real hardware. */
    bus_space_write_1(io, VGA_TS_INDEX, 0x01);
    uint8_t cur_mode = bus_space_read_1(io, VGA_TS_DATA);
    bus_space_write_1(io, VGA_TS_INDEX, 0x01);
    bus_space_write_1(io, VGA_TS_DATA, (uint8_t)(cur_mode | VGA_TS_MODE_BLANK));

    /* Misc output register (not index/data — single write port) */
    bus_space_write_1(io, VGA_MISC_DATAW, regs->miscout);

    /* Sequencer: synchronous reset, then clocking mode + remaining
     * sequencer registers, then release reset. */
    bus_space_write_1(io, VGA_TS_INDEX, 0x00);
    bus_space_write_1(io, VGA_TS_DATA, 0x01);                       /* hold reset */
    bus_space_write_1(io, VGA_TS_INDEX, 0x01);
    bus_space_write_1(io, VGA_TS_DATA, (uint8_t)(regs->ts[1] | VGA_TS_MODE_BLANK));
    for (i = 2; i < VGA_TS_NREGS; i++) {
        bus_space_write_1(io, VGA_TS_INDEX, (uint8_t)i);
        bus_space_write_1(io, VGA_TS_DATA, regs->ts[i]);
    }
    bus_space_write_1(io, VGA_TS_INDEX, 0x00);
    bus_space_write_1(io, VGA_TS_DATA, 0x03);                       /* release reset */

    /* CRTC: registers 0-7 are write-protected unless bit 7 of the
     * Vertical Sync End register (index 0x11) is cleared first. */
    bus_space_write_1(io, VGA_CRTC_INDEX, 0x11);
    uint8_t vsynce = bus_space_read_1(io, VGA_CRTC_DATA);
    bus_space_write_1(io, VGA_CRTC_INDEX, 0x11);
    bus_space_write_1(io, VGA_CRTC_DATA, (uint8_t)(vsynce & ~0x80));

    for (i = 0; i < VGA_CRTC_NREGS; i++) {
        bus_space_write_1(io, VGA_CRTC_INDEX, (uint8_t)i);
        bus_space_write_1(io, VGA_CRTC_DATA, regs->crtc[i]);
    }

    /* Graphics display controller */
    for (i = 0; i < VGA_GDC_NREGS; i++) {
        bus_space_write_1(io, VGA_GDC_INDEX, (uint8_t)i);
        bus_space_write_1(io, VGA_GDC_DATA, regs->gdc[i]);
    }

    /* Attribute controller: index and data share one port; a read
     * from the "input status 1" register (offset 0x1A, port 0x3DA)
     * resets the internal index/data flip-flop to index mode before
     * each write, matching the standard VGA AC access protocol. */
    for (i = 0; i < VGA_ATC_NREGS; i++) {
        (void)bus_space_read_1(io, 0x1A);              /* reset flip-flop */
        bus_space_write_1(io, VGA_ATC_INDEX, (uint8_t)i);
        bus_space_write_1(io, VGA_ATC_DATAW, regs->atc[i]);
    }

    /* Unblank */
    bus_space_write_1(io, VGA_TS_INDEX, 0x01);
    uint8_t final_mode = bus_space_read_1(io, VGA_TS_DATA);
    bus_space_write_1(io, VGA_TS_INDEX, 0x01);
    bus_space_write_1(io, VGA_TS_DATA, (uint8_t)(final_mode & ~VGA_TS_MODE_BLANK));
}

void vga_set_mode(const vga_videomode_t *mode) {
    bus_space_handle_t io;
    bus_space_map(BUS_SPACE_IO, 0x3C0, 0x20, 0, &io);

    vga_moderegs_t regs;
    vga_compute_regs(mode, &regs);
    vga_apply_regs(&io, &regs);
}

/* ── Built-in fallback modes ───────────────────────────────────
 * Standard VESA timings (CVT/VESA DMT — a published, vendor-neutral
 * industry standard, the same status as the MBR partition layout:
 * these specific clock/blanking numbers are not anyone's copyrighted
 * source code, they're the publicly documented timing values every
 * monitor and graphics card on Earth already implements). Used when
 * EDID is unavailable and the kernel must pick something that will
 * just work on a generic VGA-compatible display. */
const vga_videomode_t vga_mode_640x480_60 = {
    25175, 640, 656, 752, 800, 480, 490, 492, 525,
    VID_NHSYNC | VID_NVSYNC
};
const vga_videomode_t vga_mode_800x600_60 = {
    40000, 800, 840, 968, 1056, 600, 601, 605, 628,
    VID_PHSYNC | VID_PVSYNC
};
const vga_videomode_t vga_mode_1024x768_60 = {
    65000, 1024, 1048, 1184, 1344, 768, 771, 777, 806,
    VID_NHSYNC | VID_NVSYNC
};
