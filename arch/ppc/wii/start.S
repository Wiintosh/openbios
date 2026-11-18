/*
 *   Creation Date: <2001/06/16 21:30:18 samuel>
 *   Time-stamp: <2003/04/04 16:32:06 samuel>
 *
 *	<init.S>
 *
 *	Asm glue for ELF images
 *
 *   Copyright (C) 2001, 2002, 2003 Samuel Rydh (samuel@ibrium.se)
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *
 */

#include "autoconf.h"
#include "asm/asmdefs.h"
#include "asm/processor.h"

/************************************************************************/
/*	Macros								*/
/************************************************************************/

#define ILLEGAL_VECTOR( v )	.org __vectors + v ; vector__##v: bl trap_error ;
#define VECTOR( v, dummystr )	.org __vectors + v ; vector__##v

#define ULONG_SIZE 4
#define STACKFRAME_MINSIZE 16
#define stl stw
#define ll  lwz

.macro EXCEPTION_PREAMBLE
    mtsprg1 r1 /* scratch */
    mfsprg0 r1 /* exception stack in sprg0 */
    addi    r1, r1, -80 /* push exception frame */

    stw	r0,0(r1) ;	/* save r0 */
    mfsprg1	r0 ;
    stw	r0,4(r1) ;	/* save r1 */
    stw	r2,8(r1) ;	/* save r2 */
    stw	r3,12(r1) ;	/* save r3 */
    stw	r4,16(r1) ;
    stw	r5,20(r1) ;
    stw	r6,24(r1) ;
    stw	r7,28(r1) ;
    stw	r8,32(r1) ;
    stw	r9,36(r1) ;
    stw	r10,40(r1) ;
    stw	r11,44(r1) ;
    stw	r12,48(r1) ;

    mflr	r0 ;
    stw	r0,52(r1) ;
    mfcr	r0 ;
    stw	r0,56(r1) ;
    mfctr	r0 ;
    stw	r0,60(r1) ;
    mfxer	r0 ;
    stw	r0,64(r1) ;

    /* 76(r1) unused */
    addi	r1,r1,-16 ;	/* call conventions uses 0(r1) and 4(r1)... */
.endm

/************************************************************************/
/*	stack space							*/
/************************************************************************/

	.section .bss
	.balign 32

    .space  32*1024		// 32 K client stack
__client_stack:
	.space	128

	.space	64*1024		// 64 K stack
__stack:
    .space  64

	.space  32*1024		// 32 K exception stack
__exception_stack:
    .space	128

/************************************************************************/
/*	vectors								*/
/************************************************************************/

    .section .text.vectors, "ax"
GLOBL(__vectors):
    nop			// NULL-jmp trap
1:	nop			//
    b	1b

ILLEGAL_VECTOR( 0x100 )

trap_error:
    lis	r1, 0x8000			/* r1=0x80000000 */
    add.	r1,r1,r1			/* r1=r1+r1 (high 32bit !0) */
    beq	1f

    mfmsr	r1  				/* unset MSR_SF */
    clrldi	r1,r1,1
    mtmsrd	r1
1:
    mflr	r3
    LOAD_REG_FUNC(r4, unexpected_excep)
    mtctr r4
    bctr

ILLEGAL_VECTOR( 0x200 )

VECTOR( 0x300, "DSI" ):
    EXCEPTION_PREAMBLE
    LOAD_REG_FUNC(r3, dsi_exception)
    mtctr	r3
    bctrl
    b exception_return

VECTOR( 0x400, "ISI" ):
    EXCEPTION_PREAMBLE
    LOAD_REG_FUNC(r3, isi_exception)
    mtctr	r3
    bctrl
    b exception_return

ILLEGAL_VECTOR( 0x500 )
ILLEGAL_VECTOR( 0x600 )
ILLEGAL_VECTOR( 0x700 )

