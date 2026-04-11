/*
 *   Creation Date: <2004/08/28 18:38:22 greg>
 *   Time-stamp: <2004/08/28 18:38:22 greg>
 *
 *	<init.c>
 *
 *	Initialization for Wii and Wii U
 *
 *   Copyright (C) 2004 Greg Watson
 *   Copyright (C) 2005 Stefan Reinauer
 *   Copyright (C) 2025 John Davis
 *
 *   based on mol/init.c:
 *
 *   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004 Samuel & David Rydh
 *      (samuel@ibrium.se, dary@lindesign.se)
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *
 */

#include "config.h"
#include "libopenbios/openbios.h"
#include "libopenbios/bindings.h"
#include "libopenbios/console.h"
#include "drivers/usb.h"
#include "arch/common/nvram.h"
#include "drivers/drivers.h"
#include "wii/wii.h"
#include "libopenbios/ofmem.h"
#include "libopenbios/video.h"
#include "openbios-version.h"
#include "libc/byteorder.h"
#include "libc/diskio.h"
#include "libc/vsprintf.h"
#define NO_QEMU_PROTOS
#include "arch/common/fw_cfg.h"
#include "arch/ppc/processor.h"
#include "context.h"

#include "../../drivers/timer.h" // TODO

struct cpudef {
    unsigned int iu_version;
    const char *name;
    int icache_size, dcache_size;
    int icache_sets, dcache_sets;
    int icache_block_size, dcache_block_size;
    int tlb_sets, tlb_size;
    void (*initfn)(const struct cpudef *cpu);
};

static uint16_t wii_platform = 0;

enum {
    WII_RVL = 0,
    WII_CAFE
};

extern void unexpected_excep(int vector);

void
unexpected_excep(int vector)
{
    printk("openbios panic: Unexpected exception %x\n", vector);
    for (;;) {
    }
}

extern void __divide_error(void);

void
__divide_error(void)
{
    return;
}

unsigned long isa_io_base;

extern struct _console_ops rvl_console_ops, cafe_console_ops;

int is_apple(void)
{
    return 1;
}

int is_oldworld(void)
{
    return 0;
}

int is_newworld(void)
{
    return 1;
}

int is_wii_rvl(void) {
    return wii_platform == WII_RVL;
}

int is_wii_cafe(void) {
    return wii_platform == WII_CAFE;
}

static void cafe_reset_all(void) {
    out_be32((volatile unsigned int*)CAFE_LATTE_IPCPPCMSG, CAFE_CMD_REBOOT);
    out_be32((volatile unsigned int*)CAFE_LATTE_IPCPPCCTRL, 0x1);

    while (in_be32((volatile unsigned int*)CAFE_LATTE_IPCPPCCTRL) & 0x1);
}

static void cafe_poweroff(void) {
    out_be32((volatile unsigned int*)CAFE_LATTE_IPCPPCMSG, CAFE_CMD_POWEROFF);
    out_be32((volatile unsigned int*)CAFE_LATTE_IPCPPCCTRL, 0x1);

    while (in_be32((volatile unsigned int*)CAFE_LATTE_IPCPPCCTRL) & 0x1);
}

static void
setenv(const char *env, const char *value)
{
	push_str( value );
	push_str( env );
	fword("$setenv");
}

void entry(void) {
    uint32_t pvr;
    isa_io_base = 0x80000000;

    //
    // Check the platform.
    //
    asm volatile ("mfpvr %0" : "=r"(pvr));
    wii_platform = ((pvr & 0xFFFF0000) == ESPRESSO_PVR_HIGH) ? WII_CAFE : WII_RVL;

#ifdef CONFIG_DEBUG_CONSOLE
    if (is_wii_cafe()) {
        init_console(cafe_console_ops);
    }
#endif

    ofmem_init();
    initialize_forth();
    /* won't return */

    printk("of_startup returned!\n");
    for (;;) {
    }
}

/* -- phys.lo ... phys.hi */
static void
push_physaddr(phys_addr_t value)
{
    PUSH(value);
}

/* From drivers/timer.c */
extern unsigned long timer_freq;

