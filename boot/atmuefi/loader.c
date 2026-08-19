#include <efi.h>
#include <efilib.h>

#define KERNEL_PHYS 0x04000000ULL
#define KERNEL_PAGES 1024U
#define MB2_MAGIC 0x36D76289u

typedef struct __attribute__((packed)){UINT32 total_size,reserved;} mb2_info_t;
typedef struct __attribute__((packed)){UINT32 type,size;} mb2_tag_t;
typedef struct __attribute__((packed)){
    UINT32 type,size;UINT64 addr;UINT32 pitch,width,height;UINT8 bpp,fb_type;UINT16 reserved;
    UINT8 red_pos,red_mask,green_pos,green_mask,blue_pos,blue_mask;
} mb2_fb_t;

extern VOID EFIAPI atmuefi_handoff(UINT64 mbinfo);

static EFI_STATUS read_kernel(EFI_HANDLE image,EFI_SYSTEM_TABLE *st,VOID *target){
    EFI_LOADED_IMAGE *loaded=NULL;EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs=NULL;EFI_FILE_HANDLE root=NULL,file=NULL;EFI_FILE_INFO *info=NULL;EFI_STATUS rc;UINTN info_size=0,read_size;
    rc=st->BootServices->HandleProtocol(image,&LoadedImageProtocol,(VOID **)&loaded);if(EFI_ERROR(rc))goto done;
    rc=st->BootServices->HandleProtocol(loaded->DeviceHandle,&FileSystemProtocol,(VOID **)&fs);if(EFI_ERROR(rc))goto done;
    rc=fs->OpenVolume(fs,&root);if(EFI_ERROR(rc))goto done;
    rc=root->Open(root,&file,L"\\EFI\\ATMKOALA\\KERNEL.BIN",EFI_FILE_MODE_READ,0);if(EFI_ERROR(rc))goto done;
    rc=file->GetInfo(file,&GenericFileInfo,&info_size,NULL);if(rc!=EFI_BUFFER_TOO_SMALL)goto done;
    rc=st->BootServices->AllocatePool(EfiLoaderData,info_size,(VOID **)&info);if(EFI_ERROR(rc))goto done;
    rc=file->GetInfo(file,&GenericFileInfo,&info_size,info);if(EFI_ERROR(rc))goto done;
    if(!info->FileSize||info->FileSize>KERNEL_PAGES*EFI_PAGE_SIZE){rc=EFI_LOAD_ERROR;goto done;}
    read_size=(UINTN)info->FileSize;rc=file->Read(file,&read_size,target);if(!EFI_ERROR(rc)&&read_size!=(UINTN)info->FileSize)rc=EFI_LOAD_ERROR;
done: if(info)st->BootServices->FreePool(info);if(file)file->Close(file);if(root)root->Close(root);return rc;
}

static EFI_STATUS final_exit(EFI_HANDLE image,EFI_SYSTEM_TABLE *st){
    EFI_STATUS rc;UINTN size=0,key=0,desc_size=0;UINT32 version=0;EFI_MEMORY_DESCRIPTOR *map=NULL;
    rc=st->BootServices->GetMemoryMap(&size,map,&key,&desc_size,&version);if(rc!=EFI_BUFFER_TOO_SMALL)return rc;
    size+=desc_size*2;rc=st->BootServices->AllocatePool(EfiLoaderData,size,(VOID **)&map);if(EFI_ERROR(rc))return rc;
    rc=st->BootServices->GetMemoryMap(&size,map,&key,&desc_size,&version);if(!EFI_ERROR(rc))rc=st->BootServices->ExitBootServices(image,key);if(EFI_ERROR(rc))st->BootServices->FreePool(map);return rc;
}

/* Standard Multiboot2 tags; the kernel already parses cmdline, framebuffer and end tags. */
static UINT8 *put_cmd(UINT8 *p,const char *s){
    mb2_tag_t *t=(mb2_tag_t *)p;UINTN n=0;while(s[n])n++;
    t->type=1;t->size=(UINT32)(8+n+1);CopyMem(p+8,s,n+1);p+=((t->size+7)&~7u);return p;
}
static UINT8 *put_fb(UINT8 *p,EFI_GRAPHICS_OUTPUT_PROTOCOL *gop){
    mb2_fb_t *t=(mb2_fb_t *)p;EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *m=gop->Mode->Info;
    SetMem(t,sizeof(*t),0);t->type=8;t->size=38;t->addr=gop->Mode->FrameBufferBase;t->pitch=m->PixelsPerScanLine*4;t->width=m->HorizontalResolution;t->height=m->VerticalResolution;t->bpp=32;t->fb_type=1;
    if(m->PixelFormat==PixelRedGreenBlueReserved8BitPerColor){t->red_pos=0;t->red_mask=8;t->green_pos=8;t->green_mask=8;t->blue_pos=16;t->blue_mask=8;}
    else {t->red_pos=16;t->red_mask=8;t->green_pos=8;t->green_mask=8;t->blue_pos=0;t->blue_mask=8;}
    return p+40;
}
static void build_mbinfo(UINT8 *buf,int mode,EFI_GRAPHICS_OUTPUT_PROTOCOL *gop){
    mb2_info_t *h=(mb2_info_t *)buf;UINT8 *p=buf+8;SetMem(buf,128,0);
    if(mode==2)p=put_cmd(p,"novbe");
    else {p=put_fb(p,gop);if(mode==3)p=put_cmd(p,"installer");}
    ((mb2_tag_t *)p)->type=0;((mb2_tag_t *)p)->size=8;p+=8;h->total_size=(UINT32)(p-buf);h->reserved=0;
}