VECTOR( 0x800, "FPU" ):
    mtsprg1	r3
    mfsrr1	r3
    ori	r3,r3,0x2000
    mtsrr1	r3
    mfsprg1	r3
    RFI

ILLEGAL_VECTOR( 0x900 )
ILLEGAL_VECTOR( 0xa00 )
ILLEGAL_VECTOR( 0xb00 )
ILLEGAL_VECTOR( 0xc00 )
ILLEGAL_VECTOR( 0xd00 )
ILLEGAL_VECTOR( 0xe00 )
ILLEGAL_VECTOR( 0xf00 )
ILLEGAL_VECTOR( 0xf20 )
ILLEGAL_VECTOR( 0x1000 )
ILLEGAL_VECTOR( 0x1100 )
ILLEGAL_VECTOR( 0x1200 )
ILLEGAL_VECTOR( 0x1300 )
ILLEGAL_VECTOR( 0x1400 )
ILLEGAL_VECTOR( 0x1500 )
ILLEGAL_VECTOR( 0x1600 )
ILLEGAL_VECTOR( 0x1700 )

exception_return:
    addi	r1,r1,16	// pop ABI frame

    lwz	r0,52(r1)
    mtlr	r0
    lwz	r0,56(r1)
    mtcr	r0
    lwz	r0,60(r1)
    mtctr	r0
    lwz	r0,64(r1)
    mtxer	r0

    lwz	r0,0(r1)	// restore r0
    lwz	r2,8(r1)	// restore r2
    lwz	r3,12(r1)	// restore r3
    lwz	r4,16(r1)
    lwz	r5,20(r1)
    lwz	r6,24(r1)
    lwz	r7,28(r1)
    lwz	r8,32(r1)
    lwz	r9,36(r1)
    lwz	r10,40(r1)
    lwz	r11,44(r1)
    lwz	r12,48(r1)
    lwz	r1,4(r1)	// restore r1
    rfi

GLOBL(__vectors_end):

/************************************************************************/
/*	entry								*/
/************************************************************************/

    .text
GLOBL(_start):
    //
    // We may be entered with translation on through a virtual alias of our
    // load address (the Dolphin emulator enters at 0x9xxxxxxx). Drop to real
    // mode at the linked physical address. Harmless when already in real mode.
    //
    LOAD_REG_IMMEDIATE(r3, _start_real)
    mtsrr0	r3
    li		r0, 0
    mtsrr1	r0
    rfi

