CC := gcc
CXX := g++
LD := ld
PYTHON := python3
XORRISO := xorriso
QT6_WIDGETS_CFLAGS := $(shell pkg-config --cflags Qt6Widgets Qt6Gui Qt6Core 2>/dev/null)
QT6_WIDGETS_LIBS := $(shell pkg-config --libs Qt6Widgets Qt6Gui Qt6Core 2>/dev/null)
QT6_GUI_CFLAGS := $(shell pkg-config --cflags Qt6Gui Qt6Core 2>/dev/null)
QT6_GUI_LIBS := $(shell pkg-config --libs Qt6Gui Qt6Core 2>/dev/null)
QT6_QUICK_CFLAGS := $(shell pkg-config --cflags Qt6Quick Qt6Qml Qt6Gui Qt6Core 2>/dev/null)
QT6_QUICK_LIBS := $(shell pkg-config --libs Qt6Quick Qt6Qml Qt6Gui Qt6Core 2>/dev/null)
QT6_DIRECT_CFLAGS := $(shell pkg-config --cflags Qt6OpenGLWidgets Qt6OpenGL Qt6Widgets Qt6Quick Qt6Qml Qt6Gui Qt6Core 2>/dev/null)
QT6_DIRECT_LIBS := $(shell pkg-config --libs Qt6OpenGLWidgets Qt6OpenGL Qt6Widgets Qt6Quick Qt6Qml Qt6Gui Qt6Core 2>/dev/null)
DESKTOP_GL_LIBS := $(shell pkg-config --libs gl 2>/dev/null)
ifeq ($(strip $(DESKTOP_GL_LIBS)),)
DESKTOP_GL_LIBS := -lGL
endif
MESA_GBM_CFLAGS := $(shell pkg-config --cflags egl glesv2 gbm libdrm 2>/dev/null)
MESA_GBM_LIBS := $(shell pkg-config --libs egl glesv2 gbm libdrm 2>/dev/null)
PANGO_CAIRO_CFLAGS := $(shell pkg-config --cflags pangocairo cairo pango 2>/dev/null)
PANGO_CAIRO_LIBS := $(shell pkg-config --libs pangocairo cairo pango 2>/dev/null)
VULKAN_CFLAGS := $(shell pkg-config --cflags vulkan 2>/dev/null)
VULKAN_LIBS := $(shell pkg-config --libs vulkan 2>/dev/null)
ifeq ($(strip $(VULKAN_LIBS)),)
VULKAN_LIBS := -lvulkan
endif

CFLAGS := -m64 -ffreestanding -fno-pie -fno-stack-protector -mno-red-zone -mcmodel=large -mno-mmx -mno-sse -mno-sse2 -msoft-float -fno-tree-vectorize -O2 -Wall -Wextra -Wno-unused-parameter -std=gnu11
RIDUX_STATIC_HELPER_CFLAGS := -O2 -Wall -Wextra -ffreestanding -fno-builtin -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -fno-stack-protector -fno-pie -no-pie -nostdlib -static
LDFLAGS := -m elf_x86_64 -nostdlib -T linker.ld

BUILD_DIR := build
ISO_DIR := $(BUILD_DIR)/iso
KERNEL_BIN := $(BUILD_DIR)/ridux-kernel.bin
INITRD_IMG := $(BUILD_DIR)/initrd.img
INITRD_OVERLAY_IMG := $(BUILD_DIR)/initrd-overlay.img
ROOTFS_DIR := rootfs
QT6_QML_SRC ?= $(shell if [ -d /usr/lib/x86_64-linux-gnu/qt6/qml/QtQuick ]; then printf '%s\n' /usr/lib/x86_64-linux-gnu/qt6/qml; elif [ -d $(ROOTFS_DIR)/opt/kde-plasma/usr/lib/x86_64-linux-gnu/qt6/qml/QtQuick ]; then printf '%s\n' $(ROOTFS_DIR)/opt/kde-plasma/usr/lib/x86_64-linux-gnu/qt6/qml; fi)
QT6_QML_ROOT := $(ROOTFS_DIR)/usr/lib/x86_64-linux-gnu/qt6/qml
QT6_QML_MARKER := $(QT6_QML_ROOT)/.ridux-qtquick-staged
FREEBSD_SRC_DIR := third_party/upstream/freebsd-src
FREEBSD_REF := stable/14
RIDUXBSD_ISO := $(BUILD_DIR)/RiduxOS-RIDUXBSD-amd64.iso
WAYFIRE_DIR := third_party/wayfire
WAYFIRE_REF ?= v0.10.0
WAYFIRE_BUILD_TARGET ?= all
WAYFIRE_PATCH_DIR ?= patches/wayfire
HYPRLAND_DIR := third_party/hyprland
HYPRLAND_REF ?= main
HYPRLAND_BUILD_TARGET ?= all
HYPR_DEPS_REF ?= main
HYPRWIRE_REF ?= main
GLSLANG_REF ?= main
THIRD_PARTY_DEPS_REF ?= main
WAYLAND_PROTOCOLS_REF ?= 1.47
XKBCOMMON_REF ?= master
LIBINPUT_REF ?= main
NWG_DOCK_HYPRLAND_REF ?= master
KDE_DIR := third_party/kde
KDE_INSTALL_DIR ?= $(KDE_DIR)/install
DEMO_ELF := $(ROOTFS_DIR)/bin/ridux-demo.elf
ABI_SMOKE_ELF := $(ROOTFS_DIR)/bin/abi-smoke.elf
X11_SMOKE_ELF := $(ROOTFS_DIR)/bin/x11-smoke.elf
BROWSER64_ELF := $(ROOTFS_DIR)/bin/chrome.elf
FIREFOX64_ELF := $(ROOTFS_DIR)/bin/firefox.elf
NOTES_R3_ELF  := $(ROOTFS_DIR)/bin/notes-r3.elf
RIDUX_SH_ELF := $(ROOTFS_DIR)/bin/sh
STACKCHK_TRACE_SO := $(ROOTFS_DIR)/opt/ridux/stackchk_trace.so
FIREFOX_PROBE_SO := $(ROOTFS_DIR)/opt/ridux/firefox_probe.so
RIDUX_GLIBC_PRIVATE_SHIM := $(ROOTFS_DIR)/lib64/ridux-glibc-private-shim.so
RIDUX_DBUS_SESSION := $(ROOTFS_DIR)/usr/bin/ridux-dbus-session
RIDUX_DBUS_SYSTEM := $(ROOTFS_DIR)/usr/bin/ridux-dbus-system
RIDUX_USER_SERVICES := $(ROOTFS_DIR)/usr/bin/ridux-user-services
RIDUX_WAYBAR_LAUNCHER := $(ROOTFS_DIR)/usr/bin/ridux-waybar
RIDUX_SESSION_SPAWN := $(ROOTFS_DIR)/usr/bin/ridux-session-spawn
RIDUX_APP_LAUNCHER := $(ROOTFS_DIR)/usr/bin/ridux-app-launcher
RIDUX_APP_WRAPPERS := \
	$(ROOTFS_DIR)/usr/bin/ridux-open-files \
	$(ROOTFS_DIR)/usr/bin/ridux-terminal \
	$(ROOTFS_DIR)/usr/bin/ridux-display-settings \
	$(ROOTFS_DIR)/usr/bin/ridux-power-menu
