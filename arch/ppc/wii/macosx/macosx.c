/*
 *	<macosx.c>
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
#include "libc/vsprintf.h"
#include "boot_args.h"
#include "device_tree.h"
#include "macho-loader.h"
#include "macosx.h"
#include "mkext.h"
#include "../wii.h"

static phandle_t obp_devopen(const char *path) {
    phandle_t ph;

    push_str(path);
    fword("open-dev");
    ph = POP();

    return ph;
}

static int obp_devread(phandle_t ph, char *buf, int nbytes) {
    int ret;

    PUSH((int)buf);
    PUSH(nbytes);
    push_str("read");
    PUSH(ph);
    fword("$call-method");
    ret = POP();

    return ret;
}

static int obp_devseek(phandle_t ph, int hi, int lo) {
    int ret;

    PUSH(lo);
    PUSH(hi);
    push_str("seek");
    PUSH(ph);
    fword("$call-method");
    ret = POP();

    return ret;
}

static const char* get_mkext_name(void) {
    macho_sym_context_t kernel_syms;
    uint32_t            xnu_version;

    //
    // Get the kernel symbol table.
    //
    if (!xnu_get_symtab(&kernel_syms)) {
        printk("Failed to get kernel symbol table\n");
        return 0;
    }

    xnu_version = xnu_read_darwin_version(&kernel_syms);
    if (xnu_version == 0) {
        return 0;
    }

    if (xnu_match_darwin_version(xnu_version, XNU_VERSION_CHEETAH_MIN, XNU_VERSION_CHEETAH_MAX)) {
        return "Wii_cheetah.mkext";
    } else if (xnu_match_darwin_version(xnu_version, XNU_VERSION_PUMA_MIN, XNU_VERSION_PUMA_MAX)) {
        return "Wii_puma.mkext";
    } else if (xnu_match_darwin_version(xnu_version, XNU_VERSION_JAGUAR_MIN, XNU_VERSION_JAGUAR_MAX)) {
        return "Wii_jaguar.mkext";
    } else if (xnu_match_darwin_version(xnu_version, XNU_VERSION_PANTHER_MIN, XNU_VERSION_PANTHER_MAX)) {
        return "Wii_panther.mkext";
    } else if (xnu_match_darwin_version(xnu_version, XNU_VERSION_TIGER_MIN, XNU_VERSION_TIGER_MAX)) {
        return "Wii_tiger.mkext";
    } else {
        printk("Unknown Mac OS X version\n");
        return 0;
    }
}

boot_args_ptr macosx_get_boot_args(void) {
    phandle_t   memory_map;
    uint32_t*   prop;
    int         proplen;

    //
    // Read the boot args location from the memory-map node.
    //
    memory_map = find_dev("/chosen/memory-map");
    if (!memory_map) {
        return 0;
    }
    prop = (uint32_t*)get_property(memory_map, "BootArgs", &proplen);
    if (!prop || (prop[1] != sizeof (boot_args))) {
        return 0;
    }

    return (boot_args_ptr)(prop[0]);
}

int macosx_patch(void) {
    //
    // BootX generally lays out things in memory in the following order:
    //
    // Kernel
    // Drivers
    // Boot arguments
    // Flattened devicetree
    //
    boot_args_ptr   xnu_boot_args;
    DTEntry         dtEntry;
    void            *mkextPtr;
    mkext_header    mkextHeader;
    unsigned long   prop[2];
    const char*     mkextFileName;
    char            mkextName[32];
    char            mkextPath[32];
    void            *newDT;
    unsigned long   newDTSize;
    phandle_t       ph;
    int             ret;

    //
    // Get the boot arguments and devicetree.
    //
    xnu_boot_args = macosx_get_boot_args();
    if (!xnu_boot_args) {
        printk("Failed to get boot args!\n");
        return 0;
    }

    printk("BootArgs: %p, top of kernel: 0x%lx\n", xnu_boot_args, xnu_boot_args->topOfKernelData);
    printk("Devicetree: %p, length: 0x%lx\n", xnu_boot_args->deviceTreeP, xnu_boot_args->deviceTreeLength);
    DTInit(xnu_boot_args->deviceTreeP, xnu_boot_args->deviceTreeLength);

    //
    // Add a new DriversPackage property to the memory map.
    // This will be located where the devicetree currently is.
    //
    if (DTLookupEntry(0, "/chosen/memory-map", &dtEntry) != kSuccess) {
        return 0;
    }

    mkextFileName = get_mkext_name();
    if (!mkextFileName) {
        return 0;
    }

    //
    // Read the MKEXT header.
    // Search partitions until the MKEXT is found.
    //
    for (uint32_t i = 2; i < 10; i++) {
        snprintf(mkextPath, sizeof(mkextPath), "hd:%u,\\%s", i, mkextFileName);
        ph = obp_devopen(mkextPath);
        if (ph != 0) {
            printk("found mkext at %s\n", mkextPath);
            break;
        }
    }
    if (ph == 0) {
        printk("failed to open mkext\n");
        return 0;
    }

    ret = obp_devseek(ph, 0, 0);
    if (ret != 0) {
        printk("failed to seek mkext\n");
        return 0;
    }

    ret = obp_devread(ph, (char*)&mkextHeader, sizeof (mkextHeader));
    if (ret != sizeof (mkextHeader)) {
        printk("failed to read mkext header\n");
        return 0;
    }

    mkextPtr = xnu_boot_args->deviceTreeP;
    prop[0] = (unsigned long)mkextPtr;
    prop[1] = mkextHeader.length;
    sprintf(mkextName, "DriversPackage-%lx", prop[0]);
    printk("MKEXT (%s) will be at %p, length: %x\n", mkextName, mkextPtr, mkextHeader.length);

    if (DTAddProperty(dtEntry, mkextName, prop, sizeof (prop), &newDT, &newDTSize) != kSuccess) {
        return 0;
    }

    //
    // Read the mkext.
    //
    ret = obp_devseek(ph, 0, 0);
    if (ret !=  0) {
        printk("failed to seek mkext\n");
        return 0;
    }
    ret = obp_devread(ph, mkextPtr, mkextHeader.length);
    if (ret != mkextHeader.length) {
        printk("failed to read all mkext\n");
        return 0;
    }

    // TODO: do adler check.

    //
    // Copy the new devicetree after the mkext.
    //
    xnu_boot_args->deviceTreeP      = ((char*)xnu_boot_args->deviceTreeP) + ((prop[1] + 0xFFF) & ~(0xFFF));
    xnu_boot_args->deviceTreeLength = newDTSize;
    memcpy(xnu_boot_args->deviceTreeP, newDT, newDTSize);
    free(newDT);

    //
    // Fix the DeviceTree property.
    //
    DTInit(xnu_boot_args->deviceTreeP, xnu_boot_args->deviceTreeLength);
    if (DTLookupEntry(0, "/chosen/memory-map", &dtEntry) != kSuccess) {
        return 0;
    }
    prop[0] = (uint32_t)xnu_boot_args->deviceTreeP;
    prop[1] = newDTSize;
    DTSetProperty(dtEntry, "DeviceTree", prop);

    //
    // Adjust top of kernel address.
    //
    xnu_boot_args->topOfKernelData = (unsigned long)xnu_boot_args->deviceTreeP + ((newDTSize + 0xFFF) & ~(0xFFF));

    printk("New top of kernel: 0x%lx\n", xnu_boot_args->topOfKernelData);
    printk("New devicetree: %p, length: 0x%lx\n", xnu_boot_args->deviceTreeP, xnu_boot_args->deviceTreeLength);

    //
    // Fix DRAM sizes.
    // BootX in some versions will consolidate everything from OF together,
    // Wii platforms have noncontiguous memory arrangements.
    //
    // On RVL, this must be in sync with any reserved memory regions (FB, USB, etc).
    //
    if (is_wii_rvl()) {
        xnu_boot_args->PhysicalDRAM[0].base = 0x00000000;
        xnu_boot_args->PhysicalDRAM[0].size = 0x01800000; // 24MB MEM1
        xnu_boot_args->PhysicalDRAM[1].base = 0x10400000;
        xnu_boot_args->PhysicalDRAM[1].size = 0x03C00000; // 62MB MEM2
    } else if (is_wii_cafe()) {
        xnu_boot_args->PhysicalDRAM[0].base = 0x00000000;
        xnu_boot_args->PhysicalDRAM[0].size = 0x02000000; // 32MB MEM1
        xnu_boot_args->PhysicalDRAM[1].base = 0x10000000;
        xnu_boot_args->PhysicalDRAM[1].size = 0x7E000000; // 2016MB MEM2
    }

    //
    // Perform XNU patches and flush everything.
    //
    xnu_patch();
    flush_dcache_range((char*)0x4000, (char*)xnu_boot_args->topOfKernelData);
    flush_icache_range((char*)0x4000, (char*)xnu_boot_args->topOfKernelData);

    printk("XNU ready to boot\n");
    return 1;
}
