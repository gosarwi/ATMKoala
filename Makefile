# atmkoala v0.5 — Makefile (x86-64)
CC  = gcc
AS  = as
LD  = ld
OBJCOPY = objcopy

GCC_INC = $(shell $(CC) -print-file-name=include)

CFLAGS  = -m64 -ffreestanding -fno-pie -fno-pic \
          -fno-stack-protector -mno-red-zone \
          -mno-mmx -mno-sse -mno-sse2 -msoft-float \
          -mcmodel=kernel \
          -nostdlib -nostdinc -isystem $(GCC_INC) \
          -std=gnu99 -I src -O2 \
          -fno-builtin-memcpy -fno-builtin-memset -fno-builtin-memcmp \
          -Wno-unused-parameter -Wno-unused-function \
          -Wno-unused-variable -Wno-missing-braces \
          -Wno-misleading-indentation -Wno-implicit-function-declaration \
          -Wno-int-conversion -Wno-pointer-sign

ASFLAGS = --64
LDFLAGS = -m elf_x86_64 -z max-page-size=0x1000

KERNEL  = build/kernel.bin
# Limine is the sole primary ISO boot path.
ISO     = atmkoala-OS-v0.9-limine.iso
LIMINE_DIR = third_party/limine/bin
LIMINE_TOOL = $(LIMINE_DIR)/limine
LIMINE_ROOT = build/limine-iso
LIMINE_CONF = boot/limine/limine.conf
LIMINE_UEFI_CONF = boot/limine/limine-uefi.conf
LIMINE_UEFI_CD = build/limine-uefi-cd.bin
ATMBOOT_IMG = atmkoala-atmboot.img
ATMBOOT_STAGE0 = build/atmboot-stage0.bin
ATMBOOT_STAGE2 = build/atmboot-stage2.bin
ATMBOOT_STAGE2_PAD = build/atmboot-stage2-pad.bin
ATMBOOT_KERNEL = build/atmboot-kernel.raw
ATMUEFI_IMG = atmkoala-atmuefi.img
ATMUEFI_DIR = build/atmuefi
ATMUEFI_EFI = $(ATMUEFI_DIR)/BOOTX64.EFI
ATMUEFI_FLAT = $(ATMUEFI_DIR)/KERNEL.BIN

NATIVE_LIBC_DIR = sdk/libc
NATIVE_LIBC_CFLAGS = -m64 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -msoft-float -nostdlib -nostdinc -isystem $(GCC_INC) -std=gnu99 -O2 -fno-builtin -I sdk -I $(NATIVE_LIBC_DIR)/include
LIBC_SMOKE_ELF = build/libc_smoke.elf
LIBC_SMOKE_BLOB = build/libc_smoke_blob.o

OBJS = build/boot.o        $(LIBC_SMOKE_BLOB) build/gdt.o        build/idt.o       build/pit.o     \
       build/kmalloc.o     build/vga.o        build/vbe.o       build/keyboard.o build/mouse.o \
       build/util.o        build/paging.o     build/uaccess.o    build/usermode.o  build/native_fd.o build/native_socket.o build/native_app.o build/vfs.o        build/sched.o     build/elf.o     \
       build/net.o         build/net_tcp.o     build/disk.o       build/catfs.o      build/catfs_vfs.o \
       build/config.o      build/locale.o      build/users.o      build/atminit.o build/atm_posix.o build/atm_syscall.o \
       build/partmgr.o     build/diskmgr.o    build/bsd_compat.o build/vga_modeset.o \
       build/icmp.o        build/dns.o                                            \
       build/tarzst.o        build/atmbox.o     build/fileformat.o build/image_decode.o build/image_fixtures.o build/awm.o       build/exp.o       build/ossdk.o   \
       build/fat32.o        build/ext2.o       build/ext2_vfs.o   build/btrfs.o      build/hw_y116.o     build/tinygl_lite.o build/mesa_foundation.o \
       build/osbuilder.o   build/font.o       build/gamesdk.o                   \
       build/minesweeper.o build/snake_game.o build/gamelauncher.o build/store.o                                      \
       build/ttf.o        build/kmod.o        build/installer.o build/gui_demo.o \
       build/kernel_panic.o build/fish_shell.o                                  \
       build/unm.o         build/untui.o                                        \
       build/kernel.o