RIDUX_NATIVE_SHELL := $(ROOTFS_DIR)/bin/desktop-shell-r3.elf
RIDUX_QT_SHELL := $(ROOTFS_DIR)/usr/bin/ridux-shell
RIDUX_QT_DIRECT_SHELL := $(ROOTFS_DIR)/usr/bin/ridux-shell-direct
RIDUX_QT_FREEGUARD := $(ROOTFS_DIR)/lib64/ridux-qt-freeguard.so
RIDUX_UI_SHELL := $(ROOTFS_DIR)/usr/bin/ridux-ui-shell
INJURY_SHELL := $(ROOTFS_DIR)/usr/bin/injury-shell
INJURY_COMPOSITOR := $(ROOTFS_DIR)/usr/bin/injury-compositor
RIDUX_UI_OBJ := $(BUILD_DIR)/ridux_native_ui.o
RIDUX_GL_COMPOSITOR := $(ROOTFS_DIR)/usr/bin/ridux-gl-compositor
RIDUX_VULKAN_PROBE := $(ROOTFS_DIR)/usr/bin/ridux-vulkan-probe
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
	$(BUILD_DIR)/compat_app_profiles.o \
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

.PHONY: all kernel-only ridux-native-shell ridux-ui-shell ridux-qt-shell ridux-qt-direct-shell ridux-gl-compositor ridux-vulkan-probe r3-apps-only iso-from-existing-initrd iso-xorriso-update clean run glib-compat-patch chromium-compat-patch fontconfig-compat-patch chromium-rootfs firefox-rootfs browsers-rootfs wayfire-source wayfire-patches wayfire-build wayfire-rootfs wayfire-desktop hyprland-source hyprland-build hyprland-rootfs hyprland-desktop kde-source kde-build kde-plasma-build kde-rootfs kde-plasma-source-rootfs plasma-rootfs plasma-source-rootfs plasma-desktop vendor-upstream vendor-check freebsd-bootstrap freebsd-prepare riduxbsd-prepare riduxbsd-world riduxbsd-iso riduxbsd-iso-dry-run riduxbsd-clean-live-artifacts freebsd-browser-iso freebsd-browser-iso-fast freebsd-browser-vm freebsd-browser-vm-quick freebsd-compat-snapshot freebsd-ridux-kernel freebsd-live-iso freebsd-live-vm freebsd-live-iso-kernel freebsd-live-vm-kernel freebsd-live-iso-orbit freebsd-live-vm-orbit freebsd-wayfire-pkgs freebsd-wayfire-iso freebsd-wayfire-iso-offline freebsd-wayfire-iso-kernel freebsd-wayfire-vm freebsd-wayfire-vm-kernel ridux-wayfire-iso ridux-wayfire-vm debian-netinst-iso debian-runtime-seed-iso debian-vm debian-vm-quick debian-live-ridux-iso debian-live-ridux-iso-fast debian-live-ridux-vm debian-live-ridux-vm-fast sel4-browser-vm-info sel4-browser-vm-deps sel4-browser-vm-bootstrap sel4-browser-vm-build sel4-browser-vm-stage sel4-browser-vm-iso

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

$(BUILD_DIR)/compat4.o: src/compat/user_libc.c src/compat/user_libc_drm.inc src/compat/user_libc.h src/compat/linux_syscalls.h src/compat/memory_tasks.h src/compat/base.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(BUILD_DIR) -Isrc -c $< -o $@

$(BUILD_DIR)/compat_app_profiles.o: src/compat/linux_app_profiles.c src/compat/linux_app_profiles.h src/compat/user_libc.h src/compat/linux_syscalls.h src/compat/memory_tasks.h src/compat/base.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(BUILD_DIR) -Isrc -c $< -o $@

$(BUILD_DIR)/compat5.o: src/compat/bsd_libc.c src/compat/bsd_libc.h src/compat/linux_app_profiles.h src/compat/user_libc.h src/compat/linux_syscalls.h src/compat/memory_tasks.h src/compat/base.h | $(BUILD_DIR)
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

$(ROOTFS_DIR)/usr/bin:
	mkdir -p $(ROOTFS_DIR)/usr/bin

$(ROOTFS_DIR)/usr/bin/systemctl: tools/ridux_systemctl_shim.c | $(ROOTFS_DIR)/usr/bin
	$(CC) -O2 -Wall -Wextra -ffreestanding -fno-builtin \
		-mno-red-zone -mno-sse -mno-sse2 -mno-mmx \
		-fno-stack-protector -fno-pie -no-pie -nostdlib -static \
		-Wl,-e,_start \
		-o $@ $<

$(RIDUX_DBUS_SESSION): tools/ridux_dbus_session.c | $(ROOTFS_DIR)/usr/bin
	$(CC) -O2 -Wall -Wextra -ffreestanding -fno-builtin \
		-mno-red-zone -mno-sse -mno-sse2 -mno-mmx \
		-fno-stack-protector -fno-pie -no-pie -nostdlib -static \
		-Wl,-e,_start \
		-o $@ $<

$(RIDUX_DBUS_SYSTEM): tools/ridux_dbus_system.c | $(ROOTFS_DIR)/usr/bin
	$(CC) -O2 -Wall -Wextra -ffreestanding -fno-builtin \
		-mno-red-zone -mno-sse -mno-sse2 -mno-mmx \
		-fno-stack-protector -fno-pie -no-pie -nostdlib -static \
		-Wl,-e,_start \
		-o $@ $<

$(RIDUX_USER_SERVICES): tools/ridux_user_services.c | $(ROOTFS_DIR)/usr/bin
	$(CC) $(RIDUX_STATIC_HELPER_CFLAGS) \
		-Wl,-e,_start \
		-o $@ $<

$(RIDUX_WAYBAR_LAUNCHER): tools/ridux_waybar_launcher.c | $(ROOTFS_DIR)/usr/bin
	$(CC) $(RIDUX_STATIC_HELPER_CFLAGS) \
		-Wl,-e,_start \
		-o $@ $<

$(RIDUX_SESSION_SPAWN): tools/ridux_session_spawn.c | $(ROOTFS_DIR)/usr/bin
	$(CC) $(RIDUX_STATIC_HELPER_CFLAGS) \
		-Wl,-e,_start \
		-o $@ $<

$(RIDUX_APP_LAUNCHER): tools/ridux_app_launcher.c | $(ROOTFS_DIR)/usr/bin
	$(CC) $(RIDUX_STATIC_HELPER_CFLAGS) \
		-Wl,-e,_start \
		-o $@ $<

$(RIDUX_APP_WRAPPERS): $(RIDUX_APP_LAUNCHER) | $(ROOTFS_DIR)/usr/bin
	cp -f $< $@
	chmod 0755 $@

$(RIDUX_QT_SHELL): tools/ridux_shell_qt.cpp | $(ROOTFS_DIR)/usr/bin
	@if [ -z "$(QT6_WIDGETS_CFLAGS)$(QT6_WIDGETS_LIBS)" ]; then \
		echo "Qt 6 development files not found. Install Qt6Widgets/Qt6Gui/Qt6Core pkg-config files."; \
		exit 1; \
	fi
	$(CXX) -O2 -Wall -Wextra -std=c++17 -fno-omit-frame-pointer \
		$(QT6_WIDGETS_CFLAGS) \
		-o $@ $< \
		$(QT6_WIDGETS_LIBS) $(DESKTOP_GL_LIBS)
	ln -sf ridux-shell $(ROOTFS_DIR)/usr/bin/ridux-panel
	ln -sf ridux-shell $(ROOTFS_DIR)/usr/bin/ridux-dock
	ln -sf ridux-shell $(ROOTFS_DIR)/usr/bin/ridux-dashboard
	ln -sf ridux-shell $(ROOTFS_DIR)/usr/bin/ridux-files-qt
	ln -sf ridux-shell $(ROOTFS_DIR)/usr/bin/ridux-monitor-qt