_start_real:
    //
    // Ensure MMU is off.
    //
    li		r0, 0
    mtmsr	r0

    //
    // Set HID0 to initial state.
    // HID0 = 00110c64
    // bus checkstops off, sleep modes off,
    // caches off, caches invalidate,
    // store gathering off, enable data cache
    // flush assist, enable branch target cache,
    // enable branch history table
    //
    lis   r8, 		0x0011
    ori   r8, 		r8, 0x0C64
    mtspr S_HID0,	r8
    isync

    //
    // Enable floating-point.
    //
    li    r4,	MSR_FP
    mtmsr r4

    //
    // Enable L1 caches.
    //
    ori   r8, 		r8, 0xC000
    mtspr S_HID0,	r8
    isync

    //
    // Clear all BATs now that MMU is off.
    // BATs 4-7 are unique to 750CL (Broadway) and newer, XNU and Mac OS is unaware of these so they will stay disabled.
    //
    // IBAT0-3
    mtspr S_IBAT0L,	r0
    mtspr S_IBAT0U,	r0
    mtspr S_IBAT1L,	r0
    mtspr S_IBAT1U,	r0
    mtspr S_IBAT2L,	r0
    mtspr S_IBAT2U,	r0
    mtspr S_IBAT3L,	r0
    mtspr S_IBAT3U,	r0

    // DBAT0-3
    mtspr S_DBAT0L,	r0
    mtspr S_DBAT0U,	r0
    mtspr S_DBAT1L,	r0
    mtspr S_DBAT1U,	r0
    mtspr S_DBAT2L,	r0
    mtspr S_DBAT2U,	r0
    mtspr S_DBAT3L,	r0
    mtspr S_DBAT3U,	r0

    // IBAT4-7
    mtspr S_IBAT4L,	r0
    mtspr S_IBAT4U,	r0
    mtspr S_IBAT5L,	r0
    mtspr S_IBAT5U,	r0
    mtspr S_IBAT6L,	r0
    mtspr S_IBAT6U,	r0
    mtspr S_IBAT7L,	r0
    mtspr S_IBAT7U,	r0

    // DBAT4-7
    mtspr S_DBAT4L,	r0
    mtspr S_DBAT4U,	r0
    mtspr S_DBAT5L,	r0
    mtspr S_DBAT5U,	r0
    mtspr S_DBAT6L,	r0
    mtspr S_DBAT6U,	r0
    mtspr S_DBAT7L,	r0
    mtspr S_DBAT7U,	r0
    sync
    isync

    //
    // Clear all SRs.
    //
    lis 	r8, 0x8000
    mtsr  	0, r8
    mtsr  	1, r8
    mtsr  	2, r8
    mtsr  	3, r8
    mtsr	4, r8
    mtsr  	5, r8
    mtsr  	6, r8
    mtsr  	7, r8
    mtsr  	8, r8
    mtsr  	9, r8
    mtsr 	10, r8
    mtsr 	11, r8
    mtsr 	12, r8
    mtsr 	13, r8
    mtsr 	14, r8
    mtsr 	15, r8
    isync

    //
    // Ensure BAT4-7 is disabled in HID4.
    //
    mfspr   8, 1011
    rlwinm  8, 8, 0, 7, 5
    mtspr   1011, 8
    sync
    isync

    //
    // Copy exception vectors.
    //
    LOAD_REG_IMMEDIATE(r3, __vectors)
    li	    r4,0
    li	    r5,__vectors_end - __vectors + 16
    rlwinm	r5,r5,0,0,28
1:	lwz	    r6,0(r3)
    lwz	    r7,4(r3)
    lwz	    r8,8(r3)
    lwz	    r9,12(r3)
    stw	    r6,0(r4)
    stw	    r7,4(r4)
    stw	    r8,8(r4)
    stw	    r9,12(r4)
    dcbst	0,r4
    sync
    icbi	0,r4
    sync
    addi	r5,r5,-16
    addi	r3,r3,16
    addi	r4,r4,16
    cmpwi	r5,0
    bgt	1b
    isync

    //
    // Zero out BSS. The loader running on the IOP does not do this.
    //
    LOAD_REG_IMMEDIATE(r3, _bss)
    LOAD_REG_IMMEDIATE(r5, _ebss)
    sub     r5, r5, r3
    addi    r5, r5, 16
    li      r0, 0
    rlwinm	r5,r5,0,0,28
2:  stw     r0, 0(r3)
    stw     r0, 4(r3)
    stw     r0, 8(r3)
    stw     r0, 12(r3)
    dcbst   0, r3
    sync
    addi	r5, r5, -16
    addi	r3, r3, 16
    cmpwi	r5, 0
	bgt	2b
	isync

    //
    // Clear/initialize the L1 and L2 caches.
    //
    // Check if instruction cache is enabled. If not, enable it.
    //
    mfspr	r3, S_HID0
    rlwinm	r0, r3, 0, 16, 16
    cmplwi	r0, 0x0000
    bne		icache_enabled

    isync
    ori		r3, r3, HID0_ICE
    mtspr	S_HID0, r3

icache_enabled:
    //
    // Check if data cache is enabled. If not, enable it.
    //
    mfspr	r3, S_HID0
    rlwinm	r0, r3, 0, 17, 17
    cmplwi	r0, 0x0000
    bne		dcache_enabled

    isync
    ori		r3, r3, HID0_DCE
    mtspr	S_HID0, r3

