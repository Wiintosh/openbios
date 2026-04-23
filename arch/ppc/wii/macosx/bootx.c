/*
 *	<bootx.c>
 *
 *   Copyright (C) 2025 John Davis
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *
 */

#include "config.h"
#include "libopenbios/bindings.h"
#include "macosx.h"

extern void flush_dcache_range(char *start, char *stop);
extern void flush_icache_range(char *start, char *stop);

int gIsBootX = 0;

//
// Check boot args for -v or -s to enable BootX verbose mode.
//
static int macosx_check_verbose_bootargs(void) {
    phandle_t   options;
    char        *bootargs;
    char        *bootargsPtr;
    char        tc;
    int         dash;
    int         verbose;

    options = find_dev("/options");
    bootargs = get_property(options, "boot-args", NULL);
    if (!bootargs) {
        return 0;
    }

    verbose = 0;
    if (bootargs[0] != '\0') {
        dash = 0;
        bootargsPtr = bootargs;

        while ((tc = *bootargsPtr) != '\0') {
            tc = tolower(tc);

            if (tc == '-') {
                dash = 1;
                bootargsPtr++;
                continue;
            }

            if (tc == 's') {
                verbose = 1;
                break;
            } else if (tc == 'v') {
                verbose = 1;
                break;
            }

            if (dash) {
                if (isspace(tc)) {
                    dash = 0;
                }
            }
            bootargsPtr++;
        }
    }

    return verbose;
}

//
// Patch BootX.
//
int macosx_patch_bootx(char *base, unsigned long length) {
    int is_bootx;

    static const char bootx_log_find[] = "\n\nMac OS X Loader";
    static const char bootx_kernelcache_find[] = "kernelcache.%08lX";
    static const char bootx_msr_find[] = { 0x38, 0x00, 0x10, 0x00 };
    static const char bootx_msr_repl[] = { 0x38, 0x00, 0x10, 0x30 };

    int found_verbose;
    static const char bootx_kernelcache_repl[] = "zznoprelink.%08lX";
    static const char bootx_verbose_find1[] = { 0x38, 0x60, 0x00, 0x10, 0x48 };
    static const char bootx_verbose_find2[] = { 0x38, 0x60, 0x00, 0x00 };
    static const char bootx_verbose_repl2[] = { 0x38, 0x60, 0x00, 0x10 };

    //
    // Check that this is actually BootX.
    // Some versions have a newline at the end, must search without null terminator.
    //
    is_bootx = 0;
    for (int i = 0; i < length - strlen (bootx_log_find); i++) {
        if (memcmp(&base[i], bootx_log_find, strlen (bootx_log_find)) == 0) {
            is_bootx = 1;
            break;
        }
    }

    if (!is_bootx) {
        printk("Failed to find BootX pattern\n");
        return 1;
    }
    printk("Patching BootX\n");

    //
    // Patch MSR bits to prevent IR/DR from being disabled.
    //
    for (int i = 0; i < length - sizeof (bootx_msr_find); i+=4) {
        if (memcmp(&base[i], bootx_msr_find, sizeof (bootx_msr_find)) == 0) {
            memcpy(&base[i], bootx_msr_repl, sizeof (bootx_msr_repl));
            printk("Patched BootX MSR pattern at %p\n", &base[i]);
        }
    }

    //
    // Prevent prelinked kernelcache from being loaded, the supplemental mkext cannot be linked in that state.
    //
    for (int i = 0; i < length - sizeof (bootx_kernelcache_find); i++) {
        if (memcmp(&base[i], bootx_kernelcache_find, sizeof (bootx_kernelcache_find)) == 0) {
            memcpy(&base[i], bootx_kernelcache_repl, sizeof (bootx_kernelcache_repl));
            printk("Patched BootX kernelcache pattern at %p\n", &base[i]);
            break;
        }
    }

    if (macosx_check_verbose_bootargs()) {
        //
        // Patch check to force enable verbose mode.
        //
        found_verbose = 0;
        for (int i = 0; i < length - sizeof (bootx_verbose_find1); i+=4) {
            if (memcmp(&base[i], bootx_verbose_find1, sizeof (bootx_verbose_find1)) == 0) {
                found_verbose = 1;
                continue;
            } else if (found_verbose && (memcmp(&base[i], bootx_verbose_find2, sizeof (bootx_verbose_find2)) == 0)) {
                memcpy(&base[i], bootx_verbose_repl2, sizeof (bootx_verbose_repl2));
                printk("Patched BootX verbose at %p\n", &base[i]);
                break;
            }
        }
    }

    flush_dcache_range(base, base + length);
    flush_icache_range(base, base + length);

    return 0;
}

int macosx_check_bootx(void) {
    phandle_t   chosen;

    //
    // Check for BootX properties. This indicates a Mac OS X bootup in progress.
    //
    chosen = find_dev("/chosen");
    if (!chosen) {
        return 0;
    }
    if (!get_property(chosen, "BootXCacheMisses", NULL) || !get_property(chosen, "BootXCacheHits", NULL) || !get_property(chosen, "BootXCacheEvicts", NULL)) {
        return 0;
    }
    return 1;
}