.PHONY: all iso limine limine-iso atmboot atmuefi clean run run-vbe run-limine run-atmboot run-atmuefi

all: $(KERNEL)

$(KERNEL): $(OBJS) linker.ld | build
	$(LD) $(LDFLAGS) -T linker.ld $(OBJS) -o $@
	@echo "[+] atmkoala x64: $@ ($$(wc -c < $@) bytes)"

# Canonical ISO target: Limine is the only primary bootloader.
iso: limine-iso

# Limine BIOS+UEFI hybrid ISO. The existing kernel keeps its Multiboot2
# entry and receives the same MB2 magic/info contract under the new loader.
$(LIMINE_TOOL): $(LIMINE_DIR)/limine.c $(LIMINE_DIR)/Makefile
	$(MAKE) -C $(LIMINE_DIR) limine

limine: iso

# Build a self-contained FAT12 UEFI El Torito image. Limine's UEFI binary,
# its config and the Multiboot2 kernel are all on the same boot volume, so
# `boot():` remains unambiguous in OVMF and on physical optical media.
$(LIMINE_UEFI_CD): $(KERNEL) $(LIMINE_UEFI_CONF) $(LIMINE_DIR)/BOOTX64.EFI | build
	rm -f $@
	dd if=/dev/zero of=$@ bs=1M count=4 status=none
	mkfs.fat -F 12 -n ATMLIMINE $@ >/dev/null
	mmd -i $@ ::/EFI ::/EFI/BOOT
	mcopy -i $@ $(LIMINE_DIR)/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $@ $(KERNEL) ::/kernel.bin
	mcopy -i $@ $(LIMINE_UEFI_CONF) ::/limine.conf

limine-iso: $(KERNEL) $(LIMINE_TOOL) $(LIMINE_UEFI_CD) $(LIMINE_CONF) $(LIMINE_DIR)/limine-bios-cd.bin $(LIMINE_DIR)/limine-bios.sys $(LIMINE_DIR)/BOOTX64.EFI
	@echo "[*] Building Limine hybrid ISO: $(ISO)"
	rm -rf $(LIMINE_ROOT) $(ISO)
	mkdir -p $(LIMINE_ROOT)/boot $(LIMINE_ROOT)/EFI/BOOT
	cp $(KERNEL) $(LIMINE_ROOT)/boot/kernel.bin
	cp $(LIMINE_CONF) $(LIMINE_ROOT)/limine.conf
	# UEFI Limine checks an adjacent config first; root copy remains for BIOS.
	cp $(LIMINE_CONF) $(LIMINE_ROOT)/EFI/BOOT/limine.conf
	cp $(LIMINE_DIR)/limine-bios-cd.bin $(LIMINE_ROOT)/boot/limine-bios-cd.bin
	cp $(LIMINE_DIR)/limine-bios.sys $(LIMINE_ROOT)/boot/limine-bios.sys
	cp $(LIMINE_UEFI_CD) $(LIMINE_ROOT)/boot/limine-uefi-cd.bin
	cp $(LIMINE_DIR)/BOOTX64.EFI $(LIMINE_ROOT)/EFI/BOOT/BOOTX64.EFI
	# 16 ISO sectors = 32 KiB / 64 MBR sectors; Limine BIOS stage installation
	# requires a safe partition offset rather than an MBR partition at LBA < 63.
	xorriso -as mkisofs -R -r -J -partition_offset 16 -b boot/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table --efi-boot boot/limine-uefi-cd.bin -efi-boot-part --efi-boot-image $(LIMINE_ROOT) -o $(ISO)
	$(LIMINE_TOOL) bios-install $(ISO)
	@echo "[+] Limine ISO ready: $(ISO) ($$(du -sh $(ISO) | cut -f1))"