dcache_enabled:
    //
    // Check to see if the L2 cache is ready, if not need to disable and invalidate.
    //
    mfspr	r3, S_L2CR
    clrrwi	r0, r3, 31
    cmplwi	r0, 0x0000
    bne		l2c_ready

    //
    // Disable L2 cache.
    //
    mfspr	r3, S_L2CR
    clrlwi	r3, r3, (0 + 1)
    mtspr	S_L2CR, r3
    sync

    //
    // Start L2 invalidation and wait for completion.
    //
    mfspr	r3, S_L2CR
    oris	r3, r3, 0x00000020
    mtspr	S_L2CR, r3

l2c_invalidate_loop:
    mfspr	r3, S_L2CR
    clrlwi	r0, r3, 31
    cmplwi	r0, 0x0000
    bne		l2c_invalidate_loop

    //
    // Enable the L2 cache, clear L2 data-only and global invalidate bits.
    //
    mfspr	r3, S_L2CR
    oris	r0, r3, 0x8000
    rlwinm	r3, r0, 0, (10 + 1), 9
    mtspr	S_L2CR, r3

l2c_ready:
    //
    // Configure the stacks.
    //
    LOAD_REG_IMMEDIATE(r1, __exception_stack)
    mtsprg0     r1
    LOAD_REG_IMMEDIATE(r1, __stack)

    //
    // Configure MMU.
    //
    bl	BRANCH_LABEL(setup_mmu)

    //
    // Jump into the context.
    //
    LOAD_REG_IMMEDIATE(r4, __context)
    PPC_LL  r4, 0(r4)
    PPC_STL r1, (2 * ULONG_SIZE)(r4)

    bl	BRANCH_LABEL(__switch_context_nosave)
1:	nop
    b	1b

#define STKOFF 8
#define SAVE_SPACE 144

