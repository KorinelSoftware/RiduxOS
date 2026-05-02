/*
 * RiduxUI - Userspace UI for FreeBSD
 * 
 * This is the main entry point that initializes the FreeBSD framebuffer
 * and runs the RiduxOS UI (flush + window manager + apps).
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

#include "flush.h"

static framebuffer_t g_fb;

/* Initialize FreeBSD framebuffer */
static bool init_framebuffer(const char *device) {
    int fb_fd = open(device, O_RDWR);
    if (fb_fd < 0) {
        perror("Failed to open framebuffer device");
        return false;
    }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("Failed to get variable screen info");
        close(fb_fd);
        return false;
    }

    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        perror("Failed to get fixed screen info");
        close(fb_fd);
        return false;
    }

    g_fb.width = vinfo.xres;
    g_fb.height = vinfo.yres;
    g_fb.bpp = vinfo.bits_per_pixel;
    g_fb.pitch = finfo.line_length;
    g_fb.red_pos = vinfo.red.offset;
    g_fb.red_size = vinfo.red.length;
    g_fb.green_pos = vinfo.green.offset;
    g_fb.green_size = vinfo.green.length;
    g_fb.blue_pos = vinfo.blue.offset;
    g_fb.blue_size = vinfo.blue.length;

    size_t screensize = g_fb.pitch * g_fb.height;
    g_fb.address = (uint8_t *)mmap(NULL, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (g_fb.address == MAP_FAILED) {
        perror("Failed to mmap framebuffer");
        close(fb_fd);
        return false;
    }

    g_fb.ready = true;
    printf("Framebuffer initialized: %dx%d, %d bpp, pitch=%d\n",
           g_fb.width, g_fb.height, g_fb.bpp, g_fb.pitch);

    close(fb_fd);
    return true;
}

/* Main entry point */
int main(int argc, char *argv[]) {
    const char *fb_device = "/dev/fb0";
    if (argc > 1) {
        fb_device = argv[1];
    }

    printf("RiduxUI starting...\n");

    if (!init_framebuffer(fb_device)) {
        fprintf(stderr, "Failed to initialize framebuffer\n");
        return 1;
    }

    /* Initialize flush rendering */
    flush_init(&g_fb);

    /* TODO: Initialize window manager */
    /* TODO: Initialize apps */
    /* TODO: Main event loop */

    printf("RiduxUI running. Press Ctrl+C to exit.\n");

    /* Simple test: clear screen with a color */
    flush_clear(0x1a1a2e); /* Dark blue background */
    flush_execute();
    fb_present();

    /* Keep running */
    while (1) {
        sleep(1);
    }

    return 0;
}
