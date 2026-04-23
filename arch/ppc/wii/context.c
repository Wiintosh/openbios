/*
 * context switching
 * 2003-10 by SONE Takeshi
 *
 * Residual data portions:
 *     Copyright (c) 2004-2005 Jocelyn Mayer
 */

#include "config.h"
#include "kernel/kernel.h"
#include "context.h"
#include "arch/ppc/processor.h"
#include "arch/ppc/residual.h"
#include "drivers/drivers.h"
#include "libopenbios/bindings.h"
#include "libopenbios/ofmem.h"
#include "libopenbios/initprogram.h"
#include "libopenbios/sys_info.h"
#include "arch/ppc/processor.h"
#include "macosx/macosx.h"

#define MAIN_STACK_SIZE 16384
#define IMAGE_STACK_SIZE 4096*2

#define debug printk

#define ULONG_SIZE 4
#define STACKFRAME_MINSIZE 16
#define STKOFF 8
#define SAVE_SPACE 144

static void start_main(void); /* forward decl. */
void __exit_context(void); /* assembly routine */

void entry(void);
void of_client_callback(void);

/*
 * Main context structure
 * It is placed at the bottom of our stack, and loaded by assembly routine
 * to start us up.
 */
static struct context main_ctx = {
    .pc = (unsigned long) start_main,
    .return_addr = (unsigned long) __exit_context,
};

/* This is used by assembly routine to load/store the context which
 * it is to switch/switched.  */
struct context * volatile __context = &main_ctx;

/* Client program context */
static struct context *client_ctx;

/* Stack for loaded ELF image */
static uint8_t image_stack[IMAGE_STACK_SIZE];

/* Pointer to startup context (physical address) */
unsigned long __boot_ctx;

/*
 * Main starter
 * This is the C function that runs first.
 */
static void start_main(void)
{
    /* Save startup context, so we can refer to it later.
     * We have to keep it in physical address since we will relocate. */
    __boot_ctx = virt_to_phys(__context);

    /* Set up client context */
    client_ctx = init_context(image_stack, sizeof image_stack, 1);
    __context = client_ctx;

    /* Start the real fun */
    entry();

    /* Returning from here should jump to __exit_context */
    __context = boot_ctx;
}

/* Setup a new context using the given stack.
 */
struct context *
init_context(uint8_t *stack, uint32_t stack_size, int num_params)
{
    struct context *ctx;

    ctx = (struct context *)
	(stack + stack_size - (sizeof(*ctx) + num_params*sizeof(unsigned long)));
    memset(ctx, 0, sizeof(*ctx));

    /* Fill in reasonable default for flat memory model */
    ctx->sp = virt_to_phys(SP_LOC(ctx));
    ctx->return_addr = virt_to_phys(__exit_context);

    return ctx;
}

/* init-program */
int
arch_init_program(void)
{
    volatile struct context *ctx = __context;
    ucell entry, param;
    char *macho;
    unsigned long machoTop;

    /* According to IEEE 1275, PPC bindings:
     *
     *    MSR = FP, ME + (DR|IR)
     *    r1 = stack (32 K + 32 bytes link area above)
     *    r5 = client interface handler
     *    r6 = address of client program arguments (unused)
     *    r7 = length of client program arguments (unused)
     *
     *    Yaboot and Linux use r3 and r4 for initrd address and size
     *    PReP machines use r3 and r4 for residual data and load image
     */

    ctx->regs[REG_R5] = (unsigned long)of_client_callback;
    ctx->regs[REG_R6] = 0;
    ctx->regs[REG_R7] = 0;

    /* Override the stack in the default context: the OpenBSD bootloader
       fails soon after setting up virt to phys mappings with the default
       stack. My best guess is that this is because the malloc() heap
       doesn't have a 1:1 virt to phys mapping. So for the moment we use
       the original (pre-context) location just under the MMU hash table
       (SDR1) which is mapped 1:1 and makes the bootloader happy. */
   // ctx->sp = mfsdr1() - 32768 - 65536; // TODO: Needs to be looked at

    /* Set param */
    feval("load-state >ls.param @");
    param = POP();
    ctx->param[0] = param;

    /* Set entry point */
    feval("load-state >ls.entry @");
    entry = POP();
    ctx->pc = entry;

    //
    // Patch BootX if present.
    // Entry is always within the first few pages.
    //
    gIsBootX = 0;
    macho = (char*)(entry & ~(0xFFF));
    for (int i = 0; i < 4; i++) {
        machoTop = macho_get_top(macho);
        if (machoTop) {
            break;
        }

        macho -= PAGE_SIZE;
    }

    if (machoTop) {
        macosx_patch_bootx(macho, machoTop - (unsigned long)macho);
        gIsBootX = 1;
    }

    return 0;
}

/* Switch to another context. */
struct context *switch_to(struct context *ctx)
{
    volatile struct context *save;
    struct context *ret;
    unsigned int lr;

    debug("switching to new context: pc %lX\n", ctx->pc);
    save = __context;
    __context = ctx;

    asm __volatile__ ("mflr %%r9\n\t"
                      "stw %%r9, %0\n\t"
                      "bl __switch_context\n\t"
                      "lwz %%r9, %0\n\t"
                      "mtlr %%r9\n\t" : "=m" (lr) : "m" (lr) : "%r9" );

    ret = __context;
    __context = (struct context *)save;
    return ret;
}

/* Start ELF Boot image */
unsigned int start_elf(void)
{
    volatile struct context *ctx = __context;

    ctx = switch_to((struct context *)ctx);
    return ctx->regs[REG_R3];
}
