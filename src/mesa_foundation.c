#include "mesa_foundation.h"
#include "vbe.h"
#include "tinygl_lite.h"
#include "util.h"

int mesa_foundation_query(atm_gfx_capabilities_t *out){
    if(!out) return -1;
    kmemset(out,0,sizeof(*out));
    out->abi_version=ATM_GFX_FOUNDATION_ABI;
    out->capabilities=ATM_GFX_CAP_SOFTWARE_RENDERER|ATM_GFX_CAP_FIXED_TRIANGLES;
    out->renderer_backend=tgl_renderer_name();
    if(vbe.active){
        out->capabilities|=ATM_GFX_CAP_FRAMEBUFFER;
        out->width=vbe.width;
        out->height=vbe.height;
        out->pitch=vbe.pitch;
        out->bpp=vbe.bpp;
        out->framebuffer_bytes=(uint64_t)vbe.pitch*(uint64_t)vbe.height;
        out->display_backend="VBE/Multiboot framebuffer";
    }else out->display_backend="VGA text mode";
    return 0;
}

const char *mesa_foundation_status(void){
    return "foundation only: framebuffer + fixed-point software renderer; no Mesa/Gallium/OpenGL/EGL/DRM";
}
