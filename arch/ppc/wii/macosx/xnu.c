/*
 *	<xnu.c>
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
#include "macho-loader.h"
#include "nlist.h"
#include "../wii.h"

//
// Patches XNU to prevent extra prints from occuring (stacktrace) on panic.
// Useful for Wii's constrained screen space.
//
#define XNU_DISABLE_DEBUG_STACKTRACE    0

//
// Patches XNU to prevent double character prints on panic in 10.4.
//
#define XNU_DISABLE_CONSDEBUG_PUTC      0

//
// Patches XNU color table for Wii.
//
#define XNU_PATCH_COLORTABLE            0

int xnu_get_symtab(macho_sym_context_t *symContext) {
    phandle_t   memory_map;
    uint32_t*   prop;
    int         proplen;
    struct symtab_command *symTabCommand;

    //
    // Read the kernel symtab from the memory-map node.
    //
    memory_map = find_dev("/chosen/memory-map");
    if (!memory_map) {
        return 0;
    }
    prop = (uint32_t*)get_property(memory_map, "Kernel-__SYMTAB", &proplen);
    if (!prop) {
        return 0;
    }
    symTabCommand = (struct symtab_command*)prop[0];

    symContext->symbol_table    = (struct nlist*) symTabCommand->symoff;
    symContext->symbol_count    = symTabCommand->nsyms;
    symContext->string_table    = (const char*) symTabCommand->stroff;
    symContext->string_size     = symTabCommand->strsize;

    return 1;
}

//
// Patch the CPU type checking to force the 750CX branch to match Espresso.
//
static int xnu_patch_cpu_check(macho_sym_context_t *symContext) {
    unsigned long   kern_sym_start;
    char            *base;
    int             found;

    static const char cpuFind[] = {
        0xFF, 0xFF, 0x0F, 0x00,     // Mask
        0x00, 0x08,                 // PROCESSOR_VERSION_750
        0x02, 0x00                  // Version for mask
    };
    static const char cpuReplace[] = {
        0xFF, 0xFF, 0x00, 0x00,     // Mask
        0x70, 0x01,                 // High bits of Espressor PVR
        0x00, 0x00                  // Version for mask
    };

    //
    // Get _start function location.
    //
    kern_sym_start = macho_resolve_symbol(symContext, "__start");
    if (kern_sym_start == 0) {
        return 0;
    }
    base = (char*)kern_sym_start;

    //
    // Look for pattern at entry point (__start).
    //
    found = 0;
    for (int i = 0; i < 0x4000; i++, base++) {
        if (memcmp(base, cpuFind, sizeof (cpuFind)) == 0) {
            found = 1;
            break;
        }
    }

    if (!found) {
        printk("xnu_patch_cpu_check: failed to locate CPU check patch pattern\n");
        return 0;
    }

    memcpy(base, cpuReplace, sizeof (cpuReplace));
    printk("xnu_patch_cpu_check: patched CPU check for Espresso at %p\n", base);
    return 1;
}

//
// Patch the BAT setup to avoid conflicts with other BATs for the video DBAT3, and prevent DBAT2 from being filled.
//
static int xnu_patch_io_bats(macho_sym_context_t *symContext, uint32_t kernelVersion) {
    unsigned long   kern_sym_ppc_init;
    const char      *videoBatFind2;
    const char      *videoBatRepl2;
    size_t          videoBatSize2;
    char            *base;
    int             found;

    static const char videoBatFind1[] = {
        0xF0, 0x00,     // ... 0xF000
        0x41, 0x82      // beq ...
    };
    static const char videoBatRepl1[] = {
        0xFF, 0x00,     // ... 0xFF00
        0x41, 0x82      // beq ...
    };
    static const char videoBatFindCheetahPuma2[] = {
        0x61, 0x6B, 0x1F, 0xFC      // ori r11, r11, 0x1FFC
    };
    static const char videoBatReplCheetahPuma2[] = {
        0x61, 0x6B, 0x00, 0x3C      // ori r11, r11, 0x3C
    };
    static const char videoBatFindJag2[] = {
        0x63, 0xB9, 0x1F, 0xFC      // ori r25, r29, 0x1FFC
    };
    static const char videoBatReplJag2[] = {
        0x63, 0xB9, 0x00, 0x3C      // ori r25, r29, 0x3C
    };
    static const char ioBatFind1[] = {
        0x4C, 0x00, 0x01, 0x2C,     // isync
        0x48                        // bl ...
    };
    static const char ioBatRepl1[] = {
        0x4C, 0x00, 0x01, 0x2C,     // isync
        0x60, 0x00, 0x00, 0x00      // nop
    };
    static const char ioBatFind2[] = {
        0x54, 0x63, 0x00, 0x06,     // rlwinm     r3, r3, 0x0, 0x0, 0x3
        0x7C, 0x03                  // cmpw r3, ...
    };
    static const char ioBatRepl2[] = {
        0x38, 0x60, 0x00, 0x00,     // li r3, 0x0000
        0x2C, 0x03, 0x00, 0x00,     // cmpwi r3, 0x0000
    };

    //
    // Get ppc_init function. TODO: Might need to adjust for 10.0 and 10.1.
    //
    kern_sym_ppc_init = macho_resolve_symbol(symContext, "_ppc_init");
    if (kern_sym_ppc_init == 0) {
        return 0;
    }
    base = (char*)kern_sym_ppc_init;

    //
    // Do first video BAT patch. Change mask to 0xFF000000 to catch Wii U's FB.
    // Start at 0x2 offset as patch starts halfway through an opcode.
    //
    found = 0;
    for (int i = 2; i < 0x1000; i += 4) {
        if (memcmp(&base[i], videoBatFind1, sizeof (videoBatFind1)) == 0) {
            memcpy(&base[i], videoBatRepl1, sizeof (videoBatRepl1));
            printk("xnu_patch_io_bats: patched video BAT pattern 1 at %p\n", &base[i]);
            found = 1;
            break;
        }
    }

    if (!found) {
        printk("xnu_patch_io_bats: failed to patch video BAT pattern 1\n");
        return 0;
    }

    if (is_wii_rvl()) {
        if (xnu_match_darwin_version(kernelVersion, XNU_VERSION_CHEETAH_MIN, XNU_VERSION_PUMA_MAX)) {
            videoBatFind2 = videoBatFindCheetahPuma2;
            videoBatRepl2 = videoBatReplCheetahPuma2;
            videoBatSize2 = sizeof (videoBatFindCheetahPuma2);
        } else if (xnu_match_darwin_version(kernelVersion, XNU_VERSION_JAGUAR_MIN, XNU_VERSION_JAGUAR_MAX)) {
            videoBatFind2 = videoBatFindJag2;
            videoBatRepl2 = videoBatReplJag2;
            videoBatSize2 = sizeof (videoBatFindJag2);
        } else {
            printk("xnu_patch_io_bats: unknown kernel version for video BAT pattern 2\n");
            return 0;
        }

        //
        // Do second video BAT patch. Change size to 2MB to ensure regular MEM2 memory is not included in BAT.
        //
        found = 0;
        for (int i = 0; i < 0x1000; i += 4) {
            if (memcmp(&base[i], videoBatFind2, videoBatSize2) == 0) {
                memcpy(&base[i], videoBatRepl2, videoBatSize2);
                printk("xnu_patch_io_bats: patched video BAT pattern 2 at %p\n", &base[i]);
                found = 1;
                break;
            }
        }

        if (!found) {
            printk("xnu_patch_io_bats: failed to patch video BAT pattern 2\n");
            return 0;
        }
    }

    //
    // Do first I/O BAT patch. This calls _get_io_base_addr which will panic.
    //
    found = 0;
    for (int i = 0; i < 0x1000; i += 4) {
        if (memcmp(&base[i], ioBatFind1, sizeof (ioBatFind1)) == 0) {
            memcpy(&base[i], ioBatRepl1, sizeof (ioBatRepl1));
            printk("xnu_patch_io_bats: patched I/O BAT pattern 1 at %p\n", &base[i]);
            found = 1;
            break;
        }
    }

    if (!found) {
        printk("xnu_patch_io_bats: failed to patch I/O BAT pattern 1\n");
        return 0;
    }

    //
    // Do second I/O BAT patch to prevent DBAT2 from being populated.
    //
    found = 0;
    for (int i = 0; i < 0x1000; i += 4) {
        if (memcmp(&base[i], ioBatFind2, sizeof (ioBatFind2)) == 0) {
            memcpy(&base[i], ioBatRepl2, sizeof (ioBatRepl2));
            printk("xnu_patch_io_bats: patched I/O BAT pattern 2 at %p\n", &base[i]);
            found = 1;
            break;
        }
    }

    if (!found) {
        printk("xnu_patch_io_bats: failed to patch I/O BAT pattern 2\n");
        return 0;
    }

    return 1;
}

//
// Patch function to return 0.
//
static int xnu_patch_disable_function(macho_sym_context_t *symContext, const char* funcName) {
    unsigned long   sym;
    uint32_t        *func;

    //
    // Get _PE_find_scc function location.
    //
    sym = macho_resolve_symbol(symContext, funcName);
    if (sym == 0) {
        return 0;
    }
    func = (uint32_t*)sym;

    //
    // Set r3 to 0 and return right away.
    //
    func[0] = 0x38600000;
    func[1] = 0x4E800020;

    return 1;
}

#if XNU_DISABLE_CONSDEBUG_PUTC
static int xnu_patch_consdebug_putc(macho_sym_context_t *symContext) {
    unsigned long   sym;
    char            *base;

    static const char debugFind[] = {
        0x2F, 0x83, 0x00, 0x00,
        0x40, 0x9E
    };
    static const char debugRepl[] = {
        0x2F, 0x83, 0x00, 0x00,
        0x48, 0x00
    };

    //
    // Get _consdebug_putc function location.
    //
    sym = macho_resolve_symbol(symContext, "_consdebug_putc");
    if (sym == 0) {
        return 0;
    }
    base = (char*)sym;

    //
    // Look for pattern.
    //
    for (int i = 0; i < 0x4000; i++, base++) {
        if (memcmp(base, debugFind, sizeof (debugFind)) == 0) {
            memcpy(base, debugRepl, sizeof (debugRepl));
            return 1;
        }
    }

    printk("xnu_patch_consdebug_putc: failed to locate patch pattern\n");
    return 0;
}
#endif

#if XNU_PATCH_COLORTABLE
typedef struct {
  uint32_t bit8;
  uint32_t bit16;
  uint32_t bit32;
} xnu_color_table_entry_t;

static int xnu_patch_colortable(macho_sym_context_t *symContext) {
    unsigned long               sym;
    xnu_color_table_entry_t     *colorTable;

    //
    // Get _vc_colors structure location.
    //
    sym = macho_resolve_symbol(symContext, "_vc_colors");
    if (sym == 0) {
        return 0;
    }
    colorTable = (xnu_color_table_entry_t*)sym;

    //
    // Patch colors to be YUV-equivalents.
    //
    colorTable[0].bit8 = colorTable[0].bit16 = colorTable[0].bit32 = 0x10801080;
    colorTable[1].bit8 = colorTable[1].bit16 = colorTable[1].bit32 = 0x316D31B8;
    colorTable[2].bit8 = colorTable[2].bit16 = colorTable[2].bit32 = 0x515B5151;
    colorTable[3].bit8 = colorTable[3].bit16 = colorTable[3].bit32 = 0x71487189;
    colorTable[4].bit8 = colorTable[4].bit16 = colorTable[4].bit32 = 0x1DB81D77;
    colorTable[5].bit8 = colorTable[5].bit16 = colorTable[5].bit32 = 0x3DA53DAF;
    colorTable[6].bit8 = colorTable[6].bit16 = colorTable[6].bit32 = 0x5D935D48;
    colorTable[7].bit8 = colorTable[7].bit16 = colorTable[7].bit32 = 0xB580B580;

    return 1;
}
#endif

int xnu_patch(void) {
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

    if (!xnu_patch_disable_function(&kernel_syms, "_PE_find_scc")) {
        return 0;
    }

    if (is_wii_cafe()) {
        if (!xnu_patch_cpu_check(&kernel_syms)) {
            return 0;
        }
    }

    if (xnu_match_darwin_version(xnu_version, 0, XNU_VERSION_JAGUAR_MAX)) {
        if (!xnu_patch_io_bats(&kernel_syms, xnu_version)) {
            return 0;
        }
    }

    //
    // Clean up for panics.
    //
#if XNU_DISABLE_DEBUG_STACKTRACE
    xnu_patch_disable_function(&kernel_syms, "_Debugger");
    xnu_patch_disable_function(&kernel_syms, "_print_backtrace");
    xnu_patch_disable_function(&kernel_syms, "_draw_panic_dialog");
#endif

#if XNU_DISABLE_CONSDEBUG_PUTC
    if (xnu_match_darwin_version(xnu_version, XNU_VERSION_TIGER_MIN, XNU_VERSION_TIGER_MAX)) {
        xnu_patch_consdebug_putc(&kernel_syms);
    }
#endif

#if XNU_PATCH_COLORTABLE
    if (is_wii_rvl()) {
        xnu_patch_colortable(&kernel_syms);
    }
#endif

    return 1;
}