static void
cpu_generic_init(const struct cpudef *cpu)
{
    unsigned long cpuFreq = 0;
    unsigned long busFreq = 0;

    if (wii_platform == WII_CAFE) {
        cpuFreq = WII_CAFE_CPU_FREQ;
        busFreq = WII_CAFE_BUS_FREQ;
    } else {
        cpuFreq = WII_RVL_CPU_FREQ;
        busFreq = WII_RVL_BUS_FREQ;
    }

    push_str("/cpus");
    fword("find-device");

    fword("new-device");

    push_str(cpu->name);
    fword("device-name");

    push_str("cpu");
    fword("device-type");

    PUSH(mfpvr());
    fword("encode-int");
    push_str("cpu-version");
    fword("property");

    PUSH(cpu->dcache_size);
    fword("encode-int");
    push_str("d-cache-size");
    fword("property");

    PUSH(cpu->icache_size);
    fword("encode-int");
    push_str("i-cache-size");
    fword("property");

    PUSH(cpu->dcache_sets);
    fword("encode-int");
    push_str("d-cache-sets");
    fword("property");

    PUSH(cpu->icache_sets);
    fword("encode-int");
    push_str("i-cache-sets");
    fword("property");

    PUSH(cpu->dcache_block_size);
    fword("encode-int");
    push_str("d-cache-block-size");
    fword("property");

    PUSH(cpu->icache_block_size);
    fword("encode-int");
    push_str("i-cache-block-size");
    fword("property");

    PUSH(cpu->tlb_sets);
    fword("encode-int");
    push_str("tlb-sets");
    fword("property");

    PUSH(cpu->tlb_size);
    fword("encode-int");
    push_str("tlb-size");
    fword("property");

    timer_freq = busFreq / 4;
    PUSH(timer_freq);
    fword("encode-int");
    push_str("timebase-frequency");
    fword("property");

    PUSH(cpuFreq);
    fword("encode-int");
    push_str("clock-frequency");
    fword("property");

    PUSH(busFreq);
    fword("encode-int");
    push_str("bus-frequency");
    fword("property");

    push_str("running");
    fword("encode-string");
    push_str("state");
    fword("property");

    PUSH(0x20);
    fword("encode-int");
    push_str("reservation-granule-size");
    fword("property");
}

static void
cpu_750_init(const struct cpudef *cpu)
{
    cpu_generic_init(cpu);

    PUSH(0);
    fword("encode-int");
    push_str("reg");
    fword("property");

    fword("finish-device");
}

static const struct cpudef ppc_defs[] = {
    {
        .iu_version = 0x000080000,
        .name = "PowerPC,750",
        .icache_size = 0x8000,
        .dcache_size = 0x8000,
        .icache_sets = 0x80,
        .dcache_sets = 0x80,
        .icache_block_size = 0x20,
        .dcache_block_size = 0x20,
        .tlb_sets = 0x40,
        .tlb_size = 0x80,
        .initfn = cpu_750_init,
    },
    { // XXX find out real values
        .iu_version = 0x10080000,
        .name = "PowerPC,750",
        .icache_size = 0x8000,
        .dcache_size = 0x8000,
        .icache_sets = 0x80,
        .dcache_sets = 0x80,
        .icache_block_size = 0x20,
        .dcache_block_size = 0x20,
        .tlb_sets = 0x40,
        .tlb_size = 0x80,
        .initfn = cpu_750_init,
    },
    { // XXX find out real values
        .iu_version = 0x70000000,
        .name = "PowerPC,750",
        .icache_size = 0x8000,
        .dcache_size = 0x8000,
        .icache_sets = 0x80,
        .dcache_sets = 0x80,
        .icache_block_size = 0x20,
        .dcache_block_size = 0x20,
        .tlb_sets = 0x40,
        .tlb_size = 0x80,
        .initfn = cpu_750_init,
    },
    { // XXX find out real values
        .iu_version = 0x70020000,
        .name = "PowerPC,750",
        .icache_size = 0x8000,
        .dcache_size = 0x8000,
        .icache_sets = 0x80,
        .dcache_sets = 0x80,
        .icache_block_size = 0x20,
        .dcache_block_size = 0x20,
        .tlb_sets = 0x40,
        .tlb_size = 0x80,
        .initfn = cpu_750_init,
    },
    { // XXX find out real values
        .iu_version = 0x70010000,
        .name = "PowerPC,Espresso",
        .icache_size = 0x8000,
        .dcache_size = 0x8000,
        .icache_sets = 0x80,
        .dcache_sets = 0x80,
        .icache_block_size = 0x20,
        .dcache_block_size = 0x20,
        .tlb_sets = 0x40,
        .tlb_size = 0x80,
        .initfn = cpu_750_init,
    },
};

