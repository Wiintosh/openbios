/*
 *   OpenBIOS Wii "Flipper" GX driver
 *
 *   (C) 2025 John Davis
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *
 */

/*
	BootMii - a Free Software replacement for the Nintendo/BroadOn bootloader.
	low-level video support for the BootMii UI

Copyright (C) 2008, 2009	Hector Martin "marcan" <marcan@marcansoft.com>
Copyright (C) 2009			Haxx Enterprises <bushing@gmail.com>
Copyright (c) 2009		Sven Peter <svenpeter@gmail.com>

# This code is licensed to you under the terms of the GNU GPL, version 2;
# see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt

Some routines and initialization constants originally came from the
"GAMECUBE LOW LEVEL INFO" document and sourcecode released by Titanik
of Crazy Nation and the GC Linux project.
*/

#include "config.h"
#include "libopenbios/bindings.h"
#include "libopenbios/video.h"
#include "drivers/drivers.h"
#include "timer.h"
#include "flipper_vi.h"
#include "wii_ave.h"

#define WII_IPCPPCMSG      0x0D800000
#define WII_IPCPPCCTRL     0x0D800004

// These need to be in sync with the loader.
#define CMD_START_FB    0xCAFE0010
#define CMD_STOP_FB     0xCAFE0011
#define CMD_SET_XFB     0xA1000000
#define CMD_SET_FB      0xA2000000

//
// Read/write registers.
//
static inline uint16_t vi_read16(uint32_t offset) {
  return in_be16((volatile uint16_t*)(WII_VI_BASE + offset));
}
static inline uint32_t vi_read32(uint32_t offset) {
  return in_be32((volatile unsigned*)(WII_VI_BASE + offset));
}
static inline void vi_write16(uint32_t offset, uint16_t data) {
  out_be16((volatile uint16_t*)(WII_VI_BASE + offset), data);
}
static inline void vi_write32(uint32_t offset, uint32_t data) {
  out_be32((volatile unsigned*)(WII_VI_BASE + offset), data);
}

const uint16_t VIDEO_Mode640X480NtsciYUV16[64] = {
  0x0F06, 0x0001, 0x4769, 0x01AD, 0x02EA, 0x5140, 0x0003, 0x0018,
  0x0002, 0x0019, 0x410C, 0x410C, 0x40ED, 0x40ED, 0x0043, 0x5A4E,
  0x0000, 0x0000, 0x0043, 0x5A4E, 0x0000, 0x0000, 0x0000, 0x0000,
  0x1107, 0x01AE, 0x1001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
  0x0000, 0x0000, 0x0000, 0x0000, 0x2850, 0x0100, 0x1AE7, 0x71F0,
  0x0DB4, 0xA574, 0x00C1, 0x188E, 0xC4C0, 0xCBE2, 0xFCEC, 0xDECF,
  0x1313, 0x0F08, 0x0008, 0x0C0F, 0x00FF, 0x0000, 0x0000, 0x0000,
  0x0280, 0x0000, 0x0000, 0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF};

const uint16_t VIDEO_Mode640X480NtscpYUV16[64] = {
  0x1E0C, 0x0005, 0x4769, 0x01AD, 0x02EA, 0x5140, 0x0006, 0x0030,
  0x0006, 0x0030, 0x81D8, 0x81D8, 0x81D8, 0x81D8, 0x0015, 0x77A0,
  0x0000, 0x0000, 0x0015, 0x77A0, 0x0000, 0x0000, 0x022A, 0x01D6,
  0x120E, 0x0001, 0x1001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
  0x0000, 0x0000, 0x0000, 0x0000, 0x2828, 0x0100, 0x1AE7, 0x71F0,
  0x0DB4, 0xA574, 0x00C1, 0x188E, 0xC4C0, 0xCBE2, 0xFCEC, 0xDECF,
  0x1313, 0x0F08, 0x0008, 0x0C0F, 0x00FF, 0x0000, 0x0001, 0x0001,
  0x0280, 0x807A, 0x019C, 0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF};

//
// Sets the location of the XFB.
//
static void vi_set_xfb(unsigned long fb_addr) {

  vi_write32(WII_VI_REG_TFBL, WII_VI_REG_TFBL_PAGE_OFFSET | (fb_addr >> 5));

  vi_write32(WII_VI_REG_BFBL, WII_VI_REG_BFBL_PAGE_OFFSET | (fb_addr >> 5));
}

int ob_flipper_vi_init(const char *path, unsigned long xfb_base, unsigned long fb_base) {
  //
  // Reset video interface.
  //
  vi_write16(WII_VI_REG_DCR, WII_VI_REG_DCR_RESET);
	udelay(2);
	vi_write16(WII_VI_REG_DCR, 0);

  for (int i = 0; i < 64; i++) {
    if (i == 1) {
      vi_write16(2 * i, VIDEO_Mode640X480NtscpYUV16[i] & 0xFFFE);
    } else {
      vi_write16(2 * i, VIDEO_Mode640X480NtscpYUV16[i]);
    }
  }

  vi_write16(WII_VI_REG_DCR, VIDEO_Mode640X480NtscpYUV16[1]);

   uint32_t *fbPtr = (uint32_t*)(xfb_base);

     for (int i = 0; i < ((640 * 480 * 2) / 4); i++) {
    fbPtr[i] = 0x10801080;
  }

  vi_set_xfb(xfb_base);


  //
  // Initialize the AVE on Wii.
  //
  ave_init(1);

  //
  // Configure video info prior to driver load.
  // If an FB address was passed, operate in 640x480x32 mode.
  // Otherwise, use direct XFB in 640x480x16.
  //
  if (fb_base != 0) {
    VIDEO_DICT_VALUE(video.mvirt) = fb_base;
    VIDEO_DICT_VALUE(video.depth) = 32;
    VIDEO_DICT_VALUE(video.rb)    = 640 * 4;
  } else {
    VIDEO_DICT_VALUE(video.mvirt) = xfb_base;
    VIDEO_DICT_VALUE(video.depth) = 16;
    VIDEO_DICT_VALUE(video.rb)    = 640 * 2;
  }

  //
  // Startup Starlet FB to XFB conversion if running with FB bounce buffer.
  //
  if (fb_base != 0) {
    //
    // Stop any current conversion.
    //
    out_be32((volatile unsigned int*)WII_IPCPPCMSG, CMD_STOP_FB);
    out_be32((volatile unsigned int*)WII_IPCPPCCTRL, 0x1);
    while (in_be32((volatile unsigned int*)WII_IPCPPCCTRL) & 0x1);

    //
    // Send addresses.
    //
    out_be32((volatile unsigned int*)WII_IPCPPCMSG, CMD_SET_XFB | (xfb_base >> 8));
    out_be32((volatile unsigned int*)WII_IPCPPCCTRL, 0x1);
    while (in_be32((volatile unsigned int*)WII_IPCPPCCTRL) & 0x1);
    out_be32((volatile unsigned int*)WII_IPCPPCMSG, CMD_SET_FB | (fb_base >> 8));
    out_be32((volatile unsigned int*)WII_IPCPPCCTRL, 0x1);
    while (in_be32((volatile unsigned int*)WII_IPCPPCCTRL) & 0x1);

    //
    // Start conversion.
    //
    out_be32((volatile unsigned int*)WII_IPCPPCMSG, CMD_START_FB);
    out_be32((volatile unsigned int*)WII_IPCPPCCTRL, 0x1);
    while (in_be32((volatile unsigned int*)WII_IPCPPCCTRL) & 0x1);
  }

  return 0;
}


