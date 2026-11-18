/*
 *      <console.c>
 *
 *      Simple text console
 *
 *   Copyright (C) 2025 John Davis
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *
 */

#include "config.h"
#include "libopenbios/bindings.h"
#include "libopenbios/console.h"
#include "drivers/drivers.h"
#include "libc/string.h"
#include "wii.h"

#ifdef CONFIG_DEBUG_CONSOLE
/* ******************************************************************
 *      common functions, implementing simple concurrent console
 * ****************************************************************** */


//
// Wii CAFE console functions.
//
static int cafe_putchar(int c) {
  //
  // Send character over IPC to be displayed on gamepad console.
  //
  out_be32((volatile unsigned int*)CAFE_LATTE_IPCPPCMSG, CAFE_CMD_PRINT | ((uint32_t)c));
  out_be32((volatile unsigned int*)CAFE_LATTE_IPCPPCCTRL, 0x1);

  while (in_be32((volatile unsigned int*)CAFE_LATTE_IPCPPCCTRL) & 0x1);
  return c;
}

static int cafe_availchar(void) {
  return 0;
}

static int cafe_getchar(void) {
  return 0;
}

struct _console_ops cafe_console_ops = {
    .putchar = cafe_putchar,
    .availchar = cafe_availchar,
    .getchar = cafe_getchar
};

//
// Wii RVL console: USB Gecko (or compatible) on an EXI memory card slot.
// Slot B (channel 1) is tried first, then slot A (channel 0).
// Dolphin emulates this device and exposes it on a local TCP port.
//
#define RVL_EXI_BASE          0x0D006800
#define RVL_EXI_CSR(ch)       (RVL_EXI_BASE + (0x14 * (ch)) + 0x00)
#define RVL_EXI_CR(ch)        (RVL_EXI_BASE + (0x14 * (ch)) + 0x0C)
#define RVL_EXI_DATA(ch)      (RVL_EXI_BASE + (0x14 * (ch)) + 0x10)

#define GECKO_CMD_ID          0x90000000
#define GECKO_CMD_RECV        0xA0000000
#define GECKO_CMD_SEND        0xB0000000
#define GECKO_CMD_TX_STATUS   0xC0000000
#define GECKO_CMD_RX_STATUS   0xD0000000
#define GECKO_ID              0x04700000
#define GECKO_OK              0x04000000
#define GECKO_RX_VALID        0x08000000

static int gecko_chan = -1;

static uint32_t gecko_command(int ch, uint32_t command) {
  uint32_t result;
  int timeout = 100000;

  // Device 0, 32MHz clock.
  out_be32((volatile unsigned int*)RVL_EXI_CSR(ch), 0xD0);
  out_be32((volatile unsigned int*)RVL_EXI_DATA(ch), command);
  // 2 byte immediate read/write transfer, start.
  out_be32((volatile unsigned int*)RVL_EXI_CR(ch), 0x19);
  while ((in_be32((volatile unsigned int*)RVL_EXI_CR(ch)) & 0x1) && (timeout-- > 0));
  result = in_be32((volatile unsigned int*)RVL_EXI_DATA(ch));
  out_be32((volatile unsigned int*)RVL_EXI_CSR(ch), 0);
  return result;
}

int rvl_gecko_probe(void) {
  static const int chans[] = { 1, 0 };

  for (unsigned int i = 0; i < sizeof (chans) / sizeof (chans[0]); i++) {
    if ((gecko_command(chans[i], GECKO_CMD_ID) & 0xFFFF0000) == GECKO_ID) {
      gecko_chan = chans[i];
      return 1;
    }
  }
  return 0;
}

int rvl_gecko_present(void) {
  return gecko_chan >= 0;
}

static int rvl_putchar(int c) {
  int timeout = 10000;

  if (gecko_chan < 0) {
    return c;
  }
  if (c == '\n') {
    rvl_putchar('\r');
  }
  while (!(gecko_command(gecko_chan, GECKO_CMD_SEND | ((uint32_t)(c & 0xFF) << 20)) & GECKO_OK) && (timeout-- > 0));
  return c;
}

static int rvl_availchar(void) {
  if (gecko_chan < 0) {
    return 0;
  }
  return (gecko_command(gecko_chan, GECKO_CMD_RX_STATUS) & GECKO_OK) != 0;
}

static int rvl_getchar(void) {
  uint32_t result;

  if (gecko_chan < 0) {
    return 0;
  }
  result = gecko_command(gecko_chan, GECKO_CMD_RECV);
  return (result & GECKO_RX_VALID) ? ((result >> 16) & 0xFF) : 0;
}

struct _console_ops rvl_console_ops = {
    .putchar = rvl_putchar,
    .availchar = rvl_availchar,
    .getchar = rvl_getchar
};

//
// Open Firmware device node for the USB Gecko, so it can be selected as the
// Forth input and output device ("usbgecko" alias).
//
DECLARE_UNNAMED_NODE( ob_usbgecko, 0, sizeof(int) );

static void ob_usbgecko_open(int *idx) {
  RET ( -1 );
}

static void ob_usbgecko_close(int *idx) {
}

/* ( addr len -- actual ) */
static void ob_usbgecko_write(int *idx) {
  int len = POP();
  char *p = (char *)cell2pointer(POP());

  for (int i = 0; i < len; i++) {
    rvl_putchar(p[i]);
  }
  PUSH(len);
}

/* ( addr len -- actual ) */
static void ob_usbgecko_read(int *idx) {
  int len = POP();
  char *p = (char *)cell2pointer(POP());
  int n = 0;

  if (len > 0 && rvl_availchar()) {
    *p = rvl_getchar();
    n = 1;
  }
  PUSH(n);
}

NODE_METHODS(ob_usbgecko) = {
    { "open",   ob_usbgecko_open  },
    { "close",  ob_usbgecko_close },
    { "write",  ob_usbgecko_write },
    { "read",   ob_usbgecko_read  },
};

//
// Creates the /usbgecko serial node and its alias. Returns 0 on success.
//
int rvl_gecko_create_node(void) {
  phandle_t dnode, aliases;
  const char *path;

  if (gecko_chan < 0) {
    return 1;
  }

  push_str("/");
  fword("find-device");
  fword("new-device");
  dnode = get_cur_dev();

  push_str("usbgecko");
  fword("device-name");
  push_str("serial");
  fword("device-type");

  BIND_NODE_METHODS(dnode, ob_usbgecko);
  fword("finish-device");

  path = get_path_from_ph(dnode);
  aliases = find_dev("/aliases");
  set_property(aliases, "usbgecko", path, strlen(path) + 1);
  return 0;
}

#endif	// CONFIG_DEBUG_CONSOLE