static const struct cpudef *
id_cpu(void)
{
    unsigned int iu_version;
    unsigned int i;

    iu_version = mfpvr() & 0xffff0000;

    for (i = 0; i < sizeof(ppc_defs) / sizeof(struct cpudef); i++) {
        if (iu_version == ppc_defs[i].iu_version)
            return &ppc_defs[i];
    }
    printk("Unknown cpu (pvr %x), freezing!\n", iu_version);
    for (;;) {
    }
}

static void arch_go(void);

static void
arch_go(void)
{
    /* Insert copyright property for MacOS 9 and below */
    if (find_dev("/rom/macos")) {
        fword("insert-copyright-property");
    }
}

/*
 *  filll        ( addr bytes quad -- )
 */

static void ffilll(void)
{
    const u32 longval = POP();
    u32 bytes = POP();
    u32 *laddr = (u32 *)cell2pointer(POP());
    u32 len;

    for (len = 0; len < bytes / sizeof(u32); len++) {
        *laddr++ = longval;
    }
}

/*
 * adler32        ( adler buf len -- checksum )
 *
 * Adapted from Mark Adler's original implementation (zlib license)
 *
 * Both OS 9 and BootX require this word for payload validation.
 */

#define DO1(buf,i)  {s1 += buf[i]; s2 += s1;}
#define DO2(buf,i)  DO1(buf,i); DO1(buf,i+1);
#define DO4(buf,i)  DO2(buf,i); DO2(buf,i+2);
#define DO8(buf,i)  DO4(buf,i); DO4(buf,i+4);
#define DO16(buf)   DO8(buf,0); DO8(buf,8);

static void adler32(void)
{
    uint32_t len = (uint32_t)POP();
    char *buf = (char *)POP();
    uint32_t adler = (uint32_t)POP();

    if (buf == NULL) {
        RET(-1);
    }

    uint32_t base = 65521;
    uint32_t nmax = 5552;

    uint32_t s1 = adler & 0xffff;
    uint32_t s2 = (adler >> 16) & 0xffff;

    uint32_t k;
    while (len > 0) {
        k = (len < nmax ? len : nmax);
        len -= k;

        while (k >= 16) {
            DO16(buf);
            buf += 16;
            k -= 16;
        }
        if (k != 0) {
            do {
                s1 += *buf++;
                s2 += s1;
            } while (--k);
        }

        s1 %= base;
        s2 %= base;
    }

    RET(s2 << 16 | s1);
}

/* ( size -- virt ) */
static void
dma_alloc(void)
{
    ucell size = POP();
    ucell addr;
    int ret;

    ret = ofmem_posix_memalign((void *)&addr, size, PAGE_SIZE);

    if (ret) {
        PUSH(0);
    } else {
        PUSH(addr);
    }
}

/* ( virt size cacheable? -- devaddr ) */
static void
dma_map_in(void)
{
    POP();
    POP();
    ucell va = POP();
    PUSH(va);
}

/* ( virt devaddr size -- ) */
static void
dma_sync(void)
{
    ucell size = POP();
    POP();
    ucell virt = POP();

    flush_dcache_range(cell2pointer(virt), cell2pointer(virt + size));
    flush_icache_range(cell2pointer(virt), cell2pointer(virt + size));
}

