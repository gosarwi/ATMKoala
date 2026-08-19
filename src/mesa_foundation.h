#ifndef ATM_MESA_FOUNDATION_H
#define ATM_MESA_FOUNDATION_H

#include <stdint.h>

/* This is an ATMKoala capability foundation for a future graphics stack.
 * It is not Mesa source code and does not expose OpenGL, EGL, Gallium, DRM
 * or DRI ABI. */
#define ATM_GFX_FOUNDATION_ABI 1u
#define ATM_GFX_CAP_FRAMEBUFFER       0x0001u
#define ATM_GFX_CAP_SOFTWARE_RENDERER 0x0002u
#define ATM_GFX_CAP_FIXED_TRIANGLES   0x0004u
#define ATM_GFX_CAP_HW_ACCELERATION   0x0008u /* intentionally unset today */
#define ATM_GFX_CAP_PROCESS_ACCOUNTING 0x0010u /* intentionally unset today */

typedef struct {
    uint32_t abi_version;
    uint32_t capabilities;
    uint32_t width, height, pitch, bpp;
    uint64_t framebuffer_bytes;
    const char *display_backend;
    const char *renderer_backend;
} atm_gfx_capabilities_t;

/* Always returns a truthful capability snapshot. A zero framebuffer size
 * means the system is currently in VGA text mode. */
int mesa_foundation_query(atm_gfx_capabilities_t *out);
const char *mesa_foundation_status(void);

#endif
