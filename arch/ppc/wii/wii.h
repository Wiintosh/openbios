/*
 *   Creation Date: <2004/08/28 17:50:12 stepan>
 *   Time-stamp: <2004/08/28 17:50:12 stepan>
 *
 *	<wii.h>
 *
 *   Copyright (C) 2005 Stefan Reinauer
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *
 */

#ifndef _H_WII
#define _H_WII

#define ARRSIZE(x)    (sizeof(x) / (sizeof(x[0])))

/* vfd.c */
extern int		vfd_draw_str( const char *str );
extern void		vfd_close( void );

//
// Wii platform types.
//
extern int is_wii_rvl(void);
extern int is_wii_cafe(void);

#define ESPRESSO_PVR_HIGH       0x70010000

//
// Speeds.
//
#define WII_RVL_CPU_FREQ        729000000
#define WII_RVL_BUS_FREQ        243000000
#define WII_CAFE_CPU_FREQ       1243125000
#define WII_CAFE_BUS_FREQ       248625000

// Set to 0 to use direct XFB.
#define RVL_FB_BASE                             0x10000000
#define RVL_XFB_BASE                            0x1012C000

// USB Gecko serial console (console.c).
int rvl_gecko_probe(void);
int rvl_gecko_present(void);
int rvl_gecko_create_node(void);

// Optional RAM disk image placed in free MEM2 by the loader (see drivers/wii_ramdisk.c).
#define RVL_RAMDISK_BASE                        0x10400000

#define CAFE_LATTE_IPCPPCMSG        0x0D800000
#define CAFE_LATTE_IPCPPCCTRL       0x0D800004
#define CAFE_CMD_POWEROFF           0xCAFE0001
#define CAFE_CMD_REBOOT             0xCAFE0002
#define CAFE_CMD_PRINT              0xCAFE6400

#include "kernel.h"

#endif   /* _H_WII */
