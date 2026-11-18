/*
 *   OpenBIOS Wii RAM disk driver
 *
 *   A read-only block device backed by a disk image that the loader (or the
 *   Dolphin emulator, via an extra ELF segment) placed in MEM2. Used to boot
 *   in environments that have no usable storage controller.
 *
 *   Layout at WII_RAMDISK_BASE:
 *     0x000  "WRDK"            magic
 *     0x004  uint32 big-endian image size in bytes
 *     0x200  disk image
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *
 */

#include "config.h"
#include "libopenbios/bindings.h"
#include "kernel/kernel.h"
#include "libc/byteorder.h"
#include "libc/vsprintf.h"
#include <libopenbios/ofmem.h>
#include "drivers/drivers.h"

#define WII_RAMDISK_MAGIC        0x5752444B  /* "WRDK" */
#define WII_RAMDISK_DATA_OFFSET  0x200
#define WII_RAMDISK_BLOCK_SIZE   512
#define WII_RAMDISK_MAX_TRANSFER 0x10000

typedef struct {
    unsigned char *data;
    uint32_t size;
} ramdisk_device_t;

DECLARE_UNNAMED_NODE( ob_wii_ramdisk, 0, sizeof(ramdisk_device_t*) );

static void ob_wii_ramdisk_max_transfer(int *idx) {
    PUSH(WII_RAMDISK_MAX_TRANSFER);
}

static void ob_wii_ramdisk_read_blocks(int *idx) {
    cell n = POP();
    ucell blk = POP();
    unsigned char *dest = (unsigned char *)cell2pointer(POP());
    ramdisk_device_t *ramdisk = *(ramdisk_device_t **)idx;
    ucell total = ramdisk->size / WII_RAMDISK_BLOCK_SIZE;

    if (blk >= total) {
        RET(0);
    }
    if (blk + n > total) {
        n = total - blk;
    }

    memcpy(dest, ramdisk->data + (blk * WII_RAMDISK_BLOCK_SIZE), n * WII_RAMDISK_BLOCK_SIZE);
    PUSH(n);
}

static void ob_wii_ramdisk_block_size(int *idx) {
    PUSH(WII_RAMDISK_BLOCK_SIZE);
}

static void ob_wii_ramdisk_open(int *idx) {
    phandle_t ph;
    ramdisk_device_t *ramdisk;

    PUSH(find_ih_method("ramdisk", my_self()));
    fword("execute");
    ramdisk = cell2pointer(POP());
    *(ramdisk_device_t **)idx = ramdisk;

    selfword("open-deblocker");

    /* interpose disk-label */
    ph = find_dev("/packages/disk-label");
    fword("my-args");
    PUSH_ph( ph );
    fword("interpose");

    RET ( -1 );
}

static void ob_wii_ramdisk_close(int *idx) {
    selfword("close-deblocker");
}

NODE_METHODS(ob_wii_ramdisk) = {
    { "open",           ob_wii_ramdisk_open         },
    { "close",          ob_wii_ramdisk_close        },
    { "read-blocks",    ob_wii_ramdisk_read_blocks  },
    { "block-size",     ob_wii_ramdisk_block_size   },
    { "max-transfer",   ob_wii_ramdisk_max_transfer },
};

//
// Probes for a RAM disk image at base and, if one is there, creates a block
// device for it under the current device and points the hd/disk aliases at it.
// Returns 0 if a disk was registered.
//
int ob_wii_ramdisk_init(unsigned long base) {
    phandle_t dnode, aliases;
    ramdisk_device_t *ramdisk;
    const char *path;
    uint32_t size;

    if (in_be32((volatile unsigned int*)base) != WII_RAMDISK_MAGIC) {
        return 1;
    }
    size = in_be32((volatile unsigned int*)(base + 4));

    ramdisk = malloc(sizeof (*ramdisk));
    ramdisk->data = (unsigned char *)(base + WII_RAMDISK_DATA_OFFSET);
    ramdisk->size = size;

    fword("new-device");
    dnode = get_cur_dev();

    push_str("ramdisk");
    fword("device-name");

    push_str("block");
    fword("device-type");

    PUSH(pointer2cell(ramdisk));
    feval("value ramdisk");

    BIND_NODE_METHODS(dnode, ob_wii_ramdisk);
    fword("is-deblocker");

    fword("finish-device");

    path = get_path_from_ph(dnode);
    aliases = find_dev("/aliases");
    set_property(aliases, "hd", path, strlen(path) + 1);
    set_property(aliases, "disk", path, strlen(path) + 1);

    printk("RAM disk: %u KB at 0x%lx as %s\n", size / 1024, base + WII_RAMDISK_DATA_OFFSET, path);
    return 0;
}
