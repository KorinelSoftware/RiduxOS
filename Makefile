CC := gcc
LD := ld
PYTHON := python3

CFLAGS := -m64 -ffreestanding -fno-pie -fno-stack-protector -mno-red-zone -mcmodel=large -mno-mmx -mno-sse -mno-sse2 -msoft-float -fno-tree-vectorize -O2 -Wall -Wextra -Wno-unused-parameter -std=gnu11
LDFLAGS := -m elf_x86_64 -nostdlib -T linker.ld

BUILD_DIR := build
ISO_DIR := $(BUILD_DIR)/iso
KERNEL_BIN := $(BUILD_DIR)/ridux-kernel.bin
INITRD_IMG := $(BUILD_DIR)/initrd.img
INITRD_OVERLAY_IMG := $(BUILD_DIR)/initrd-overlay.img
ROOTFS_DIR := rootfs
FREEBSD_SRC_DIR := third_party/upstream/freebsd-src
FREEBSD_REF := stable/14
DEMO_ELF := $(ROOTFS_DIR)/bin/ridux-demo.elf
ABI_SMOKE_ELF := $(ROOTFS_DIR)/bin/abi-smoke.elf
X11_SMOKE_ELF := $(ROOTFS_DIR)/bin/x11-smoke.elf
BROWSER64_ELF := $(ROOTFS_DIR)/bin/chrome.elf
FIREFOX64_ELF := $(ROOTFS_DIR)/bin/firefox.elf
NOTES_R3_ELF  := $(ROOTFS_DIR)/bin/notes-r3.elf
STACKCHK_TRACE_SO := $(ROOTFS_DIR)/opt/ridux/stackchk_trace.so
ISO := $(BUILD_DIR)/RiduxOS-Unix.iso
FONT_HEADER := $(BUILD_DIR)/font8x16.h
ASSETS_HEADER := $(BUILD_DIR)/assets.h
KERNEL_PARTS := $(wildcard src/kernel/*.c)
ICON_ASSETS := $(wildcard RiduxIcons/*.png) $(wildcard RiduxIcons/Wallpaper/*.png)
WALLPAPER_ASSETS := $(wildcard Wallpapers/*.png) $(wildcard Wallpapers/*.jpg) $(wildcard Wallpapers/*.jpeg)

# FreeBSD Linuxulator import flags. Imported files in src/linuxulator/
# are copied verbatim from third_party/upstream/freebsd-src/sys/compat/linux.
#
# Include path strategy:
#   1. -Isrc/freebsd_compat        first  -> our minimal shim wins for the
#                                            <sys/*> headers we override
#                                            (param.h, systm.h, errno.h, ...)
#   2. -Ithird_party/.../sys/amd64 next   -> resolves <machine/../linux/*>
#                                            (which actually means amd64/linux/*)
#   3. -Ithird_party/.../sys       last   -> resolves <compat/linux/*>,
#                                            <netinet/*>, etc. that we have
#                                            no opinion on yet.
#
# This lets us scale the import without copying hundreds of header files.
# When the upstream tree's <sys/X.h> conflicts with our shim, we just add
# a curated stub to src/freebsd_compat/sys/X.h.
LINUXULATOR_CFLAGS := -Isrc/freebsd_compat \
                      -Ithird_party/upstream/freebsd-src/sys/amd64 \
                      -Ithird_party/upstream/freebsd-src/sys

OBJS := \
	$(BUILD_DIR)/boot64.o \
	$(BUILD_DIR)/isr64.o \
	$(BUILD_DIR)/flush.o \
	$(BUILD_DIR)/compat.o \
	$(BUILD_DIR)/compat2.o \
	$(BUILD_DIR)/compat3.o \
	$(BUILD_DIR)/compat4.o \
	$(BUILD_DIR)/compat5.o \
	$(BUILD_DIR)/compat6.o \
	$(BUILD_DIR)/compat7.o \
	$(BUILD_DIR)/compat8.o \
	$(BUILD_DIR)/ridux_freebsd_glue.o \
	$(BUILD_DIR)/linux_errno.o \
	$(BUILD_DIR)/linux_rseq.o \
	$(BUILD_DIR)/linux_emul.o \
	$(BUILD_DIR)/kernel.o

.DELETE_ON_ERROR:

.PHONY: all kernel-only r3-apps-only iso-from-existing-initrd clean run glib-compat-patch chromium-compat-patch fontconfig-compat-patch chromium-rootfs firefox-rootfs browsers-rootfs vendor-upstream vendor-check freebsd-bootstrap freebsd-prepare freebsd-browser-iso freebsd-browser-iso-fast freebsd-browser-vm freebsd-browser-vm-quick freebsd-compat-snapshot freebsd-ridux-kernel freebsd-live-iso freebsd-live-vm freebsd-live-iso-kernel freebsd-live-vm-kernel freebsd-live-iso-orbit freebsd-live-vm-orbit debian-netinst-iso debian-runtime-seed-iso debian-vm debian-vm-quick debian-live-ridux-iso debian-live-ridux-iso-fast debian-live-ridux-vm debian-live-ridux-vm-fast sel4-browser-vm-info sel4-browser-vm-deps sel4-browser-vm-bootstrap sel4-browser-vm-build sel4-browser-vm-stage sel4-browser-vm-iso

all: $(ISO)

kernel-only: $(KERNEL_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

glib-compat-patch: tools/patch_glib_g_strerror.py
	$(PYTHON) tools/patch_glib_g_strerror.py

chromium-compat-patch: tools/patch_chromium_fd_ownership.py
	$(PYTHON) tools/patch_chromium_fd_ownership.py

fontconfig-compat-patch: tools/patch_fontconfig_cache_guard.py
	$(PYTHON) tools/patch_fontconfig_cache_guard.py

$(FONT_HEADER): tools/gen_font.py | $(BUILD_DIR)
	$(PYTHON) tools/gen_font.py $(FONT_HEADER)

$(ASSETS_HEADER): tools/gen_assets.py | $(BUILD_DIR)
	$(PYTHON) tools/gen_assets.py $(ASSETS_HEADER)

$(BUILD_DIR)/boot64.o: src/boot64.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/isr64.o: src/isr64.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/flush.o: src/flush.c src/flush.h $(FONT_HEADER) $(ASSETS_HEADER) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(BUILD_DIR) -c $< -o $@

$(BUILD_DIR)/compat.o: src/compat/base.c src/compat/base.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(BUILD_DIR) -Isrc -c $< -o $@

$(BUILD_DIR)/compat2.o: src/compat/memory_tasks.c src/compat/memory_tasks.h src/compat/base.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(BUILD_DIR) -Isrc -c $< -o $@

$(BUILD_DIR)/compat3.o: src/compat/linux_syscalls.c src/compat/linux_syscalls.h src/compat/memory_tasks.h src/compat/base.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(BUILD_DIR) -Isrc -c $< -o $@

$(BUILD_DIR)/compat4.o: src/compat/user_libc.c src/compat/user_libc.h src/compat/linux_syscalls.h src/compat/memory_tasks.h src/compat/base.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(BUILD_DIR) -Isrc -c $< -o $@

$(BUILD_DIR)/compat5.o: src/compat/bsd_libc.c src/compat/bsd_libc.h src/compat/user_libc.h src/compat/linux_syscalls.h src/compat/memory_tasks.h src/compat/base.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(BUILD_DIR) -Isrc -c $< -o $@

$(BUILD_DIR)/compat6.o: src/compat/linux_abi.c src/compat/linux_abi.h src/compat/bsd_libc.h src/compat/user_libc.h src/compat/linux_syscalls.h src/compat/memory_tasks.h src/compat/base.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(BUILD_DIR) -Isrc -c $< -o $@

$(BUILD_DIR)/compat7.o: src/compat/display_wayland.c src/compat/display_wayland.h src/compat/linux_abi.h src/compat/bsd_libc.h src/compat/user_libc.h src/compat/linux_syscalls.h src/compat/memory_tasks.h src/compat/base.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(BUILD_DIR) -Isrc -c $< -o $@

$(BUILD_DIR)/compat8.o: src/compat/browser_runtime.c src/compat/browser_runtime.h src/compat/display_wayland.h src/compat/linux_abi.h src/compat/bsd_libc.h src/compat/user_libc.h src/compat/linux_syscalls.h src/compat/memory_tasks.h src/compat/base.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(BUILD_DIR) -Isrc -c $< -o $@

# Imported FreeBSD Linuxulator object files. Each rule compiles a file
# from src/linuxulator/ against the shim layer in src/freebsd_compat/.
# Keep -Wno-* relaxations narrow so we still catch real bugs in our
# shim, while not getting blocked by FreeBSD style choices that GCC
# under -Wall -Wextra would flag.
$(BUILD_DIR)/ridux_freebsd_glue.o: src/freebsd_compat/ridux_freebsd_glue.c \
		src/freebsd_compat/sys/proc.h \
		src/freebsd_compat/sys/malloc.h \
		src/freebsd_compat/sys/lock.h \
		src/freebsd_compat/sys/mutex.h \
		src/freebsd_compat/sys/sx.h \
		src/freebsd_compat/sys/ktr.h \
		src/compat/base.h src/compat/memory_tasks.h src/compat/user_libc.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LINUXULATOR_CFLAGS) -c $< -o $@

$(BUILD_DIR)/linux_errno.o: src/linuxulator/linux_errno.c \
		src/freebsd_compat/sys/param.h \
		src/freebsd_compat/sys/systm.h \
		src/freebsd_compat/sys/errno.h \
		src/freebsd_compat/compat/linux/linux_errno.h \
		src/freebsd_compat/compat/linux/linux_errno_table.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LINUXULATOR_CFLAGS) -Wno-unused-variable -Wno-unused-function -c $< -o $@

$(BUILD_DIR)/linux_rseq.o: src/linuxulator/linux_rseq.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LINUXULATOR_CFLAGS) -Wno-unused-variable -Wno-unused-function -Wno-attributes -c $< -o $@

$(BUILD_DIR)/linux_emul.o: src/linuxulator/linux_emul.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LINUXULATOR_CFLAGS) -Wno-unused-variable -Wno-unused-function -Wno-attributes -Wno-builtin-declaration-mismatch -c $< -o $@

$(BUILD_DIR)/kernel.o: src/kernel.c $(KERNEL_PARTS) src/flush.h src/ridux_r3wm.h src/compat/base.h src/compat/memory_tasks.h src/compat/linux_syscalls.h src/compat/user_libc.h src/compat/bsd_libc.h src/compat/linux_abi.h src/compat/display_wayland.h src/compat/browser_runtime.h $(ASSETS_HEADER) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(BUILD_DIR) -Isrc -c $< -o $@

$(KERNEL_BIN): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	grub-file --is-x86-multiboot2 $@

$(ROOTFS_DIR)/bin:
	mkdir -p $(ROOTFS_DIR)/bin

$(DEMO_ELF): tools/user_demo.S | $(BUILD_DIR) $(ROOTFS_DIR)/bin
	$(CC) -m32 -ffreestanding -fno-pie -c tools/user_demo.S -o $(BUILD_DIR)/user_demo.o
	$(LD) -m elf_i386 -nostdlib -Ttext 0x00200000 -e _start -o $@ $(BUILD_DIR)/user_demo.o

$(ABI_SMOKE_ELF): tools/user_abi_smoke.S | $(BUILD_DIR) $(ROOTFS_DIR)/bin
	$(CC) -m64 -ffreestanding -fno-pie -c tools/user_abi_smoke.S -o $(BUILD_DIR)/user_abi_smoke.o
	$(LD) -m elf_x86_64 -nostdlib -Ttext 0x00480000 -e _start -o $@ $(BUILD_DIR)/user_abi_smoke.o

$(X11_SMOKE_ELF): tools/user_x11_smoke.S | $(BUILD_DIR) $(ROOTFS_DIR)/bin
	$(CC) -m64 -ffreestanding -fno-pie -c tools/user_x11_smoke.S -o $(BUILD_DIR)/user_x11_smoke.o
	$(LD) -m elf_x86_64 -nostdlib -Ttext 0x00490000 -e _start -o $@ $(BUILD_DIR)/user_x11_smoke.o

$(BROWSER64_ELF): tools/user_browser64.S | $(BUILD_DIR) $(ROOTFS_DIR)/bin
	$(CC) -m64 -ffreestanding -fno-pie -c tools/user_browser64.S -o $(BUILD_DIR)/user_browser64.o
	$(LD) -m elf_x86_64 -nostdlib -Ttext 0x00400000 -e _start -o $@ $(BUILD_DIR)/user_browser64.o

$(FIREFOX64_ELF): $(BROWSER64_ELF) | $(ROOTFS_DIR)/bin
	cp $(BROWSER64_ELF) $@

# Notes Ring 3 demo: freestanding C app linked against libridux SDK.
# All three sources go through the same freestanding -m64 flags as the
# kernel so we can use mcmodel=large; linked at 0x004A0000 to avoid the
# other smoke-test ELFs and to keep mappings inside the user-space
# region the elf64 loader services.
USER_R3_CFLAGS := -m64 -ffreestanding -fno-pie -fno-stack-protector \
                  -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -msoft-float \
                  -fno-tree-vectorize -O2 -Wall -Wextra \
                  -Wno-unused-parameter -std=gnu11 -nostdlib -I$(BUILD_DIR)

$(BUILD_DIR)/user_crt0_r3.o: tools/user_crt0_r3.S | $(BUILD_DIR)
	$(CC) -m64 -ffreestanding -fno-pie -c $< -o $@

$(BUILD_DIR)/user_libridux.o: tools/user_libridux.c tools/user_libridux.h $(FONT_HEADER) | $(BUILD_DIR)
	$(CC) $(USER_R3_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user_notes_r3.o: tools/user_notes_r3.c tools/user_libridux.h | $(BUILD_DIR)
	$(CC) $(USER_R3_CFLAGS) -c $< -o $@

$(NOTES_R3_ELF): $(BUILD_DIR)/user_crt0_r3.o $(BUILD_DIR)/user_libridux.o $(BUILD_DIR)/user_notes_r3.o | $(ROOTFS_DIR)/bin
	$(LD) -m elf_x86_64 -nostdlib -Ttext 0x004A0000 -e _start -o $@ \
		$(BUILD_DIR)/user_crt0_r3.o \
		$(BUILD_DIR)/user_notes_r3.o \
		$(BUILD_DIR)/user_libridux.o

# Paquete de apps Ring 3: misma base visual, binarios separados.
# Asi cada tile del launcher termina en un proceso CPL=3 real.
R3_APP_ELFS := \
	$(ROOTFS_DIR)/bin/desktop-shell-r3.elf \
	$(ROOTFS_DIR)/bin/terminal-r3.elf \
	$(ROOTFS_DIR)/bin/files-r3.elf \
	$(ROOTFS_DIR)/bin/settings-r3.elf \
	$(ROOTFS_DIR)/bin/calculator-r3.elf \
	$(ROOTFS_DIR)/bin/clock-r3.elf \
	$(ROOTFS_DIR)/bin/paint-r3.elf \
	$(ROOTFS_DIR)/bin/taskmgr-r3.elf \
	$(ROOTFS_DIR)/bin/browser-r3.elf \
	$(ROOTFS_DIR)/bin/firefox-ui-r3.elf \
	$(ROOTFS_DIR)/bin/weather-r3.elf \
	$(ROOTFS_DIR)/bin/store-r3.elf \
	$(ROOTFS_DIR)/bin/about-r3.elf \
	$(ROOTFS_DIR)/bin/media-r3.elf \
	$(ROOTFS_DIR)/bin/monitor-r3.elf \
	$(ROOTFS_DIR)/bin/flush-r3.elf \
	$(ROOTFS_DIR)/bin/editor-r3.elf \
	$(ROOTFS_DIR)/bin/minesweeper-r3.elf \
	$(ROOTFS_DIR)/bin/snake-r3.elf \
	$(ROOTFS_DIR)/bin/logviewer-r3.elf \
	$(ROOTFS_DIR)/bin/network-r3.elf \
	$(ROOTFS_DIR)/bin/processes-r3.elf \
	$(ROOTFS_DIR)/bin/sysinfo-r3.elf \
	$(ROOTFS_DIR)/bin/tictactoe-r3.elf \
	$(ROOTFS_DIR)/bin/ring3demo-r3.elf

$(BUILD_DIR)/user_desktop_shell_r3.o: tools/user_desktop_shell_r3.c tools/user_libridux.h $(ASSETS_HEADER) | $(BUILD_DIR)
	$(CC) $(USER_R3_CFLAGS) -c $< -o $@

$(ROOTFS_DIR)/bin/desktop-shell-r3.elf: $(BUILD_DIR)/user_crt0_r3.o $(BUILD_DIR)/user_libridux.o $(BUILD_DIR)/user_desktop_shell_r3.o | $(ROOTFS_DIR)/bin
	$(LD) -m elf_x86_64 -nostdlib -Ttext 0x004A0000 -e _start -o $@ \
		$(BUILD_DIR)/user_crt0_r3.o \
		$(BUILD_DIR)/user_desktop_shell_r3.o \
		$(BUILD_DIR)/user_libridux.o

define R3_APP_RULE
$(BUILD_DIR)/user_$(1)_r3.o: tools/user_ridux_app_r3.c tools/user_libridux.h | $(BUILD_DIR)
	$$(CC) $$(USER_R3_CFLAGS) -DRIDUX_APP_KIND=$(2) -c $$< -o $$@

$(ROOTFS_DIR)/bin/$(1)-r3.elf: $(BUILD_DIR)/user_crt0_r3.o $(BUILD_DIR)/user_libridux.o $(BUILD_DIR)/user_$(1)_r3.o | $(ROOTFS_DIR)/bin
	$$(LD) -m elf_x86_64 -nostdlib -Ttext 0x004A0000 -e _start -o $$@ \
		$(BUILD_DIR)/user_crt0_r3.o \
		$(BUILD_DIR)/user_$(1)_r3.o \
		$(BUILD_DIR)/user_libridux.o
endef

$(eval $(call R3_APP_RULE,terminal,1))
$(eval $(call R3_APP_RULE,files,2))
$(eval $(call R3_APP_RULE,settings,3))
$(eval $(call R3_APP_RULE,calculator,4))
$(eval $(call R3_APP_RULE,clock,5))
$(eval $(call R3_APP_RULE,paint,6))
$(eval $(call R3_APP_RULE,taskmgr,7))
$(eval $(call R3_APP_RULE,browser,8))
$(eval $(call R3_APP_RULE,firefox-ui,9))
$(eval $(call R3_APP_RULE,weather,10))
$(eval $(call R3_APP_RULE,store,11))
$(eval $(call R3_APP_RULE,about,12))
$(eval $(call R3_APP_RULE,media,13))
$(eval $(call R3_APP_RULE,monitor,14))
$(eval $(call R3_APP_RULE,flush,15))
$(eval $(call R3_APP_RULE,editor,16))
$(eval $(call R3_APP_RULE,minesweeper,17))
$(eval $(call R3_APP_RULE,snake,18))
$(eval $(call R3_APP_RULE,logviewer,19))
$(eval $(call R3_APP_RULE,network,20))
$(eval $(call R3_APP_RULE,processes,21))
$(eval $(call R3_APP_RULE,sysinfo,22))
$(eval $(call R3_APP_RULE,tictactoe,23))
$(eval $(call R3_APP_RULE,ring3demo,25))

r3-apps-only: $(R3_APP_ELFS)

$(INITRD_OVERLAY_IMG): $(R3_APP_ELFS) $(NOTES_R3_ELF)
	mkdir -p $(BUILD_DIR)
	rm -f $@.tmp
	tar --format=ustar -cf $@.tmp -C $(ROOTFS_DIR) \
		bin/desktop-shell-r3.elf \
		bin/terminal-r3.elf \
		bin/files-r3.elf \
		bin/settings-r3.elf \
		bin/calculator-r3.elf \
		bin/clock-r3.elf \
		bin/paint-r3.elf \
		bin/taskmgr-r3.elf \
		bin/browser-r3.elf \
		bin/firefox-ui-r3.elf \
		bin/weather-r3.elf \
		bin/store-r3.elf \
		bin/about-r3.elf \
		bin/media-r3.elf \
		bin/monitor-r3.elf \
		bin/flush-r3.elf \
		bin/editor-r3.elf \
		bin/minesweeper-r3.elf \
		bin/snake-r3.elf \
		bin/logviewer-r3.elf \
		bin/network-r3.elf \
		bin/processes-r3.elf \
		bin/sysinfo-r3.elf \
		bin/tictactoe-r3.elf \
		bin/ring3demo-r3.elf \
		bin/notes-r3.elf
	mv $@.tmp $@

$(STACKCHK_TRACE_SO): tools/stackchk_trace.c tools/stackchk_trace.map
	mkdir -p $(ROOTFS_DIR)/opt/ridux
	$(CC) -shared -fPIC -O2 -fno-stack-protector -nostdlib \
		-Wl,--version-script=tools/stackchk_trace.map \
		-o $@ tools/stackchk_trace.c

$(INITRD_IMG): glib-compat-patch chromium-compat-patch fontconfig-compat-patch $(ROOTFS_DIR) $(ROOTFS_DIR)/etc/autoboot.cmd $(DEMO_ELF) $(ABI_SMOKE_ELF) $(X11_SMOKE_ELF) $(BROWSER64_ELF) $(FIREFOX64_ELF) $(NOTES_R3_ELF) $(R3_APP_ELFS) $(STACKCHK_TRACE_SO)
	mkdir -p $(BUILD_DIR)
	rm -f $@.tmp
	tar --format=ustar -cf $@.tmp -C $(ROOTFS_DIR) .
	mv $@.tmp $@

$(ISO): $(KERNEL_BIN) $(INITRD_IMG) grub/grub.cfg
	rm -rf $(ISO_DIR)
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL_BIN) $(ISO_DIR)/boot/ridux-kernel.bin
	cp $(INITRD_IMG) $(ISO_DIR)/boot/initrd.img
	if [ -f "$(BUILD_DIR)/browser-vm/kernel-x86_64-pc99" ] && [ -f "$(BUILD_DIR)/browser-vm/capdl-loader-image-x86_64-pc99" ]; then \
		mkdir -p $(ISO_DIR)/boot/sel4; \
		cp "$(BUILD_DIR)/browser-vm/kernel-x86_64-pc99" $(ISO_DIR)/boot/sel4/kernel-x86_64-pc99; \
		cp "$(BUILD_DIR)/browser-vm/capdl-loader-image-x86_64-pc99" $(ISO_DIR)/boot/sel4/capdl-loader-image-x86_64-pc99; \
	fi
	cp grub/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	rm -f $@.tmp
	grub-mkrescue -o $@.tmp $(ISO_DIR) > /dev/null 2>&1
	mv $@.tmp $@

iso-from-existing-initrd: $(KERNEL_BIN) $(INITRD_OVERLAY_IMG) grub/grub.cfg
	test -f $(INITRD_IMG)
	test $$(stat -c%s $(INITRD_IMG)) -gt 104857600
	rm -rf $(ISO_DIR)
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL_BIN) $(ISO_DIR)/boot/ridux-kernel.bin
	cp $(INITRD_IMG) $(ISO_DIR)/boot/initrd.img
	cp $(INITRD_OVERLAY_IMG) $(ISO_DIR)/boot/initrd-overlay.img
	cp grub/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	rm -f $(ISO).tmp
	grub-mkrescue -o $(ISO).tmp $(ISO_DIR) > /dev/null 2>&1
	mv $(ISO).tmp $(ISO)

clean:
	rm -rf $(BUILD_DIR)

run:
	@echo "ISO generated at $(ISO)"

chromium-rootfs:
	bash tools/package_chromium_rootfs.sh "$(ROOTFS_DIR)"

firefox-rootfs:
	bash tools/package_firefox_rootfs.sh "$(ROOTFS_DIR)"

browsers-rootfs: chromium-rootfs firefox-rootfs

vendor-upstream:
	bash tools/vendor_upstream.sh network

vendor-check:
	bash tools/license_guard.sh

freebsd-bootstrap:
	bash tools/bootstrap_freebsd_src.sh "$(FREEBSD_SRC_DIR)" "$(FREEBSD_REF)"

freebsd-prepare:
	bash tools/setup_freebsd_integration.sh "$(FREEBSD_SRC_DIR)"

# Ridux production ISO. Builds a ready-to-run Ridux distribution: a
# Ridux kernel image with the Ridux Linux ABI runtime enabled by
# default, the Ridux UI, and Firefox/Chromium installed at first boot.
ridux-iso freebsd-browser-iso:
	bash tools/build_freebsd_browser_iso.sh --output "$(BUILD_DIR)/RiduxOS-Browser.iso"

ridux-iso-fast freebsd-browser-iso-fast:
	bash tools/build_freebsd_browser_iso.sh --fast --output "$(BUILD_DIR)/RiduxOS-Browser.iso"

ridux-vm freebsd-browser-vm: ridux-iso
	powershell -ExecutionPolicy Bypass -File .\scripts\boot-freebsd-browser-vm.ps1 -IsoPath ".\build\RiduxOS-Browser.iso"

ridux-vm-quick freebsd-browser-vm-quick:
	powershell -ExecutionPolicy Bypass -File .\scripts\boot-freebsd-browser-vm.ps1 -QuickBoot

freebsd-compat-snapshot:
	bash tools/export_ridux_compat_snapshot.sh

freebsd-ridux-kernel:
	bash tools/build_freebsd_ridux_kernel.sh --src "$(FREEBSD_SRC_DIR)" --kernconf RIDUX --output "$(BUILD_DIR)/freebsd-kernel/RIDUX/kernel"

# Ridux Live ISO. Boots directly into Ridux UI from removable media.
# No installer, no FreeBSD branding visible to the user, no manual login.
# This is the default ISO most end users should run; the installer ISO
# (ridux-iso) is for users who want to install Ridux to a disk.
ridux-live-iso freebsd-live-iso:
	bash tools/build_freebsd_live_iso.sh --output "$(BUILD_DIR)/RiduxOS-Live.iso"

ridux-live-vm freebsd-live-vm: ridux-live-iso
	powershell -ExecutionPolicy Bypass -File .\scripts\boot-freebsd-live-vm.ps1 -IsoPath ".\build\RiduxOS-Live.iso"

# Same as live ISO, but forces embedding a prebuilt custom FreeBSD kernel
# from build/freebsd-kernel/RIDUX/kernel (generated by freebsd-ridux-kernel).
ridux-live-iso-kernel freebsd-live-iso-kernel:
	bash tools/build_freebsd_live_iso.sh --ridux-kernel "$(BUILD_DIR)/freebsd-kernel/RIDUX/kernel" --output "$(BUILD_DIR)/RiduxOS-Live.iso"

ridux-live-vm-kernel freebsd-live-vm-kernel: ridux-live-iso-kernel
	powershell -ExecutionPolicy Bypass -File .\scripts\boot-freebsd-live-vm.ps1 -IsoPath ".\build\RiduxOS-Live.iso"

# "Fast start" variant: boots faster by deferring Chromium install to
# background. Firefox is ready immediately; Chromium becomes available
# a few minutes after the desktop is up.
ridux-live-iso-fast freebsd-live-iso-orbit:
	bash tools/build_freebsd_live_iso.sh --orbit-fast-start --output "$(BUILD_DIR)/RiduxOS-Live-Fast.iso"

ridux-live-vm-fast freebsd-live-vm-orbit: ridux-live-iso-fast
	powershell -ExecutionPolicy Bypass -File .\scripts\boot-freebsd-live-vm.ps1 -IsoPath ".\build\RiduxOS-Live-Fast.iso"

debian-netinst-iso:
	bash tools/download_debian_netinst_iso.sh --output "$(BUILD_DIR)/RiduxOS-Debian-Netinst.iso"

debian-runtime-seed-iso:
	bash tools/build_debian_runtime_seed_iso.sh --output "$(BUILD_DIR)/RiduxOS-Debian-Seed.iso"

debian-vm: debian-netinst-iso debian-runtime-seed-iso
	powershell -ExecutionPolicy Bypass -File .\scripts\boot-debian-ridux-vm.ps1 -IsoPath ".\build\RiduxOS-Debian-Netinst.iso" -SeedIsoPath ".\build\RiduxOS-Debian-Seed.iso"

debian-vm-quick:
	powershell -ExecutionPolicy Bypass -File .\scripts\boot-debian-ridux-vm.ps1 -QuickBoot -SeedIsoPath ".\build\RiduxOS-Debian-Seed.iso"

debian-live-ridux-iso:
	bash tools/build_debian_live_ridux_iso.sh --output "$(BUILD_DIR)/RiduxOS-Debian-Live-Ridux.iso"

debian-live-ridux-iso-fast:
	bash tools/build_debian_live_ridux_iso.sh --incremental --skip-host-deps --output "$(BUILD_DIR)/RiduxOS-Debian-Live-Ridux.iso"

debian-live-ridux-vm:
	powershell -ExecutionPolicy Bypass -File .\scripts\boot-debian-live-ridux-vm.ps1 -IsoPath ".\build\RiduxOS-Debian-Live-Ridux.iso"

debian-live-ridux-vm-fast: debian-live-ridux-iso-fast
	powershell -ExecutionPolicy Bypass -File .\scripts\boot-debian-live-ridux-vm.ps1 -IsoPath ".\build\RiduxOS-Debian-Live-Ridux.iso"

sel4-browser-vm-info:
	bash tools/bootstrap_sel4_browser_vm.sh info

sel4-browser-vm-deps:
	bash tools/bootstrap_sel4_browser_vm.sh deps

sel4-browser-vm-bootstrap:
	bash tools/bootstrap_sel4_browser_vm.sh fetch

sel4-browser-vm-build:
	bash tools/bootstrap_sel4_browser_vm.sh build

sel4-browser-vm-stage:
	bash tools/bootstrap_sel4_browser_vm.sh stage

sel4-browser-vm-iso: sel4-browser-vm-build all
