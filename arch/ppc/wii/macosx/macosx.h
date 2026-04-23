/*
 *	<macosx.h>
 *
 *   Copyright (C) 2025 John Davis
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *
 */

#ifndef _H_MACOSX
#define _H_MACOSX

#include "boot_args.h"

extern int gIsBootX;

typedef struct {
    struct nlist*   symbol_table;
    unsigned long   symbol_count;
    const char*     string_table;
    unsigned long   string_size;
} macho_sym_context_t;

//
// Kernel versions.
//
#define XNU_VERSION(A, B, C)  ((A) * 10000 + (B) * 100 + (C))

#define XNU_VERSION_CHEETAH_PUMA   1
#define XNU_VERSION_PUMA_UPDATED   5
#define XNU_VERSION_JAGUAR         6
#define XNU_VERSION_PANTHER        7
#define XNU_VERSION_TIGER          8
#define XNU_VERSION_LEOPARD        9
#define XNU_VERSION_SNOW_LEOPARD   10

//
// Minimum kernel versions.
//
#define XNU_VERSION_CHEETAH_MIN             XNU_VERSION(XNU_VERSION_CHEETAH_PUMA, 3, 0)
#define XNU_VERSION_PUMA_MIN                XNU_VERSION(XNU_VERSION_CHEETAH_PUMA, 4, 0)
#define XNU_VERSION_JAGUAR_MIN              XNU_VERSION(XNU_VERSION_JAGUAR, 0, 0)
#define XNU_VERSION_PANTHER_MIN             XNU_VERSION(XNU_VERSION_PANTHER, 0, 0)
#define XNU_VERSION_TIGER_MIN               XNU_VERSION(XNU_VERSION_TIGER, 0, 0)
#define XNU_VERSION_LEOPARD_MIN             XNU_VERSION(XNU_VERSION_LEOPARD, 0, 0)
#define XNU_VERSION_SNOW_LEOPARD_MIN        XNU_VERSION(XNU_VERSION_SNOW_LEOPARD, 0, 0)

//
// Maximum kernel versions.
//
#define XNU_VERSION_CHEETAH_MAX             (XNU_VERSION_PUMA_MIN - 1)
#define XNU_VERSION_PUMA_MAX                (XNU_VERSION_JAGUAR_MIN - 1)
#define XNU_VERSION_JAGUAR_MAX              (XNU_VERSION_PANTHER_MIN - 1)
#define XNU_VERSION_PANTHER_MAX             (XNU_VERSION_TIGER_MIN - 1)
#define XNU_VERSION_TIGER_MAX               (XNU_VERSION_LEOPARD_MIN - 1)
#define XNU_VERSION_LEOPARD_MAX             (XNU_VERSION_SNOW_LEOPARD_MIN - 1)

extern unsigned long macho_resolve_symbol(macho_sym_context_t *context, const char *symbolName);
extern unsigned long macho_get_top(void *macho);
extern int macosx_patch_bootx(char *base, unsigned long length);
extern int macosx_check_bootx(void);
extern boot_args_ptr macosx_get_boot_args(void);
extern int macosx_patch(void);

//
// xnu_version.c
//
extern int xnu_match_darwin_version(uint32_t kernelVersion, uint32_t minVerison, uint32_t maxVersion);
extern uint32_t xnu_read_darwin_version(macho_sym_context_t *symContext);

//
// xnu.c
//
extern int xnu_patch(void);

#endif