$(QT6_QML_MARKER):
	@if [ -z "$(QT6_QML_SRC)" ]; then \
		echo "Qt Quick QML modules not found. Install qt6-declarative packages or stage QtQuick under rootfs/opt/kde-plasma."; \
		exit 1; \
	fi
	mkdir -p $(QT6_QML_ROOT)
	rm -rf $(QT6_QML_ROOT)/QtQuick $(QT6_QML_ROOT)/QtQml $(QT6_QML_ROOT)/QtCore
	cp -a "$(QT6_QML_SRC)/QtQuick" $(QT6_QML_ROOT)/
	if [ -d "$(QT6_QML_SRC)/QtQml" ]; then cp -a "$(QT6_QML_SRC)/QtQml" $(QT6_QML_ROOT)/; fi
	if [ -d "$(QT6_QML_SRC)/QtCore" ]; then cp -a "$(QT6_QML_SRC)/QtCore" $(QT6_QML_ROOT)/; fi
	find $(QT6_QML_ROOT) -name qmldir -exec sed -i '/^prefer :\/qt-project\.org\/imports\//d' {} +
	touch $@

$(RIDUX_QT_DIRECT_SHELL): tools/ridux_shell_qt.cpp $(ROOTFS_DIR)/usr/share/riduxui/ridux-shell.qml $(QT6_QML_MARKER) | $(ROOTFS_DIR)/usr/bin
	@if [ -z "$(QT6_DIRECT_CFLAGS)$(QT6_DIRECT_LIBS)" ]; then \
		echo "Qt 6 direct shell development files not found. Install Qt6OpenGLWidgets/Qt6Widgets/Qt6Quick pkg-config files."; \
		exit 1; \
	fi
	$(CXX) -O2 -Wall -Wextra -std=c++17 -fno-omit-frame-pointer \
		-DRIDUX_DIRECT_ONLY \
		$(QT6_DIRECT_CFLAGS) \
		-o $@ $< \
		$(QT6_DIRECT_LIBS) $(DESKTOP_GL_LIBS)

ridux-qt-direct-shell: $(RIDUX_QT_DIRECT_SHELL)

ridux-qt-shell: $(RIDUX_QT_SHELL) $(RIDUX_QT_DIRECT_SHELL)

$(RIDUX_UI_OBJ): tools/ridux_native_ui.c tools/ridux_native_ui.h | $(BUILD_DIR)
	@if [ -z "$(MESA_GBM_CFLAGS)$(MESA_GBM_LIBS)" ]; then \
		echo "Mesa EGL/GLES/GBM/libdrm development files not found."; \
		exit 1; \
	fi
	@if [ -z "$(PANGO_CAIRO_CFLAGS)$(PANGO_CAIRO_LIBS)" ]; then \
		echo "Pango/Cairo development files not found."; \
		exit 1; \
	fi
	$(CC) -O2 -Wall -Wextra -std=gnu11 -fno-stack-protector -fno-pie -no-pie \
		$(MESA_GBM_CFLAGS) $(PANGO_CAIRO_CFLAGS) \
		-c $< -o $@

$(RIDUX_UI_SHELL): tools/ridux_ui_shell.cpp tools/ridux_native_ui.h tools/ridux_surface_protocol.h tools/ridux_ui_components.hpp tools/injury_shell.hpp tools/injury_client_runtime.hpp tools/ridux_compositor.hpp tools/ridux_adwaita_theme.hpp $(ASSETS_HEADER) $(RIDUX_UI_OBJ) | $(ROOTFS_DIR)/usr/bin
	$(CXX) -O2 -Wall -Wextra -std=c++17 -fno-stack-protector -fno-pie -no-pie \
		-I$(BUILD_DIR) $(MESA_GBM_CFLAGS) $(PANGO_CAIRO_CFLAGS) \
		-Wl,--enable-new-dtags -Wl,-rpath,/lib64 -Wl,-rpath,/usr/lib/x86_64-linux-gnu \
		-o $@ tools/ridux_ui_shell.cpp $(RIDUX_UI_OBJ) \
		$(MESA_GBM_LIBS) $(PANGO_CAIRO_LIBS)
	cp $@ $(INJURY_SHELL)
	cp $@ $(INJURY_COMPOSITOR)

ridux-ui-shell: $(RIDUX_UI_SHELL)

$(RIDUX_QT_FREEGUARD): tools/wayfire/ridux_qt_freeguard.c
	mkdir -p $(ROOTFS_DIR)/lib64 $(ROOTFS_DIR)/opt/wayfire/lib
	$(CC) -shared -fPIC -O2 -fno-stack-protector \
		-o $@ $< -ldl -pthread
	cp $@ $(ROOTFS_DIR)/opt/wayfire/lib/ridux-qt-freeguard.so

$(RIDUX_GL_COMPOSITOR): tools/ridux_gl_compositor.c | $(ROOTFS_DIR)/usr/bin
	@if [ -z "$(MESA_GBM_CFLAGS)$(MESA_GBM_LIBS)" ]; then \
		echo "Mesa EGL/GLES/GBM/libdrm development files not found."; \
		exit 1; \
	fi
	$(CC) -O2 -Wall -Wextra -std=gnu11 -fno-stack-protector -fno-pie -no-pie \
		$(MESA_GBM_CFLAGS) \
		-Wl,--enable-new-dtags -Wl,-rpath,/lib64 -Wl,-rpath,/usr/lib/x86_64-linux-gnu \
		-o $@ $< \
		$(MESA_GBM_LIBS)

ridux-gl-compositor: $(RIDUX_GL_COMPOSITOR)

$(RIDUX_VULKAN_PROBE): tools/ridux_vulkan_probe.c | $(ROOTFS_DIR)/usr/bin
	@if [ -z "$(VULKAN_LIBS)" ]; then \
		echo "Vulkan development files not found."; \
		exit 1; \
	fi
	$(CC) -O2 -Wall -Wextra -std=gnu11 -fno-builtin -fno-stack-protector -fno-pie -no-pie \
		$(VULKAN_CFLAGS) \
		-Wl,--enable-new-dtags -Wl,-rpath,/lib64 -Wl,-rpath,/usr/lib/x86_64-linux-gnu \
		-o $@ $< \
		$(VULKAN_LIBS)

ridux-vulkan-probe: $(RIDUX_VULKAN_PROBE)

ridux-native-shell: $(RIDUX_NATIVE_SHELL)

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

$(RIDUX_SH_ELF): tools/ridux_sh.c | $(ROOTFS_DIR)/bin
	$(CC) -m64 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector \
		-mno-red-zone -O2 -Wall -Wextra -nostdlib -no-pie \
		-Wl,--build-id=none -Wl,-Ttext=0x00401000 -Wl,-e,_start -o $@ $<

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

$(BUILD_DIR)/user_desktop_shell_r3.o: tools/user_desktop_shell_r3.c tools/user_libridux.h tools/ridux_ui.h $(ASSETS_HEADER) | $(BUILD_DIR)
	$(CC) $(USER_R3_CFLAGS) -c $< -o $@

$(ROOTFS_DIR)/bin/desktop-shell-r3.elf: $(BUILD_DIR)/user_crt0_r3.o $(BUILD_DIR)/user_libridux.o $(BUILD_DIR)/user_desktop_shell_r3.o | $(ROOTFS_DIR)/bin
	$(LD) -m elf_x86_64 -nostdlib -Ttext 0x004A0000 -e _start -o $@ \
		$(BUILD_DIR)/user_crt0_r3.o \
		$(BUILD_DIR)/user_desktop_shell_r3.o \
		$(BUILD_DIR)/user_libridux.o

define R3_APP_RULE
$(BUILD_DIR)/user_$(1)_r3.o: tools/user_ridux_app_r3.c tools/user_libridux.h tools/ridux_ui.h $(ASSETS_HEADER) | $(BUILD_DIR)
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