# ATMBOOT is a frozen, separate BIOS/IDE raw image (stage0 -> stage2 -> raw kernel).
atmboot: $(KERNEL) | build
	$(AS) --32 boot/atmboot/stage0.s -o build/atmboot-stage0.o
	$(LD) -m elf_i386 -Ttext 0x7C00 --oformat binary build/atmboot-stage0.o -o $(ATMBOOT_STAGE0)
	@test $$(wc -c < $(ATMBOOT_STAGE0)) -eq 512 || (echo "ERROR: ATMBOOT stage0 must be 512 bytes" && exit 1)
	$(AS) --32 boot/atmboot/stage2.s -o build/atmboot-stage2.o
	$(LD) -m elf_i386 -Ttext 0x8000 --oformat binary build/atmboot-stage2.o -o $(ATMBOOT_STAGE2)
	@test $$(wc -c < $(ATMBOOT_STAGE2)) -le 32768 || (echo "ERROR: ATMBOOT stage2 exceeds 64 sectors" && exit 1)
	dd if=/dev/zero of=$(ATMBOOT_STAGE2_PAD) bs=512 count=64 status=none
	dd if=$(ATMBOOT_STAGE2) of=$(ATMBOOT_STAGE2_PAD) conv=notrunc status=none
	@sectors=$$((($$(wc -c < $(KERNEL))+511)/512)); test $$sectors -le 1024 || (echo "ERROR: kernel exceeds ATMBOOT 1024-sector payload" && exit 1)
	$(OBJCOPY) -O binary $(KERNEL) build/atmboot-kernel-flat.bin
	@sectors=$$((($$(wc -c < build/atmboot-kernel-flat.bin)+511)/512)); test $$sectors -le 1024 || (echo "ERROR: flat kernel exceeds ATMBOOT 1024-sector payload" && exit 1)
	dd if=/dev/zero of=$(ATMBOOT_KERNEL) bs=512 count=1024 status=none
	dd if=build/atmboot-kernel-flat.bin of=$(ATMBOOT_KERNEL) conv=notrunc status=none
	@cat $(ATMBOOT_STAGE0) $(ATMBOOT_STAGE2_PAD) $(ATMBOOT_KERNEL) > $(ATMBOOT_IMG)
	@echo "[+] ATMBOOT BIOS image: $(ATMBOOT_IMG) ($$(wc -c < $(ATMBOOT_IMG)) bytes)"

# ATMUEFI is a UEFI x86-64 FAT ESP image. It shares the exact flat kernel
# payload with ATMBOOT but loads it through UEFI Simple File System + GOP.
atmuefi: $(KERNEL) | build
	@test "$$(nm -n $(KERNEL) | awk '/ uefi_start$$/{print $$1}')" = "0000000004001158" || (echo "ERROR: ATMUEFI uefi_start layout changed; update handoff address" && exit 1)
	@mkdir -p $(ATMUEFI_DIR)
	$(CC) -I/usr/include/efi -I/usr/include/efi/x86_64 -fpic -fshort-wchar -mno-red-zone -fno-stack-protector -ffreestanding -fno-strict-aliasing -maccumulate-outgoing-args -DGNU_EFI_USE_MS_ABI -c boot/atmuefi/loader.c -o $(ATMUEFI_DIR)/loader.o
	$(AS) --64 boot/atmuefi/handoff.s -o $(ATMUEFI_DIR)/handoff.o
	$(LD) -nostdlib -znocombreloc -T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic -L/usr/lib /usr/lib/crt0-efi-x86_64.o $(ATMUEFI_DIR)/loader.o $(ATMUEFI_DIR)/handoff.o -lefi -lgnuefi -o $(ATMUEFI_DIR)/loader.so
	$(OBJCOPY) --subsystem=10 -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc -O pei-x86-64 $(ATMUEFI_DIR)/loader.so $(ATMUEFI_EFI)
	$(OBJCOPY) -O binary $(KERNEL) $(ATMUEFI_FLAT)
	@rm -f $(ATMUEFI_IMG)
	dd if=/dev/zero of=$(ATMUEFI_IMG) bs=1M count=64 status=none
	sgdisk --clear --new=1:2048:0 --typecode=1:ef00 --change-name=1:ATMUEFI-ESP $(ATMUEFI_IMG) >/dev/null
	mkfs.fat -F 32 --offset=2048 -n ATMUEFI $(ATMUEFI_IMG) >/dev/null
	mmd -i $(ATMUEFI_IMG)@@1048576 ::/EFI ::/EFI/BOOT ::/EFI/ATMKOALA
	mcopy -i $(ATMUEFI_IMG)@@1048576 $(ATMUEFI_EFI) ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $(ATMUEFI_IMG)@@1048576 $(ATMUEFI_FLAT) ::/EFI/ATMKOALA/KERNEL.BIN
	@echo "[+] ATMUEFI ESP image: $(ATMUEFI_IMG) ($$(du -h $(ATMUEFI_IMG) | cut -f1))"

