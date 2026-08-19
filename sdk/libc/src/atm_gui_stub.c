#include "atm_gui.h"
#include "errno.h"

/*
 * v0.9 publishes the client ABI before its compositor transport is enabled.
 * A linked application can therefore perform feature detection without weak
 * symbols or private Exp headers. Windows/surfaces remain unavailable until
 * the shared-memory event transport milestone.
 */
int atm_gui_runtime_info(atm_gui_runtime_info_t *out){
    if(!out){ errno=EFAULT; return -1; }
    out->abi_version=ATM_GUI_ABI_VERSION;
    out->struct_size=(uint32_t)sizeof(*out);
    out->capabilities=0;
    out->reserved=0;
    return 0;
}

int atm_gui_window_create(const atm_gui_window_desc_t *desc, atm_gui_window_t *out){
    (void)desc; (void)out; errno=ENOSYS; return -1;
}
int atm_gui_window_destroy(atm_gui_window_t window){
    (void)window; errno=ENOSYS; return -1;
}
int atm_gui_surface_create(atm_gui_window_t window,const atm_gui_surface_desc_t *desc,
                           atm_gui_surface_t *out,void **pixels_out){
    (void)window; (void)desc; (void)out; (void)pixels_out; errno=ENOSYS; return -1;
}
int atm_gui_surface_present(atm_gui_surface_t surface,const atm_gui_rect_t *damage,
                            uint32_t damage_count,uint32_t flags){
    (void)surface; (void)damage; (void)damage_count; (void)flags; errno=ENOSYS; return -1;
}
int atm_gui_event_next(atm_gui_event_t *out_event){
    (void)out_event; errno=ENOSYS; return -1;
}
