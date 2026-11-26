/*
 * testglobe - timer.device clock shared by the Amiga chipset and MiniGL targets.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Intuition's CurrentTime advances only with input/vertical-blank updates,
 * which steps animation and quantizes FPS. timer.device answers TR_GETSYSTIME
 * on AmigaOS 1.3 and reads the free-running E-clock from V36. Compile this
 * object with its target's baseline options; it has no FPU-specific code.
 */
#include "testglob.h"
#if defined(DRV_AMI) || defined(OGL_AMIGA)
#include <exec/types.h>
#include <exec/devices.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <devices/timer.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <clib/alib_protos.h>
#ifdef __VBCC__
#include <inline/exec_protos.h>
#include <inline/intuition_protos.h>
#endif
#include "drv_ext.h"
#include "ami_time.h"

/* ReadEClock is the only timer.device function vector used. A private
 * veneer keeps its base in a6 without a TimerBase declaration, whose type
 * differs between NDK releases. Offset/registers: timer.fd. */
#ifdef __VBCC__
ULONG ami_clock_eclock(__reg("a6") struct Device *base,
    __reg("a0") struct EClockVal *value) = "\tjsr\t-60(a6)";
#else
/* Host contract tests provide a recording implementation. */
extern ULONG ami_clock_eclock(struct Device *base, struct EClockVal *value);
#endif

#define AMI_CLOCK_INTUITION 0
#define AMI_CLOCK_SYSTIME 1
#define AMI_CLOCK_ECLOCK 2

/* amiga.lib creates the reply port and request on every OS version, where
 * CreateMsgPort/CreateIORequest would need V36. */
static struct MsgPort *ami_clock_port;
static struct timerequest *ami_clock_request;
static int ami_clock_source;
static Drv_clock ami_clock_state;

void ami_clock_close(void)
{
    if (ami_clock_source != AMI_CLOCK_INTUITION)
        CloseDevice((struct IORequest *)ami_clock_request);
    if (ami_clock_request != NULL)
        DeleteExtIO((struct IORequest *)ami_clock_request);
    if (ami_clock_port != NULL) DeletePort(ami_clock_port);
    ami_clock_request = NULL;
    ami_clock_port = NULL;
    ami_clock_source = AMI_CLOCK_INTUITION;
    /* A different source has another origin: rebase, keeping elapsed time. */
    ami_clock_state.initialized = FALSE;
}

void ami_clock_open(void)
{
    ami_clock_close();
    ami_clock_port = CreatePort(NULL, 0L);
    if (ami_clock_port != NULL)
        ami_clock_request = (struct timerequest *)
            CreateExtIO(ami_clock_port, (LONG)sizeof(struct timerequest));
    /* Every unit answers TR_GETSYSTIME; vertical blank is the cheapest. */
    if (ami_clock_request != NULL &&
        OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK,
                   (struct IORequest *)ami_clock_request, 0UL) == 0)
    {
        /* Keep one source for the program's lifetime: E-clock and system
         * time have different origins and cannot be mixed in one clock. */
        ami_clock_source =
            ami_clock_request->tr_node.io_Device->dd_Library.lib_Version >= 36 ?
            AMI_CLOCK_ECLOCK : AMI_CLOCK_SYSTIME;
        return;
    }
    ami_clock_close();
}

double ami_clock_now(void)
{
    struct EClockVal ticks;
    ULONG frequency, seconds, micros;
    if (ami_clock_source == AMI_CLOCK_ECLOCK)
    {
        frequency = ami_clock_eclock(ami_clock_request->tr_node.io_Device, &ticks);
        if (frequency == 0) return ami_clock_state.elapsed;
        return drv_elapsed_time(&ami_clock_state,
            ((double)ticks.ev_hi * 4294967296.0 + (double)ticks.ev_lo) /
            (double)frequency);
    }
    if (ami_clock_source == AMI_CLOCK_SYSTIME)
    {
        ami_clock_request->tr_node.io_Command = TR_GETSYSTIME;
        if (DoIO((struct IORequest *)ami_clock_request) != 0)
            return ami_clock_state.elapsed;
        seconds = ami_clock_request->tr_time.tv_secs;
        micros = ami_clock_request->tr_time.tv_micro;
    }
    else
    {
        if (IntuitionBase == NULL) return ami_clock_state.elapsed;
        CurrentTime(&seconds, &micros);
    }
    return drv_elapsed_time(&ami_clock_state,
                            (double)seconds + (double)micros / 1000000.0);
}
#endif /* DRV_AMI || OGL_AMIGA */