run-atmuefi: atmuefi
	@cp /usr/share/OVMF/OVMF_VARS_4M.fd /tmp/atmkoala-atmuefi-vars.fd
	qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
	    -drive if=pflash,format=raw,file=/tmp/atmkoala-atmuefi-vars.fd \
	    -drive file=$(ATMUEFI_IMG),format=raw,if=virtio -m 256M -vga std -no-reboot -name "ATMKoala [ATMUEFI]"

build/boot.o: boot/boot.s | build
	$(AS) $(ASFLAGS) $< -o $@

$(LIBC_SMOKE_ELF): $(NATIVE_LIBC_DIR)/src/crt0.s $(NATIVE_LIBC_DIR)/src/crt.c $(NATIVE_LIBC_DIR)/src/string.c $(NATIVE_LIBC_DIR)/src/ctype.c $(NATIVE_LIBC_DIR)/src/errno.c $(NATIVE_LIBC_DIR)/src/unistd.c $(NATIVE_LIBC_DIR)/src/malloc.c $(NATIVE_LIBC_DIR)/src/stdlib.c $(NATIVE_LIBC_DIR)/src/stdio.c $(NATIVE_LIBC_DIR)/src/socket.c $(NATIVE_LIBC_DIR)/src/atm_gui_stub.c $(NATIVE_LIBC_DIR)/src/internal.h $(NATIVE_LIBC_DIR)/demo/libc_smoke.c $(NATIVE_LIBC_DIR)/atm_native.ld $(NATIVE_LIBC_DIR)/include/atm_crt.h $(NATIVE_LIBC_DIR)/include/ctype.h $(NATIVE_LIBC_DIR)/include/errno.h $(NATIVE_LIBC_DIR)/include/unistd.h $(NATIVE_LIBC_DIR)/include/fcntl.h $(NATIVE_LIBC_DIR)/include/stdlib.h $(NATIVE_LIBC_DIR)/include/string.h $(NATIVE_LIBC_DIR)/include/stdio.h $(NATIVE_LIBC_DIR)/include/sys/stat.h $(NATIVE_LIBC_DIR)/include/sys/socket.h $(NATIVE_LIBC_DIR)/include/netinet/in.h sdk/atm_native_abi.h sdk/atm_gui.h | build
	@mkdir -p build/libc-smoke
	$(AS) --64 $(NATIVE_LIBC_DIR)/src/crt0.s -o build/libc-smoke/crt0.o
	$(CC) $(NATIVE_LIBC_CFLAGS) -c $(NATIVE_LIBC_DIR)/src/crt.c -o build/libc-smoke/crt.o
	$(CC) $(NATIVE_LIBC_CFLAGS) -c $(NATIVE_LIBC_DIR)/src/string.c -o build/libc-smoke/string.o
	$(CC) $(NATIVE_LIBC_CFLAGS) -c $(NATIVE_LIBC_DIR)/src/ctype.c -o build/libc-smoke/ctype.o
	$(CC) $(NATIVE_LIBC_CFLAGS) -c $(NATIVE_LIBC_DIR)/src/errno.c -o build/libc-smoke/errno.o
	$(CC) $(NATIVE_LIBC_CFLAGS) -c $(NATIVE_LIBC_DIR)/src/unistd.c -o build/libc-smoke/unistd.o
	$(CC) $(NATIVE_LIBC_CFLAGS) -c $(NATIVE_LIBC_DIR)/src/malloc.c -o build/libc-smoke/malloc.o
	$(CC) $(NATIVE_LIBC_CFLAGS) -c $(NATIVE_LIBC_DIR)/src/stdlib.c -o build/libc-smoke/stdlib.o
	$(CC) $(NATIVE_LIBC_CFLAGS) -c $(NATIVE_LIBC_DIR)/src/stdio.c -o build/libc-smoke/stdio.o
	$(CC) $(NATIVE_LIBC_CFLAGS) -c $(NATIVE_LIBC_DIR)/src/socket.c -o build/libc-smoke/socket.o
	$(CC) $(NATIVE_LIBC_CFLAGS) -c $(NATIVE_LIBC_DIR)/src/atm_gui_stub.c -o build/libc-smoke/atm_gui_stub.o
	$(CC) $(NATIVE_LIBC_CFLAGS) -c $(NATIVE_LIBC_DIR)/demo/libc_smoke.c -o build/libc-smoke/main.o
	$(LD) -m elf_x86_64 -T $(NATIVE_LIBC_DIR)/atm_native.ld build/libc-smoke/crt0.o build/libc-smoke/crt.o build/libc-smoke/string.o build/libc-smoke/ctype.o build/libc-smoke/errno.o build/libc-smoke/unistd.o build/libc-smoke/malloc.o build/libc-smoke/stdlib.o build/libc-smoke/stdio.o build/libc-smoke/socket.o build/libc-smoke/atm_gui_stub.o build/libc-smoke/main.o -o $@