r3-apps-only: $(R3_APP_ELFS) $(RIDUX_SH_ELF)

$(INJURY_SHELL): $(RIDUX_UI_SHELL) | $(ROOTFS_DIR)/usr/bin
	cp $(RIDUX_UI_SHELL) $@

$(INJURY_COMPOSITOR): $(RIDUX_UI_SHELL) | $(ROOTFS_DIR)/usr/bin
	cp $(RIDUX_UI_SHELL) $@

$(INITRD_OVERLAY_IMG): $(RIDUX_SH_ELF) $(R3_APP_ELFS) $(RIDUX_UI_SHELL) $(INJURY_SHELL) $(INJURY_COMPOSITOR) $(RIDUX_GL_COMPOSITOR) $(RIDUX_VULKAN_PROBE) $(RIDUX_GLIBC_PRIVATE_SHIM) $(RIDUX_DBUS_SESSION) $(RIDUX_DBUS_SYSTEM) $(RIDUX_USER_SERVICES) $(RIDUX_WAYBAR_LAUNCHER) $(RIDUX_SESSION_SPAWN) $(RIDUX_APP_LAUNCHER) $(RIDUX_APP_WRAPPERS) $(ROOTFS_DIR)/etc/autoboot.cmd \
	$(wildcard $(ROOTFS_DIR)/usr/share/riduxui/*) \
	$(ROOTFS_DIR)/usr/share/icons/Adwaita/cursor.theme \
	$(wildcard $(ROOTFS_DIR)/usr/share/icons/Adwaita/cursors/*) \
	$(wildcard $(ROOTFS_DIR)/usr/share/icons/hicolor/*) \
	$(wildcard $(ROOTFS_DIR)/usr/share/glvnd/egl_vendor.d/*) \
	$(wildcard $(ROOTFS_DIR)/usr/share/vulkan/icd.d/*) \
	$(wildcard $(ROOTFS_DIR)/usr/share/drirc.d/*) \
	$(wildcard $(ROOTFS_DIR)/etc/profile.d/*) \
	$(ROOTFS_DIR)/etc/passwd \
	$(ROOTFS_DIR)/etc/group \
	$(ROOTFS_DIR)/etc/nsswitch.conf \
	$(ROOTFS_DIR)/etc/fonts/fonts.conf \
	$(ROOTFS_DIR)/etc/fonts/fonts-ridux.conf \
	$(wildcard $(ROOTFS_DIR)/tmp/fontconfig-cache) \
	$(wildcard $(ROOTFS_DIR)/var/cache/fontconfig) \
	$(ROOTFS_DIR)/etc/ridux-ui-shell.enable \
	$(ROOTFS_DIR)/etc/ridux-gl-compositor.enable \
	$(ROOTFS_DIR)/etc/ridux-gpu-production.enable \
	$(ROOTFS_DIR)/etc/ridux-physical-gpu-preferred.enable \
	$(ROOTFS_DIR)/etc/ridux-gpu-intel.enable \
	$(ROOTFS_DIR)/etc/ridux-virtgpu-venus.enable \
	$(ROOTFS_DIR)/etc/ridux-vulkan-virtio-only.enable \
	$(ROOTFS_DIR)/etc/ridux-ui-hw-cursor-only.enable \
	$(wildcard $(ROOTFS_DIR)/etc/ridux-hyprland*.enable) \
	$(wildcard $(ROOTFS_DIR)/etc/hypr/*) \
	$(wildcard $(ROOTFS_DIR)/etc/nwg-dock-hyprland/*) \
	$(wildcard $(ROOTFS_DIR)/etc/gtk-3.0/*) \
	$(wildcard $(ROOTFS_DIR)/etc/gtk-4.0/*) \
	$(wildcard $(ROOTFS_DIR)/tmp/hyprland-home/config/gtk-3.0/*) \
	$(wildcard $(ROOTFS_DIR)/tmp/hyprland-home/config/gtk-4.0/*) \
	$(wildcard $(ROOTFS_DIR)/tmp/hyprland-home/config/xdg-desktop-portal/*) \
	$(wildcard $(ROOTFS_DIR)/tmp/hyprland-home/.cache) \
	$(wildcard $(ROOTFS_DIR)/tmp/hyprland-home/cache) \
	$(wildcard $(ROOTFS_DIR)/tmp/hyprland-home/share) \
	$(wildcard $(ROOTFS_DIR)/tmp/hyprland-home/state) \
	$(wildcard $(ROOTFS_DIR)/tmp/hyprland-waybar/config/gtk-3.0/*) \
	$(wildcard $(ROOTFS_DIR)/tmp/hyprland-waybar/config/gtk-4.0/*) \
	$(wildcard $(ROOTFS_DIR)/tmp/hyprland-waybar/config/xdg-desktop-portal/*) \
	$(wildcard $(ROOTFS_DIR)/tmp/hyprland-waybar/cache) \
	$(wildcard $(ROOTFS_DIR)/tmp/hyprland-waybar/share) \
	$(wildcard $(ROOTFS_DIR)/tmp/hyprland-waybar/state) \
	$(wildcard $(ROOTFS_DIR)/etc/xdg/xdg-desktop-portal/*) \
	$(wildcard $(ROOTFS_DIR)/etc/xdg/xdg-desktop-portal-wlr/*) \
	$(wildcard $(ROOTFS_DIR)/etc/xdg/waybar/*) \
	$(wildcard $(ROOTFS_DIR)/etc/xdg/wofi/*) \
	$(wildcard $(ROOTFS_DIR)/usr/share/applications/*) \
	$(wildcard $(ROOTFS_DIR)/usr/share/wayland-sessions/ridux-hyprland.desktop) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/Hyprland) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/start-hyprland) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/waybar) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/swaybg) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/wofi) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/hyprland-dialog) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/hyprland-run) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/hyprland-welcome) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/hyprland-update-screen) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/hyprland-donate-screen) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/hyprland-share-picker) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/ridux-open-launcher-native) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/ridux-open-launcher) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/ridux-open-files) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/ridux-terminal) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/ridux-display-settings) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/ridux-power-menu) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/ridux-user-services) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/ridux-waybar) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/ridux-session-spawn) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/ridux-wallpaper) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/foot) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/thunar) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/wdisplays) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/ridux-hyprland-session) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/hyprctl) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/dbus-update-activation-environment) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/fusermount3) \
	$(wildcard $(ROOTFS_DIR)/bin/fusermount3) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/lspci) \
	$(wildcard $(ROOTFS_DIR)/usr/bin/grep) \
	$(RIDUX_DBUS_SESSION) \
	$(RIDUX_DBUS_SYSTEM) \
	$(wildcard $(ROOTFS_DIR)/usr/libexec/xdg-document-portal) \
	$(wildcard $(ROOTFS_DIR)/usr/libexec/xdg-desktop-portal-gtk) \
	$(wildcard $(ROOTFS_DIR)/usr/share/dbus-1/services/org.freedesktop.portal.Desktop.service) \
	$(wildcard $(ROOTFS_DIR)/usr/share/dbus-1/services/org.freedesktop.portal.Documents.service) \
	$(wildcard $(ROOTFS_DIR)/usr/share/dbus-1/services/org.freedesktop.impl.portal.PermissionStore.service) \
	$(wildcard $(ROOTFS_DIR)/usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.gtk.service) \
	$(wildcard $(ROOTFS_DIR)/usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.hyprland.service) \
	$(wildcard $(ROOTFS_DIR)/usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.wlr.service) \
	$(wildcard $(ROOTFS_DIR)/usr/share/xdg-desktop-portal/portals/gtk.portal) \
	$(wildcard $(ROOTFS_DIR)/usr/share/glib-2.0/schemas/*) \
	$(wildcard $(ROOTFS_DIR)/usr/share/ridux/wallpapers/*) \
	$(wildcard $(ROOTFS_DIR)/usr/share/icons/Ridux/*) \
	$(wildcard $(ROOTFS_DIR)/usr/share/icons/Ridux/256x256/apps/*) \
	$(wildcard $(ROOTFS_DIR)/usr/share/pixmaps/ridux-logo.png) \
	$(wildcard $(ROOTFS_DIR)/usr/lib/x86_64-linux-gnu/qt6/qml/org/hyprland/style/*) \
	$(wildcard $(ROOTFS_DIR)/usr/lib/x86_64-linux-gnu/qt6/qml/org/hyprland/style/impl/*) \
	$(wildcard $(ROOTFS_DIR)/usr/lib/x86_64-linux-gnu/libhyprtoolkit.so*) \
	$(wildcard $(ROOTFS_DIR)/usr/lib/x86_64-linux-gnu/libiniparser.so*) \
	$(wildcard $(ROOTFS_DIR)/usr/lib/x86_64-linux-gnu/libkmod.so.2*) \
	$(wildcard $(ROOTFS_DIR)/usr/lib/x86_64-linux-gnu/libpci.so.3*) \
	$(wildcard $(ROOTFS_DIR)/usr/lib/x86_64-linux-gnu/libpcre2-8.so.0*) \
	$(wildcard $(ROOTFS_DIR)/lib/x86_64-linux-gnu/libfuse3.so.4*) \
	$(wildcard $(ROOTFS_DIR)/usr/lib/x86_64-linux-gnu/libfuse3.so.4*) \
	$(ROOTFS_DIR)/usr/bin/systemctl \
	$(wildcard $(ROOTFS_DIR)/usr/libexec/xdg-desktop-portal-hyprland)
	mkdir -p $(BUILD_DIR)
	rm -f $@.tmp
	rm -f $@.list
	{ \
		printf '%s\n' \
			etc/autoboot.cmd \
			etc/profile.d/ridux-gpu.sh \
			etc/passwd \
			etc/group \
			etc/nsswitch.conf \
			etc/fonts/fonts.conf \
			etc/fonts/fonts-ridux.conf \
			$$( [ ! -d "$(ROOTFS_DIR)/tmp/fontconfig-cache" ] || printf '%s\n' tmp/fontconfig-cache ) \
			$$( [ ! -d "$(ROOTFS_DIR)/var/cache/fontconfig" ] || printf '%s\n' var/cache/fontconfig ) \
			etc/ridux-ui-shell.enable \
			etc/ridux-gl-compositor.enable \
			etc/ridux-gpu-production.enable \
			etc/ridux-physical-gpu-preferred.enable \
			etc/ridux-gpu-intel.enable \
			etc/ridux-virtgpu-venus.enable \
			etc/ridux-vulkan-virtio-only.enable \
			etc/ridux-ui-hw-cursor-only.enable \
				$$( [ ! -f "$(ROOTFS_DIR)/etc/ridux-hyprland-primary.enable" ] || printf '%s\n' etc/ridux-hyprland-primary.enable ) \
				$$( [ ! -f "$(ROOTFS_DIR)/etc/ridux-hyprland-gpu.enable" ] || printf '%s\n' etc/ridux-hyprland-gpu.enable ) \
				$$( [ ! -f "$(ROOTFS_DIR)/etc/ridux-hyprland-virtio-gpu.enable" ] || printf '%s\n' etc/ridux-hyprland-virtio-gpu.enable ) \
				$$( [ ! -f "$(ROOTFS_DIR)/etc/ridux-hyprland-debug.enable" ] || printf '%s\n' etc/ridux-hyprland-debug.enable ) \
				$$( [ ! -f "$(ROOTFS_DIR)/etc/ridux-dbus-trace.enable" ] || printf '%s\n' etc/ridux-dbus-trace.enable ) \
				$$( [ ! -f "$(ROOTFS_DIR)/etc/ridux-ipc-debug.enable" ] || printf '%s\n' etc/ridux-ipc-debug.enable ) \
			$$( [ ! -d "$(ROOTFS_DIR)/etc/hypr" ] || printf '%s\n' etc/hypr ) \
			$$( [ ! -d "$(ROOTFS_DIR)/etc/nwg-dock-hyprland" ] || printf '%s\n' etc/nwg-dock-hyprland ) \
			$$( [ ! -d "$(ROOTFS_DIR)/etc/gtk-3.0" ] || printf '%s\n' etc/gtk-3.0 ) \
			$$( [ ! -d "$(ROOTFS_DIR)/etc/gtk-4.0" ] || printf '%s\n' etc/gtk-4.0 ) \
			$$( [ ! -d "$(ROOTFS_DIR)/tmp/hyprland-home/config/gtk-3.0" ] || printf '%s\n' tmp/hyprland-home/config/gtk-3.0 ) \
			$$( [ ! -d "$(ROOTFS_DIR)/tmp/hyprland-home/config/gtk-4.0" ] || printf '%s\n' tmp/hyprland-home/config/gtk-4.0 ) \
			$$( [ ! -d "$(ROOTFS_DIR)/tmp/hyprland-home/config/xdg-desktop-portal" ] || printf '%s\n' tmp/hyprland-home/config/xdg-desktop-portal ) \
			$$( [ ! -d "$(ROOTFS_DIR)/tmp/hyprland-home/.cache" ] || printf '%s\n' tmp/hyprland-home/.cache ) \
			$$( [ ! -d "$(ROOTFS_DIR)/tmp/hyprland-home/cache" ] || printf '%s\n' tmp/hyprland-home/cache ) \
			$$( [ ! -d "$(ROOTFS_DIR)/tmp/hyprland-home/share" ] || printf '%s\n' tmp/hyprland-home/share ) \
			$$( [ ! -d "$(ROOTFS_DIR)/tmp/hyprland-home/state" ] || printf '%s\n' tmp/hyprland-home/state ) \
			$$( [ ! -d "$(ROOTFS_DIR)/tmp/hyprland-waybar/config/gtk-3.0" ] || printf '%s\n' tmp/hyprland-waybar/config/gtk-3.0 ) \
			$$( [ ! -d "$(ROOTFS_DIR)/tmp/hyprland-waybar/config/gtk-4.0" ] || printf '%s\n' tmp/hyprland-waybar/config/gtk-4.0 ) \
			$$( [ ! -d "$(ROOTFS_DIR)/tmp/hyprland-waybar/config/xdg-desktop-portal" ] || printf '%s\n' tmp/hyprland-waybar/config/xdg-desktop-portal ) \
			$$( [ ! -d "$(ROOTFS_DIR)/tmp/hyprland-waybar/cache" ] || printf '%s\n' tmp/hyprland-waybar/cache ) \
			$$( [ ! -d "$(ROOTFS_DIR)/tmp/hyprland-waybar/share" ] || printf '%s\n' tmp/hyprland-waybar/share ) \
			$$( [ ! -d "$(ROOTFS_DIR)/tmp/hyprland-waybar/state" ] || printf '%s\n' tmp/hyprland-waybar/state ) \
			$$( [ ! -d "$(ROOTFS_DIR)/etc/xdg/xdg-desktop-portal" ] || printf '%s\n' etc/xdg/xdg-desktop-portal ) \
			$$( [ ! -d "$(ROOTFS_DIR)/etc/xdg/xdg-desktop-portal-wlr" ] || printf '%s\n' etc/xdg/xdg-desktop-portal-wlr ) \
			$$( [ ! -d "$(ROOTFS_DIR)/etc/xdg/waybar" ] || printf '%s\n' etc/xdg/waybar ) \
			$$( [ ! -d "$(ROOTFS_DIR)/etc/xdg/wofi" ] || printf '%s\n' etc/xdg/wofi ) \
			$$( [ ! -d "$(ROOTFS_DIR)/usr/share/applications" ] || printf '%s\n' usr/share/applications ) \
			$$( [ ! -f "$(ROOTFS_DIR)/usr/share/wayland-sessions/ridux-hyprland.desktop" ] || printf '%s\n' usr/share/wayland-sessions/ridux-hyprland.desktop ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/Hyprland" ] || printf '%s\n' usr/bin/Hyprland ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/start-hyprland" ] || printf '%s\n' usr/bin/start-hyprland ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/waybar" ] || printf '%s\n' usr/bin/waybar ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/swaybg" ] || printf '%s\n' usr/bin/swaybg ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/wofi" ] || printf '%s\n' usr/bin/wofi ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/hyprland-dialog" ] || printf '%s\n' usr/bin/hyprland-dialog ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/hyprland-run" ] || printf '%s\n' usr/bin/hyprland-run ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/hyprland-welcome" ] || printf '%s\n' usr/bin/hyprland-welcome ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/hyprland-update-screen" ] || printf '%s\n' usr/bin/hyprland-update-screen ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/hyprland-donate-screen" ] || printf '%s\n' usr/bin/hyprland-donate-screen ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/hyprland-share-picker" ] || printf '%s\n' usr/bin/hyprland-share-picker ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/ridux-open-launcher-native" ] || printf '%s\n' usr/bin/ridux-open-launcher-native ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/ridux-open-launcher" ] || printf '%s\n' usr/bin/ridux-open-launcher ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/ridux-open-files" ] || printf '%s\n' usr/bin/ridux-open-files ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/ridux-terminal" ] || printf '%s\n' usr/bin/ridux-terminal ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/ridux-display-settings" ] || printf '%s\n' usr/bin/ridux-display-settings ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/ridux-power-menu" ] || printf '%s\n' usr/bin/ridux-power-menu ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/ridux-user-services" ] || printf '%s\n' usr/bin/ridux-user-services ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/ridux-waybar" ] || printf '%s\n' usr/bin/ridux-waybar ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/ridux-session-spawn" ] || printf '%s\n' usr/bin/ridux-session-spawn ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/ridux-wallpaper" ] || printf '%s\n' usr/bin/ridux-wallpaper ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/foot" ] || printf '%s\n' usr/bin/foot ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/thunar" ] || printf '%s\n' usr/bin/thunar ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/wdisplays" ] || printf '%s\n' usr/bin/wdisplays ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/ridux-hyprland-session" ] || printf '%s\n' usr/bin/ridux-hyprland-session ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/hyprctl" ] || printf '%s\n' usr/bin/hyprctl ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/dbus-update-activation-environment" ] || printf '%s\n' usr/bin/dbus-update-activation-environment ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/fusermount3" ] || printf '%s\n' usr/bin/fusermount3 ) \
			$$( [ ! -x "$(ROOTFS_DIR)/bin/fusermount3" ] || printf '%s\n' bin/fusermount3 ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/lspci" ] || printf '%s\n' usr/bin/lspci ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/grep" ] || printf '%s\n' usr/bin/grep ) \
			usr/bin/ridux-dbus-session \
			usr/bin/ridux-dbus-system \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/libexec/xdg-desktop-portal" ] || printf '%s\n' usr/libexec/xdg-desktop-portal ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/libexec/xdg-document-portal" ] || printf '%s\n' usr/libexec/xdg-document-portal ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/libexec/xdg-permission-store" ] || printf '%s\n' usr/libexec/xdg-permission-store ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/libexec/xdg-desktop-portal-gtk" ] || printf '%s\n' usr/libexec/xdg-desktop-portal-gtk ) \
			$$( [ ! -f "$(ROOTFS_DIR)/usr/share/dbus-1/services/org.freedesktop.portal.Desktop.service" ] || printf '%s\n' usr/share/dbus-1/services/org.freedesktop.portal.Desktop.service ) \
			$$( [ ! -f "$(ROOTFS_DIR)/usr/share/dbus-1/services/org.freedesktop.portal.Documents.service" ] || printf '%s\n' usr/share/dbus-1/services/org.freedesktop.portal.Documents.service ) \
			$$( [ ! -f "$(ROOTFS_DIR)/usr/share/dbus-1/services/org.freedesktop.impl.portal.PermissionStore.service" ] || printf '%s\n' usr/share/dbus-1/services/org.freedesktop.impl.portal.PermissionStore.service ) \
			$$( [ ! -f "$(ROOTFS_DIR)/usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.gtk.service" ] || printf '%s\n' usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.gtk.service ) \
			$$( [ ! -f "$(ROOTFS_DIR)/usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.hyprland.service" ] || printf '%s\n' usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.hyprland.service ) \
			$$( [ ! -f "$(ROOTFS_DIR)/usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.wlr.service" ] || printf '%s\n' usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.wlr.service ) \
			$$( [ ! -d "$(ROOTFS_DIR)/usr/share/xdg-desktop-portal" ] || printf '%s\n' usr/share/xdg-desktop-portal ) \
			$$( [ ! -d "$(ROOTFS_DIR)/usr/share/glib-2.0/schemas" ] || printf '%s\n' usr/share/glib-2.0/schemas ) \
			$$( [ ! -d "$(ROOTFS_DIR)/usr/share/ridux/wallpapers" ] || printf '%s\n' usr/share/ridux/wallpapers ) \
			$$( [ ! -d "$(ROOTFS_DIR)/usr/share/icons/Ridux" ] || printf '%s\n' usr/share/icons/Ridux ) \
			$$( [ ! -f "$(ROOTFS_DIR)/usr/share/pixmaps/ridux-logo.png" ] || printf '%s\n' usr/share/pixmaps/ridux-logo.png ) \
			$$( [ ! -d "$(ROOTFS_DIR)/usr/lib/x86_64-linux-gnu/qt6/qml/org/hyprland" ] || printf '%s\n' usr/lib/x86_64-linux-gnu/qt6/qml/org/hyprland ) \
			$(patsubst $(ROOTFS_DIR)/%,%,$(wildcard $(ROOTFS_DIR)/usr/lib/x86_64-linux-gnu/libhyprtoolkit.so*)) \
			$(patsubst $(ROOTFS_DIR)/%,%,$(wildcard $(ROOTFS_DIR)/usr/lib/x86_64-linux-gnu/libiniparser.so*)) \
			$(patsubst $(ROOTFS_DIR)/%,%,$(wildcard $(ROOTFS_DIR)/usr/lib/x86_64-linux-gnu/libkmod.so.2*)) \
			$(patsubst $(ROOTFS_DIR)/%,%,$(wildcard $(ROOTFS_DIR)/usr/lib/x86_64-linux-gnu/libpci.so.3*)) \
			$(patsubst $(ROOTFS_DIR)/%,%,$(wildcard $(ROOTFS_DIR)/usr/lib/x86_64-linux-gnu/libpcre2-8.so.0*)) \
			$(patsubst $(ROOTFS_DIR)/%,%,$(wildcard $(ROOTFS_DIR)/lib/x86_64-linux-gnu/libfuse3.so.4*)) \
			$(patsubst $(ROOTFS_DIR)/%,%,$(wildcard $(ROOTFS_DIR)/usr/lib/x86_64-linux-gnu/libfuse3.so.4*)) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/bin/systemctl" ] || printf '%s\n' usr/bin/systemctl ) \
			$$( [ ! -x "$(ROOTFS_DIR)/usr/libexec/xdg-desktop-portal-hyprland" ] || printf '%s\n' usr/libexec/xdg-desktop-portal-hyprland ) \
			usr/share/riduxui \
			usr/share/icons/Adwaita/cursor.theme \
			usr/share/icons/Adwaita/index.theme \
			usr/share/icons/Adwaita/cursors \
			usr/share/icons/hicolor \
			usr/share/glvnd \
			usr/share/vulkan \
			usr/share/drirc.d \
			usr/bin/ridux-ui-shell \
			usr/bin/injury-shell \
			usr/bin/injury-compositor \
			usr/bin/ridux-gl-compositor \
			usr/bin/ridux-vulkan-probe \
			lib64/ridux-glibc-private-shim.so \
			bin/sh \
			$(patsubst $(ROOTFS_DIR)/%,%,$(R3_APP_ELFS)); \
	} > $@.list
	tar --format=ustar --hard-dereference -cf $@.tmp -C $(ROOTFS_DIR) -T $@.list
	rm -f $@.list
	mv $@.tmp $@

$(STACKCHK_TRACE_SO): tools/stackchk_trace.c tools/stackchk_trace.map
	mkdir -p $(ROOTFS_DIR)/opt/ridux
	$(CC) -shared -fPIC -O2 -fno-stack-protector -nostdlib \
		-Wl,--version-script=tools/stackchk_trace.map \
		-o $@ tools/stackchk_trace.c

$(FIREFOX_PROBE_SO): tools/firefox_probe.c tools/firefox_probe.map
	mkdir -p $(ROOTFS_DIR)/opt/ridux
	$(CC) -shared -fPIC -O2 -fno-stack-protector \
		-Wl,--version-script=tools/firefox_probe.map \
		-o $@ tools/firefox_probe.c -ldl

$(RIDUX_GLIBC_PRIVATE_SHIM): tools/ridux_glibc_private_shim.c tools/ridux_glibc_private_shim.map
	mkdir -p $(ROOTFS_DIR)/lib64
	$(CC) -shared -fPIC -O2 -fno-stack-protector -nostdlib \
		-Wl,--version-script=tools/ridux_glibc_private_shim.map \
		-o $@ tools/ridux_glibc_private_shim.c

$(INITRD_IMG): glib-compat-patch chromium-compat-patch fontconfig-compat-patch $(ROOTFS_DIR) $(ROOTFS_DIR)/etc/autoboot.cmd $(RIDUX_GL_COMPOSITOR) $(RIDUX_VULKAN_PROBE) $(RIDUX_GLIBC_PRIVATE_SHIM) $(DEMO_ELF) $(ABI_SMOKE_ELF) $(X11_SMOKE_ELF) $(BROWSER64_ELF) $(FIREFOX64_ELF) $(NOTES_R3_ELF) $(R3_APP_ELFS) $(RIDUX_SH_ELF) $(STACKCHK_TRACE_SO) $(FIREFOX_PROBE_SO)
	mkdir -p $(BUILD_DIR)
	rm -f $@.tmp
	tar --format=ustar \
		--exclude='./opt/firefox' \
		--exclude='./opt/chromium' \
		--exclude='./bin/*.elf' \
		--exclude='./usr/bin/ridux-shell' \
		--exclude='./usr/bin/ridux-shell-direct' \
		--exclude='./usr/bin/ridux-panel' \
		--exclude='./usr/bin/ridux-dock' \
		--exclude='./usr/bin/ridux-dashboard' \
		--exclude='./usr/bin/ridux-files-qt' \
		--exclude='./usr/bin/ridux-monitor-qt' \
		--exclude='./mnt/c' \
		--exclude='./opt/wayfire' \
		--exclude='./opt/kde-plasma' \
		--exclude='./etc/wayfire' \
		--exclude='./tmp/wayfire-home' \
		--exclude='./tmp/wayfire-build' \
		--exclude='./tmp/kde-home' \
		--exclude='./tmp/kde-tmp' \
		--exclude='./tmp/kde-var-tmp' \
		--exclude='./etc/ridux-wayfire*' \
		--exclude='./etc/ridux-plasma*' \
		--exclude='./lib64/libwayfire*' \
		--exclude='./lib64/libwf-*' \
		--exclude='./lib64/libwlroots*' \
		--exclude='./usr/lib/x86_64-linux-gnu/libwayfire*' \
		--exclude='./usr/lib/x86_64-linux-gnu/libwf-*' \
		--exclude='./usr/lib/x86_64-linux-gnu/libwlroots*' \
		--exclude='./usr/share/waybar' \
		--exclude='./usr/share/wlogout' \
		--exclude='./usr/share/wayland-sessions/*wayfire*' \
		--exclude='./usr/share/wayland-sessions/*plasma*' \
		-cf $@.tmp -C $(ROOTFS_DIR) .
	mv $@.tmp $@

$(ISO): $(KERNEL_BIN) $(INITRD_IMG) $(INITRD_OVERLAY_IMG) grub/grub.cfg
	rm -rf $(ISO_DIR)
	mkdir -p $(ISO_DIR)/boot/grub
	cp -al $(KERNEL_BIN) $(ISO_DIR)/boot/ridux-kernel.bin || cp $(KERNEL_BIN) $(ISO_DIR)/boot/ridux-kernel.bin
	cp -al $(INITRD_IMG) $(ISO_DIR)/boot/initrd.img || cp $(INITRD_IMG) $(ISO_DIR)/boot/initrd.img
	cp -al $(INITRD_OVERLAY_IMG) $(ISO_DIR)/boot/initrd-overlay.img || cp $(INITRD_OVERLAY_IMG) $(ISO_DIR)/boot/initrd-overlay.img
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
	cp -al $(KERNEL_BIN) $(ISO_DIR)/boot/ridux-kernel.bin || cp $(KERNEL_BIN) $(ISO_DIR)/boot/ridux-kernel.bin
	cp -al $(INITRD_IMG) $(ISO_DIR)/boot/initrd.img || cp $(INITRD_IMG) $(ISO_DIR)/boot/initrd.img
	cp -al $(INITRD_OVERLAY_IMG) $(ISO_DIR)/boot/initrd-overlay.img || cp $(INITRD_OVERLAY_IMG) $(ISO_DIR)/boot/initrd-overlay.img
	cp grub/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	rm -f $(ISO).tmp
	grub-mkrescue -o $(ISO).tmp $(ISO_DIR) > /dev/null 2>&1
	mv $(ISO).tmp $(ISO)

iso-xorriso-update: $(KERNEL_BIN) $(INITRD_OVERLAY_IMG) grub/grub.cfg
	test -f $(ISO)
	test $$(stat -c%s $(ISO)) -gt 104857600
	test -f $(INITRD_IMG)
	test $$(stat -c%s $(INITRD_IMG)) -gt 104857600
	rm -rf $(ISO_DIR)-xorriso
	mkdir -p $(ISO_DIR)-xorriso/boot
	$(XORRISO) -osirrox on -indev $(ISO) \
		-extract /boot/grub $(ISO_DIR)-xorriso/boot/grub >/tmp/ridux-xorriso-extract-grub.log 2>&1
	if $(XORRISO) -indev $(ISO) -find /efi.img -type f >/tmp/ridux-xorriso-efi-find.log 2>&1; then \
		$(XORRISO) -osirrox on -indev $(ISO) \
			-extract /efi.img $(ISO_DIR)-xorriso/efi.img >/tmp/ridux-xorriso-extract-efi.log 2>&1 || true; \
	fi
	chmod -R u+w $(ISO_DIR)-xorriso || true
	cp $(KERNEL_BIN) $(ISO_DIR)-xorriso/boot/ridux-kernel.bin
	cp $(INITRD_IMG) $(ISO_DIR)-xorriso/boot/initrd.img
	cp $(INITRD_OVERLAY_IMG) $(ISO_DIR)-xorriso/boot/initrd-overlay.img
	rm -f $(ISO_DIR)-xorriso/boot/grub/grub.cfg
	cp grub/grub.cfg $(ISO_DIR)-xorriso/boot/grub/grub.cfg
	rm -f $(ISO).xorriso.tmp
	if [ -f "$(ISO_DIR)-xorriso/efi.img" ]; then \
		$(XORRISO) -as mkisofs -quiet -iso-level 3 -R -J -V ISOIMAGE \
			-o $(ISO).xorriso.tmp \
			-b boot/grub/i386-pc/eltorito.img \
			-c boot.catalog \
			-no-emul-boot -boot-load-size 4 -boot-info-table --grub2-boot-info \
			-eltorito-alt-boot -e efi.img -no-emul-boot -boot-load-size 5760 \
			$(ISO_DIR)-xorriso; \
	else \
		$(XORRISO) -as mkisofs -quiet -iso-level 3 -R -J -V ISOIMAGE \
			-o $(ISO).xorriso.tmp \
			-b boot/grub/i386-pc/eltorito.img \
			-c boot.catalog \
			-no-emul-boot -boot-load-size 4 -boot-info-table --grub2-boot-info \
			$(ISO_DIR)-xorriso; \
	fi
	test $$(stat -c%s $(ISO).xorriso.tmp) -gt 104857600
	mv $(ISO).xorriso.tmp $(ISO)

clean:
	rm -rf $(BUILD_DIR)

run:
	@echo "ISO generated at $(ISO)"

chromium-rootfs:
	bash tools/package_chromium_rootfs.sh "$(ROOTFS_DIR)"

firefox-rootfs:
	bash tools/package_firefox_rootfs.sh "$(ROOTFS_DIR)"

browsers-rootfs: chromium-rootfs firefox-rootfs

wayfire-source:
	bash tools/bootstrap_wayfire_sources.sh "$(WAYFIRE_DIR)" "$(WAYFIRE_REF)"

wayfire-patches: wayfire-source
	bash tools/apply_wayfire_patches.sh "$(WAYFIRE_DIR)" "$(WAYFIRE_PATCH_DIR)"

wayfire-build: wayfire-source wayfire-patches
	RIDUX_WAYFIRE_PATCH_DIR="$(WAYFIRE_PATCH_DIR)" bash tools/build_wayfire_sources.sh "$(WAYFIRE_DIR)" "$(WAYFIRE_BUILD_TARGET)"

wayfire-rootfs:
	bash tools/package_wayfire_rootfs.sh "$(ROOTFS_DIR)" "$(WAYFIRE_DIR)/install"

wayfire-desktop: wayfire-build wayfire-rootfs

hyprland-source:
	RIDUX_HYPR_DEPS_REF="$(HYPR_DEPS_REF)" RIDUX_HYPRWIRE_REF="$(HYPRWIRE_REF)" RIDUX_GLSLANG_REF="$(GLSLANG_REF)" RIDUX_THIRD_PARTY_DEPS_REF="$(THIRD_PARTY_DEPS_REF)" \
		RIDUX_WAYLAND_PROTOCOLS_REF="$(WAYLAND_PROTOCOLS_REF)" RIDUX_XKBCOMMON_REF="$(XKBCOMMON_REF)" RIDUX_LIBINPUT_REF="$(LIBINPUT_REF)" \
		bash tools/bootstrap_hyprland_sources.sh "$(HYPRLAND_DIR)" "$(HYPRLAND_REF)" "$(NWG_DOCK_HYPRLAND_REF)"

hyprland-build: hyprland-source
	bash tools/build_hyprland_sources.sh "$(HYPRLAND_DIR)" "$(HYPRLAND_BUILD_TARGET)"

hyprland-rootfs:
	bash tools/package_hyprland_rootfs.sh "$(ROOTFS_DIR)" "$(HYPRLAND_DIR)/install"

hyprland-desktop: hyprland-build hyprland-rootfs

kde-source:
	bash tools/bootstrap_kde_plasma_sources.sh "$(KDE_DIR)"

kde-build kde-plasma-build: kde-source
	bash tools/build_kde_plasma_sources.sh "$(KDE_DIR)"

kde-rootfs kde-plasma-source-rootfs plasma-rootfs plasma-source-rootfs:
	bash tools/package_plasma_rootfs.sh "$(ROOTFS_DIR)" "$(KDE_INSTALL_DIR)"

plasma-desktop: plasma-rootfs

vendor-upstream:
	bash tools/vendor_upstream.sh network

vendor-check:
	bash tools/license_guard.sh

freebsd-bootstrap:
	bash tools/bootstrap_freebsd_src.sh "$(FREEBSD_SRC_DIR)" "$(FREEBSD_REF)"

freebsd-prepare:
	bash tools/setup_freebsd_integration.sh "$(FREEBSD_SRC_DIR)"

riduxbsd-prepare: freebsd-prepare

riduxbsd-world: riduxbsd-prepare
	bash tools/build_riduxbsd_release.sh --world-only --src "$(FREEBSD_SRC_DIR)"

riduxbsd-iso: riduxbsd-prepare
	bash tools/build_riduxbsd_release.sh --iso --src "$(FREEBSD_SRC_DIR)" --output "$(RIDUXBSD_ISO)"

riduxbsd-iso-dry-run: riduxbsd-prepare
	bash tools/build_riduxbsd_release.sh --iso --dry-run --src "$(FREEBSD_SRC_DIR)" --output "$(RIDUXBSD_ISO)"

riduxbsd-clean-live-artifacts:
	bash tools/clean_riduxbsd_live_artifacts.sh

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

# Legacy patched-live ISO path is intentionally disabled. RiduxBSD now
# builds from the FreeBSD source tree via riduxbsd-iso.
ridux-live-iso freebsd-live-iso:
	@echo "Legacy live ISO disabled. Use: make riduxbsd-iso"
	@false

ridux-live-vm freebsd-live-vm: ridux-live-iso
	@echo "Legacy live VM disabled. Use the source-built ISO: $(RIDUXBSD_ISO)"
	@false

ridux-live-iso-kernel freebsd-live-iso-kernel:
	@echo "Legacy live ISO kernel injection disabled. Use: make riduxbsd-iso"
	@false

ridux-live-vm-kernel freebsd-live-vm-kernel: ridux-live-iso-kernel
	@echo "Legacy live VM disabled. Use the source-built ISO: $(RIDUXBSD_ISO)"
	@false

ridux-live-iso-fast freebsd-live-iso-orbit:
	@echo "Legacy live fast ISO disabled. Use: make riduxbsd-iso"
	@false

ridux-live-vm-fast freebsd-live-vm-orbit: ridux-live-iso-fast
	@echo "Legacy live VM disabled. Use the source-built ISO: $(RIDUXBSD_ISO)"
	@false

freebsd-wayfire-pkgs:
	@echo "Legacy live pkg prefetch disabled. Source-built RiduxBSD uses make riduxbsd-iso."
	@false

ridux-wayfire-iso freebsd-wayfire-iso: riduxbsd-iso

freebsd-wayfire-iso-offline: riduxbsd-iso

freebsd-wayfire-iso-kernel: riduxbsd-iso

ridux-wayfire-vm freebsd-wayfire-vm: riduxbsd-iso
	powershell -ExecutionPolicy Bypass -File .\scripts\boot-freebsd-live-vm.ps1 -IsoPath ".\$(RIDUXBSD_ISO)"

freebsd-wayfire-vm-kernel: riduxbsd-iso
	powershell -ExecutionPolicy Bypass -File .\scripts\boot-freebsd-live-vm.ps1 -IsoPath ".\$(RIDUXBSD_ISO)"

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