GLOBL(of_client_callback):
    PPC_STLU r1, -STACKFRAME_MINSIZE(r1) /* fits within alignment */

    /* save r4 */
    PPC_STL r4, STKOFF(r1)

    /* save lr */
    mflr	r4
    PPC_STL r4, PPC_LR_STKOFF(r1)

    /* switch to OF stack */
    LOAD_REG_IMMEDIATE(r4, __client_stack)

    PPC_STLU r4, -SAVE_SPACE(r4)
    PPC_STL  r1, (STKOFF)(r4)	// save caller stack
    mr	r1,r4

    PPC_STL  r3,  (STKOFF + 5 * ULONG_SIZE)(r1)
    PPC_STL  r2,  (STKOFF + 4 * ULONG_SIZE)(r1)
    PPC_STL  r0,  (STKOFF + 3 * ULONG_SIZE)(r1)

    /* save ctr, cr and xer */
    mfctr	r2
    PPC_STL  r2,  (STKOFF + 6 * ULONG_SIZE)(r1)
    mfcr	r2
    PPC_STL  r2,  (STKOFF + 7 * ULONG_SIZE)(r1)
    mfxer	r2
    PPC_STL  r2,  (STKOFF + 8 * ULONG_SIZE)(r1)

    /* save r5 - r31 */
    PPC_STL  r5,  (STKOFF + 10 * ULONG_SIZE)(r1)
    PPC_STL  r6,  (STKOFF + 11 * ULONG_SIZE)(r1)
    PPC_STL  r7,  (STKOFF + 12 * ULONG_SIZE)(r1)
    PPC_STL  r8,  (STKOFF + 13 * ULONG_SIZE)(r1)
    PPC_STL  r9,  (STKOFF + 14 * ULONG_SIZE)(r1)
    PPC_STL  r10,  (STKOFF + 15 * ULONG_SIZE)(r1)
    PPC_STL  r11,  (STKOFF + 16 * ULONG_SIZE)(r1)
    PPC_STL  r12,  (STKOFF + 17 * ULONG_SIZE)(r1)
    PPC_STL  r13,  (STKOFF + 18 * ULONG_SIZE)(r1)
    PPC_STL  r14,  (STKOFF + 19 * ULONG_SIZE)(r1)
    PPC_STL  r15,  (STKOFF + 20 * ULONG_SIZE)(r1)
    PPC_STL  r16,  (STKOFF + 21 * ULONG_SIZE)(r1)
    PPC_STL  r17,  (STKOFF + 22 * ULONG_SIZE)(r1)
    PPC_STL  r18,  (STKOFF + 23 * ULONG_SIZE)(r1)
    PPC_STL  r19,  (STKOFF + 24 * ULONG_SIZE)(r1)
    PPC_STL  r20,  (STKOFF + 25 * ULONG_SIZE)(r1)
    PPC_STL  r21,  (STKOFF + 26 * ULONG_SIZE)(r1)
    PPC_STL  r22,  (STKOFF + 27 * ULONG_SIZE)(r1)
    PPC_STL  r23,  (STKOFF + 28 * ULONG_SIZE)(r1)
    PPC_STL  r24,  (STKOFF + 29 * ULONG_SIZE)(r1)
    PPC_STL  r25,  (STKOFF + 30 * ULONG_SIZE)(r1)
    PPC_STL  r26,  (STKOFF + 31 * ULONG_SIZE)(r1)
    PPC_STL  r27,  (STKOFF + 32 * ULONG_SIZE)(r1)
    PPC_STL  r28,  (STKOFF + 33 * ULONG_SIZE)(r1)
    PPC_STL  r29,  (STKOFF + 34 * ULONG_SIZE)(r1)
    PPC_STL  r30,  (STKOFF + 35 * ULONG_SIZE)(r1)
    PPC_STL  r31,  (STKOFF + 36 * ULONG_SIZE)(r1)

    bl	BRANCH_LABEL(of_client_interface)

    /* restore r5 - r31 */
    PPC_LL  r5,  (STKOFF + 10 * ULONG_SIZE)(r1)
    PPC_LL  r6,  (STKOFF + 11 * ULONG_SIZE)(r1)
    PPC_LL  r7,  (STKOFF + 12 * ULONG_SIZE)(r1)
    PPC_LL  r8,  (STKOFF + 13 * ULONG_SIZE)(r1)
    PPC_LL  r9,  (STKOFF + 14 * ULONG_SIZE)(r1)
    PPC_LL  r10,  (STKOFF + 15 * ULONG_SIZE)(r1)
    PPC_LL  r11,  (STKOFF + 16 * ULONG_SIZE)(r1)
    PPC_LL  r12,  (STKOFF + 17 * ULONG_SIZE)(r1)
    PPC_LL  r13,  (STKOFF + 18 * ULONG_SIZE)(r1)
    PPC_LL  r14,  (STKOFF + 19 * ULONG_SIZE)(r1)
    PPC_LL  r15,  (STKOFF + 20 * ULONG_SIZE)(r1)
    PPC_LL  r16,  (STKOFF + 21 * ULONG_SIZE)(r1)
    PPC_LL  r17,  (STKOFF + 22 * ULONG_SIZE)(r1)
    PPC_LL  r18,  (STKOFF + 23 * ULONG_SIZE)(r1)
    PPC_LL  r19,  (STKOFF + 24 * ULONG_SIZE)(r1)
    PPC_LL  r20,  (STKOFF + 25 * ULONG_SIZE)(r1)
    PPC_LL  r21,  (STKOFF + 26 * ULONG_SIZE)(r1)
    PPC_LL  r22,  (STKOFF + 27 * ULONG_SIZE)(r1)
    PPC_LL  r23,  (STKOFF + 28 * ULONG_SIZE)(r1)
    PPC_LL  r24,  (STKOFF + 29 * ULONG_SIZE)(r1)
    PPC_LL  r25,  (STKOFF + 30 * ULONG_SIZE)(r1)
    PPC_LL  r26,  (STKOFF + 31 * ULONG_SIZE)(r1)
    PPC_LL  r27,  (STKOFF + 32 * ULONG_SIZE)(r1)
    PPC_LL  r28,  (STKOFF + 33 * ULONG_SIZE)(r1)
    PPC_LL  r29,  (STKOFF + 34 * ULONG_SIZE)(r1)
    PPC_LL  r30,  (STKOFF + 35 * ULONG_SIZE)(r1)
    PPC_LL  r31,  (STKOFF + 36 * ULONG_SIZE)(r1)

    /* restore ctr, cr and xer */
    PPC_LL r2,  (STKOFF + 6 * ULONG_SIZE)(r1)
    mtctr	r2
    PPC_LL r2,  (STKOFF + 7 * ULONG_SIZE)(r1)
    mtcr	r2
    PPC_LL r2,  (STKOFF + 8 * ULONG_SIZE)(r1)
    mtxer	r2

    /* restore r0 and r2 */
    PPC_LL r2,  (STKOFF + 4 * ULONG_SIZE)(r1)
    PPC_LL r0,  (STKOFF + 3 * ULONG_SIZE)(r1)

    /* restore caller stack */
    PPC_LL  r1,  (STKOFF)(r1)

    PPC_LL  r4, PPC_LR_STKOFF(r1)
    mtlr	r4
    PPC_LL  r4, STKOFF(r1)
    PPC_LL  r1, 0(r1)

    blr

    /* rtas glue (must be reloctable) */