$(LIBC_SMOKE_BLOB): $(LIBC_SMOKE_ELF) | build
	$(LD) -r -b binary -o $@ $<
	$(OBJCOPY) --add-section .note.GNU-stack=/dev/null --set-section-flags .note.GNU-stack=noload,readonly $@

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build:    ; @mkdir -p build

disk.img:
	dd if=/dev/zero of=disk.img bs=1M count=64 2>/dev/null

run: $(ISO)
	qemu-system-x86_64 -cdrom $(ISO) -m 256M -no-reboot \
	    -name "atmkoala x64 [VGA]"

run-vbe: $(ISO)
	qemu-system-x86_64 -cdrom $(ISO) -m 256M -no-reboot -vga std \
		-name "ATMKoala [VBE]"

run-limine: iso
	qemu-system-x86_64 -cdrom $(ISO) -m 256M -no-reboot -vga std \
		-name "ATMKoala [Limine]"

run-atmboot: atmboot
	qemu-system-x86_64 -drive file=$(ATMBOOT_IMG),format=raw,if=ide \
	    -m 256M -no-reboot -vga std -name "ATMKoala [ATMBOOT BIOS]"

run-full: $(ISO) disk.img
	qemu-system-x86_64 -cdrom $(ISO) -drive file=disk.img,format=raw,if=ide \
	    -m 256M -no-reboot -vga std \
	    -netdev user,id=net0 -device rtl8139,netdev=net0 \
	    -name "atmkoala x64 [Full]"

clean:
	rm -rf build/ $(ISO) $(ATMBOOT_IMG) $(ATMUEFI_IMG)

clean-all: clean
	rm -f disk.img