/* Text grid intentionally uses UEFI Simple Text Output, so the selector stays
 * usable before the graphics protocol is committed to the kernel. */
static int atm_loader_menu(EFI_SYSTEM_TABLE *st){
    EFI_INPUT_KEY key;UINTN index;
    st->ConOut->ClearScreen(st->ConOut);
    Print(L"\r\n ATM LOADER  |  UEFI / ATMUEFI\r\n\r\n");
    Print(L" +----------------------+----------------------+\r\n");
    Print(L" | [1] EXP DESKTOP      | [2] COMPAT CONSOLE   |\r\n");
    Print(L" |     GOP / Exp        |     text-safe        |\r\n");
    Print(L" +----------------------+----------------------+\r\n");
    Print(L" | [3] DISK INSTALLER   | [4] RESTART          |\r\n");
    Print(L" |     graphical setup  |     firmware reset   |\r\n");
    Print(L" +----------------------+----------------------+\r\n\r\n Choose 1-4 (Enter = desktop): ");
    st->ConIn->Reset(st->ConIn,FALSE);
    for(;;){
        while(st->ConIn->ReadKeyStroke(st->ConIn,&key)==EFI_NOT_READY)st->BootServices->WaitForEvent(1,&st->ConIn->WaitForKey,&index);
        if(key.UnicodeChar==CHAR_CARRIAGE_RETURN||key.UnicodeChar==L'1')return 1;
        if(key.UnicodeChar==L'2')return 2;
        if(key.UnicodeChar==L'3')return 3;
        if(key.UnicodeChar==L'4'){st->RuntimeServices->ResetSystem(EfiResetCold,EFI_SUCCESS,0,NULL);}
    }
}

EFI_STATUS efi_main(EFI_HANDLE image,EFI_SYSTEM_TABLE *st){
    EFI_STATUS rc;EFI_GRAPHICS_OUTPUT_PROTOCOL *gop=NULL;EFI_PHYSICAL_ADDRESS kernel=KERNEL_PHYS,mb=0xFFFFFFFFULL;int mode;
    InitializeLib(image,st);mode=atm_loader_menu(st);
    rc=st->BootServices->AllocatePages(AllocateAddress,EfiLoaderData,KERNEL_PAGES,&kernel);if(EFI_ERROR(rc)||kernel!=KERNEL_PHYS){Print(L"\r\nATMUEFI: kernel memory unavailable: %r\r\n",rc);return rc;}
    rc=st->BootServices->AllocatePages(AllocateMaxAddress,EfiLoaderData,1,&mb);if(EFI_ERROR(rc)||mb>0xFFFFFFFFULL){Print(L"\r\nATMUEFI: boot info memory unavailable: %r\r\n",rc);return rc;}
    SetMem((VOID *)(UINTN)kernel,KERNEL_PAGES*EFI_PAGE_SIZE,0);rc=read_kernel(image,st,(VOID *)(UINTN)kernel);if(EFI_ERROR(rc)){Print(L"\r\nATMUEFI: KERNEL.BIN read failed: %r\r\n",rc);return rc;}
    if(mode!=2){rc=st->BootServices->LocateProtocol(&GraphicsOutputProtocol,NULL,(VOID **)&gop);if(EFI_ERROR(rc)||!gop||!gop->Mode||!gop->Mode->Info){Print(L"\r\nATMUEFI: GOP unavailable; choose COMPAT CONSOLE.\r\n");return EFI_UNSUPPORTED;}Print(L"\r\nGOP %ux%u ready\r\n",gop->Mode->Info->HorizontalResolution,gop->Mode->Info->VerticalResolution);}
    build_mbinfo((UINT8 *)(UINTN)mb,mode,gop);
    rc=final_exit(image,st);if(EFI_ERROR(rc)){Print(L"ATMUEFI: ExitBootServices failed: %r\r\n",rc);return rc;}
    atmuefi_handoff(mb);return EFI_LOAD_ERROR;
}