GLOBL(of_rtas_start):
    /* r3 = argument buffer, r4 = of_rtas_start */
    /* according to the CHRP standard, cr must be preserved (cr0/cr1 too?) */
    blr
GLOBL(of_rtas_end):


#define CACHE_LINE_SIZE         32
#define LG_CACHE_LINE_SIZE      5

/* flush_icache_range( unsigned long start, unsigned long stop) */
_GLOBAL(flush_icache_range):
        li      r5,CACHE_LINE_SIZE-1
        andc    r3,r3,r5
        subf    r4,r3,r4
        add     r4,r4,r5
        srwi.   r4,r4,LG_CACHE_LINE_SIZE
        beqlr
        mtctr   r4
        mr      r6,r3
1:      dcbst   0,r3
        addi    r3,r3,CACHE_LINE_SIZE
        bdnz    1b
        sync                            /* wait for dcbst's to get to ram */
        mtctr   r4
2:      icbi    0,r6
        addi    r6,r6,CACHE_LINE_SIZE
        bdnz    2b
        sync                            /* additional sync needed on g4 */
        isync
        blr

/* flush_dcache_range( unsigned long start, unsigned long stop) */
_GLOBAL(flush_dcache_range):
        li      r5,CACHE_LINE_SIZE-1
        andc    r3,r3,r5
        subf    r4,r3,r4
        add     r4,r4,r5
        srwi.   r4,r4,LG_CACHE_LINE_SIZE
        beqlr
        mtctr   r4
        mr      r6,r3
1:      dcbst   0,r3
        addi    r3,r3,CACHE_LINE_SIZE
        bdnz    1b
        sync                            /* wait for dcbst's to get to ram */
        mtctr   r4
2:      dcbi    0,r6
        addi    r6,r6,CACHE_LINE_SIZE
        bdnz    2b
        sync
        blr

/* invalidate_dcache_range( unsigned long start, unsigned long stop) */
_GLOBAL(invalidate_dcache_range):
        li      r5,CACHE_LINE_SIZE-1
        andc    r3,r3,r5
        subf    r4,r3,r4
        add     r4,r4,r5
        srwi.   r4,r4,LG_CACHE_LINE_SIZE
        beqlr
        mtctr   r4
        mr      r6,r3
1:      dcbi    0,r6
        addi    r6,r6,CACHE_LINE_SIZE
        bdnz    1b
        sync
        blr
