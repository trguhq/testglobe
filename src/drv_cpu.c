/*
 * testglobe - real-mode DOS CPU and x87 detection for Microsoft C 7.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Compile this file with /G0 /FPi. The compiler-generated code and the
 * beginning of the probe must run before the processor has been identified.
 * Only the guarded EFLAGS probe uses 386 instructions. A 486 or newer CPU
 * uses the same separately compiled /G3 integer kernel as a 386.
 */
#include <dos.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>
#include "drv_ext.h"

#if !defined(_MSC_VER) || !defined(_M_I86)
#error drv_cpu.c requires the 16-bit Microsoft C DOS compiler
#endif

int drv_cpu_detect(void)
{
    int result;
    _asm {
        push bx
        push si
        pushf
        pop dx

        /* 8086/80186 FLAGS bits 12..15 remain set. */
        mov ax, dx
        and ax, 0fffh
        push ax
        popf
        pushf
        pop ax
        and ax, 0f000h
        cmp ax, 0f000h
        jne drv_probe_at
        mov ax, 86
        jmp drv_probe_done

    drv_probe_at:
        /* In real mode a 286 cannot set these high FLAGS bits. */
        mov ax, dx
        or ax, 0f000h
        push ax
        popf
        pushf
        pop ax
        and ax, 0f000h
        jnz drv_probe_386
        mov ax, 286
        jmp drv_probe_done

    drv_probe_386:
        push dx
        popf
        /* Keep every 32-bit stack access aligned. Do not use ESP to align
         * a 16-bit DOS stack: its upper half is not part of this ABI. */
        mov bx, sp
        and sp, 0fffch
        /* Emit the operand-size override explicitly so /G0 need not
         * enable 386 assembler mnemonics for the rest of this file. */
        _emit 066h
        _emit 09ch                  /* PUSHFD */
        pop ax                      /* original EFLAGS low word */
        pop si                      /* original EFLAGS high word */
        mov cx, si
        xor cx, 4                   /* AC is bit 18 */
        push cx
        push ax
        _emit 066h
        _emit 09dh                  /* POPFD */
        _emit 066h
        _emit 09ch                  /* PUSHFD */
        pop ax
        pop cx                      /* observed EFLAGS high word */
        push si
        push ax
        _emit 066h
        _emit 09dh                  /* restore original EFLAGS */
        mov sp, bx
        xor cx, si
        and cx, 4
        jz drv_probe_is386
        mov ax, 486
        jmp drv_probe_done
    drv_probe_is386:
        mov ax, 386
    drv_probe_done:
        mov result, ax
        push dx
        popf
        pop si
        pop bx
    }
    return result;
}

/* The no-wait stores may finish asynchronously on an old coprocessor.
 * Static storage remains valid even if a broken device misses the deadline. */
static volatile unsigned short drv_fp_probe[2];

int drv_cpu_has_fpu(void)
{
    union REGS regs;
    volatile unsigned short _far *probe = drv_fp_probe;
    const char *disabled;
    unsigned int remaining;
    int available = 0;

    disabled = getenv("NO87");
    if (disabled != NULL && *disabled != '\0') return 0;
    memset(&regs, 0, sizeof(regs));
    int86(0x11, &regs, &regs);
    if ((regs.x.ax & 2U) == 0U) return 0;

    drv_fp_probe[0] = drv_fp_probe[1] = 0xffffU;
    _asm {
        push es
        push bx
        les bx, probe
        /* No FWAIT: an absent coprocessor must never stall this probe.
         * These memory forms avoid the 387-only FNSTSW AX instruction. */
        fninit
        fnstsw word ptr es:[bx]
        fnstcw word ptr es:[bx+2]
        pop bx
        pop es
    }
    for (remaining = 65535U; remaining != 0U; --remaining)
    {
        if (drv_fp_probe[0] == 0U &&
            (drv_fp_probe[1] & 0x103fU) == 0x003fU)
        {
            available = 1;
            break;
        }
    }
    /* Restore the compiler runtime's normal control word after FNINIT.
     * /FPi keeps this safe even when the runtime selected emulation. */
    _fpreset();
    return available;
}