static void ehci_disable(uint32_t addr) {
    // Stop EHCI controller and wait for halt bit to be set.
    out_be32((volatile unsigned int*)(addr + 0x10), in_be32((volatile unsigned int*)(addr + 0x10)) & ~(0x1));
    while ((in_be32((volatile unsigned int*)(addr + 0x14)) & (1 << 12)) == 0) {
        mdelay(1);
    }

    // Reset the controller and wait for reset to complete.
    out_be32((volatile unsigned int*)(addr + 0x10), in_be32((volatile unsigned int*)(addr + 0x10)) | 0x2);
    while (in_be32((volatile unsigned int*)(addr + 0x10)) & 0x2) {
        mdelay(1);
    }
}

void
arch_of_init(void)
{
    phandle_t dnode;
    uint64_t ram_size;
    const struct cpudef *cpu;
    char buf[256];
    const char *stdin_path, *stdout_path, *boot_path;
    char *boot_device;
    ofmem_t *ofmem = ofmem_arch_get_private();
    ucell load_base;
    char autobootPath[32];
    int autoboot;
    int fd;

    openbios_init();
    modules_init();
    setup_timers();
    setup_video();

    bind_func("ppc-dma-alloc", dma_alloc);
    feval("['] ppc-dma-alloc to (dma-alloc)");
    bind_func("ppc-dma-map-in", dma_map_in);
    feval("['] ppc-dma-map-in to (dma-map-in)");
    bind_func("ppc-dma-sync", dma_sync);
    feval("['] ppc-dma-sync to (dma-sync)");

    //
    // Pulse the disc light on Wii.
    //
    if (wii_platform == WII_RVL) {
        out_be32((volatile unsigned int*)0x0D0000C0, in_be32((volatile unsigned int*)0x0D0000C0) | 0x20);
        mdelay(2000);
        out_be32((volatile unsigned int*)0x0D0000C0, in_be32((volatile unsigned int*)0x0D0000C0) & ~(0x20));
    }

    printk("\n");
    printk("=============================================================\n");
    printk(PROGRAM_NAME " " OPENBIOS_VERSION_STR " [%s]\n",
           OPENBIOS_BUILD_DATE);

    ram_size = ofmem->ramsize;

    printk("Memory: %lldM\n", ram_size / 1024 / 1024);

    //
    // Finalize the tree for the current Wii platform.
    //
    if (wii_platform == WII_CAFE) {
        fword("fixup-device-tree-cafe");
    } else {
        fword("fixup-device-tree-rvl");
    }

    //
    // Create the device tree info.
    //

    /* memory info */

    push_str("/memory");
    fword("find-device");

    /* all memory */

    push_physaddr(0);
    fword("encode-phys");
    /* This needs adjusting if #size-cells gets increased.
       Alternatively use multiple (address, size) tuples. */
    PUSH(ram_size & 0xffffffff);
    fword("encode-int");
    fword("encode+");
    push_str("reg");
    fword("property");

    cpu = id_cpu();
    cpu->initfn(cpu);
    printk("CPU type %s\n", cpu->name);

    snprintf(buf, sizeof(buf), "/cpus/%s", cpu->name);
    ofmem_register(find_dev("/memory"), find_dev(buf));
    node_methods_init(buf);

    stdin_path = "keyboard";
    stdout_path = "screen";

    /* Setup nvram variables */
    push_str("/options");
    fword("find-device");

    /* No bootorder present, use fw_cfg device if no custom
        boot-device specified */
    fword("boot-device");
    boot_device = pop_fstr_copy();

    if (boot_device && strcmp(boot_device, "disk") == 0) {
        boot_path = "hd";

        snprintf(buf, sizeof(buf),
                    "%s:,\\\\:tbxi "
                    "%s:,\\ppc\\bootinfo.txt "
                    "%s:,%%BOOT",
                    boot_path, boot_path, boot_path);

        push_str(buf);
        fword("encode-string");
        push_str("boot-device");
        fword("property");
    }

    free(boot_device);

    /* Set up other properties */

    push_str("/chosen");
    fword("find-device");

    push_str(stdin_path);
    fword("pathres-resolve-aliases");
    push_str("input-device");
    fword("$setenv");

    push_str(stdout_path);
    fword("pathres-resolve-aliases");
    push_str("output-device");
    fword("$setenv");

    fword("activate-tty-interface");

    //
    // Install GPU driver.
    //
    if (wii_platform == WII_CAFE) {
        push_str("/gx2");
        fword("find-device");
        feval("['] wii-gx2-driver-fcode 2 cells + 1 byte-load");
    } else if (is_wii_rvl()) {
        //
        // Configure video interface.
        //
        push_str("/video");
        fword("find-device");
        dnode = get_cur_dev();
        ob_flipper_vi_init(get_path_from_ph(dnode), RVL_XFB_BASE, RVL_FB_BASE);
        feval("['] flipper-vi-driver-fcode 2 cells + 1 byte-load");

        //
        // Enable interrupts on USB host controllers on Wii via "chicken bits" on EHCI.
        // Pulled from NetBSD/Linux/ppcskel.
        //
        // Setting these here will allow the OS X USB host controller drivers to expect a
        // more normal state as they may load in any order.
        //
        out_be32((volatile unsigned int*)0x0D0400CC, in_be32((volatile unsigned int*)0x0D0400CC) | 0xE9800);
    }

    //
    // Reset EHCI controllers to force all devices to OHCI.
    //
    ehci_disable(0x0D040000);
    if (wii_platform == WII_CAFE) {
        ehci_disable(0x0D120000);
        ehci_disable(0x0D140000);
    }

    //
    // Initialize OHCI controllers.
    //
    // Rear OHCI
    push_str("/usb@0d050000");
    fword("find-device");
    dnode = get_cur_dev();
    ob_usb_ohci_init(get_path_from_ph(dnode), get_int_property(dnode, "reg", NULL));

    // Internal Bluetooth OHCI
    push_str("/usb@0d060000");
    fword("find-device");
    dnode = get_cur_dev();
    ob_usb_ohci_init(get_path_from_ph(dnode), get_int_property(dnode, "reg", NULL));

    if (wii_platform == WII_CAFE) {
        // Front OHCI
        push_str("/usb@0d130000");
        fword("find-device");
        dnode = get_cur_dev();
        ob_usb_ohci_init(get_path_from_ph(dnode), get_int_property(dnode, "reg", NULL));

        // GamePad OHCI
        push_str("/usb@0d150000");
        fword("find-device");
        dnode = get_cur_dev();
        ob_usb_ohci_init(get_path_from_ph(dnode), get_int_property(dnode, "reg", NULL));
    }

    //
    // Initialize SDHC.
    //
    push_str("/sdhc");
    fword("find-device");
    dnode = get_cur_dev();
    ob_wii_shdc_init(get_path_from_ph(dnode), get_int_property(dnode, "reg", NULL));

    device_end();

    //
    // Check for disable-autoboot file. File presence will disable normal boot process
    //
    autoboot = 1;
    for (uint32_t i = 2; i < 6; i++) {
        snprintf(autobootPath, sizeof(autobootPath), "hd:%u,\\disable-autoboot", i);
        fd = open_io(autobootPath);
        if (fd != -1) {
            if (strcmp(get_fstype(fd), "FAT") != 0) {
                close_io(fd);
                continue;
            }

            printk("found disable-autoboot at %s\n", autobootPath);
            close_io(fd);
            autoboot = 0;
            break;
        }
    }

    setenv("auto-boot?", autoboot ? "true" : "false" );

    //
    // Bind poweroff and reset words.
    //
    if (wii_platform == WII_CAFE) {
        bind_func("ppc32-power-off", cafe_poweroff);
        feval("['] ppc32-power-off to power-off");
        bind_func("ppc32-reset-all", cafe_reset_all);
        feval("['] ppc32-reset-all to reset-all");
    }

    /* Implementation of filll word (required by BootX) */
    bind_func("filll", ffilll);

    /* Implementation of adler32 word (required by OS 9, BootX) */
    bind_func("(adler32)", adler32);

    bind_func("platform-boot", boot);
    bind_func("(arch-go)", arch_go);

    /* Allocate 8MB memory at load-base */
    fword("load-base");
    load_base = POP();
    ofmem_claim_phys(load_base, 0x800000, 0);
    ofmem_claim_virt(load_base, 0x800000, 0);
    ofmem_map(load_base, load_base, 0x800000, 0);
}
