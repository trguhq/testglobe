/*
 * testglobe - IBM Professional Graphics Controller commands.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The communication area is 1 KB of shared memory: a 256-byte command
 * FIFO at offset 0, the read-back FIFO at 100h and the error FIFO at 200h,
 * then each FIFO's write and read pointer bytes at 300h-305h and the error
 * enable flag at 308h. A FIFO is empty when its pointers are equal and
 * full when the write pointer is one less than the read pointer (modulo
 * 256). The host owns the command write pointer and the other two read
 * pointers. A board stops taking commands while its error FIFO is full,
 * so every wait here also drains that FIFO.
 *
 * Commands are hex mode: an opcode byte, Ints as two bytes (low first) and
 * Reals as 16.16 with the integer word first. Matrox's PG-640A reports
 * board type 2 at offset 3F8h and has FLAGRD flags beyond the PGC's 25.
 *
 * The communication area is the board processor's own memory. For each
 * host access the board holds its 8088 off its bus (HOLD) and stretches
 * the host's cycle with I/O CH RDY until it may proceed (IBM Technical
 * Reference: system-bus interface and processor-card logic). A host that
 * polls back to back keeps the board's processor stopped; a fast CPU does
 * little else between accesses and can starve the very command it waits
 * for. Every wait therefore pauses between polls for a time set by I/O
 * bus cycles, not CPU speed.
 */
#include "testglob.h"
#if defined(DRV_MTX)
#include <time.h>
#include "mtx_pgc.h"

#define PGC_OUT 0x000U
#define PGC_IN 0x100U
#define PGC_ERR 0x200U
#define PGC_OUT_WRITE 0x300U
#define PGC_OUT_READ 0x301U
#define PGC_IN_WRITE 0x302U
#define PGC_IN_READ 0x303U
#define PGC_ERR_WRITE 0x304U
#define PGC_ERR_READ 0x305U
#define PGC_COLD_RESTART 0x306U
#define PGC_WARM_RESTART 0x307U
#define PGC_ERROR_ENABLE 0x308U
#define PGC_BOARD 0x3F8U
#define PGC_BOARD_PG640A 2U

/* Deadlines in forward clock ticks: a reply or free FIFO space, and the
 * shorter wait when probing for a board or identifying it. */
#define PGC_WAIT_TICKS (10UL * (unsigned long)CLOCKS_PER_SEC)
#define PGC_PROBE_TICKS (2UL * (unsigned long)CLOCKS_PER_SEC)
/* A cold restart (a previous program's, or power-up on a fast machine)
 * runs the board's self-tests before it clears the flag. */
#define PGC_RESET_TICKS (15UL * (unsigned long)CLOCKS_PER_SEC)
/* Port reads between polls: ISA I/O cycles of about a microsecond each, so
 * the board's 8088 has the bus for all but a few percent of a wait. */
#define PGC_PAUSE_READS 64U

#ifdef MTX_PGC_TEST
extern unsigned char mtx_pgc_test_read(unsigned int offset);
extern void mtx_pgc_test_write(unsigned int offset, unsigned char value);
extern clock_t mtx_pgc_test_clock(void);
extern void mtx_pgc_test_select(unsigned int segment);
extern void mtx_pgc_test_pause(void);
#define PGC_READ(offset) mtx_pgc_test_read(offset)
#define PGC_WRITE(offset, value) mtx_pgc_test_write((offset), (unsigned char)(value))
#define PGC_CLOCK() mtx_pgc_test_clock()
#define PGC_PAUSE() mtx_pgc_test_pause()
#else
#include <conio.h>
static volatile unsigned char _far *pgc_area;
#define PGC_READ(offset) (pgc_area[offset])
#define PGC_WRITE(offset, value) (pgc_area[offset] = (unsigned char)(value))
#define PGC_CLOCK() clock()
#define PGC_PAUSE() pgc_pause()
/* System control port B (61h): reading it changes nothing. */
static void pgc_pause(void)
{
    unsigned int i;
    for (i = 0; i < PGC_PAUSE_READS; ++i) (void)inp(0x61);
}
#endif

static const unsigned int pgc_segments[] = { 0xC600U, 0xC640U };
static unsigned int pgc_seg = 0xC600U;
static int pgc_found, pgc_is_640a, pgc_stopped;
static unsigned int pgc_errors;
static unsigned char pgc_saved_enable;
static unsigned long pgc_limit = PGC_WAIT_TICKS;
/* The host's copy of the command write pointer, and bytes known free. */
static unsigned int pgc_write, pgc_room;

typedef struct Pgc_wait
{
    clock_t previous;
    unsigned long elapsed;
    int started;
} Pgc_wait;

static void pgc_wait_begin(Pgc_wait *wait)
{
    wait->elapsed = 0UL;
    wait->started = FALSE;
}

/* TRUE once limit ticks of forward clock time have passed; a clock step
 * backward neither ends the wait nor restarts it. */
static int pgc_wait_over(Pgc_wait *wait, unsigned long limit)
{
    clock_t now = PGC_CLOCK();
    if (now == (clock_t)-1) return TRUE;
    if (wait->started && now > wait->previous)
    {
        if ((unsigned long)(now - wait->previous) >= limit - wait->elapsed)
            wait->elapsed = limit;
        else wait->elapsed += (unsigned long)(now - wait->previous);
    }
    wait->previous = now;
    wait->started = TRUE;
    return wait->elapsed >= limit;
}

int pgc_pg640a(void) { return pgc_is_640a; }
int pgc_failed(void) { return pgc_stopped; }
unsigned int pgc_segment(void) { return pgc_seg; }

unsigned int pgc_take_errors(void)
{
    unsigned int errors = pgc_errors;
    pgc_errors = 0U;
    return errors;
}

static void pgc_drain_errors(void)
{
    unsigned int read, code, count;
    read = PGC_READ(PGC_ERR_READ);
    for (count = 0; count < 256U && read != PGC_READ(PGC_ERR_WRITE); ++count)
    {
        code = PGC_READ(PGC_ERR + read);
        pgc_errors |= code < 15U ? 1U << code : 0x8000U;
        read = (read + 1U) & 255U;
        PGC_WRITE(PGC_ERR_READ, read);
    }
}

static void pgc_publish(void)
{
    if (pgc_found && !pgc_stopped) PGC_WRITE(PGC_OUT_WRITE, pgc_write);
}

static void pgc_byte(unsigned char value)
{
    Pgc_wait wait;
    if (!pgc_found || pgc_stopped) return;
    if (pgc_room == 0U)
    {
        pgc_publish();
        pgc_wait_begin(&wait);
        for (;;)
        {
            pgc_room = ((unsigned int)PGC_READ(PGC_OUT_READ) - pgc_write - 1U) & 255U;
            if (pgc_room != 0U) break;
            pgc_drain_errors();
            if (pgc_wait_over(&wait, pgc_limit))
            {
                pgc_stopped = TRUE;
                return;
            }
            PGC_PAUSE();
        }
    }
    PGC_WRITE(PGC_OUT + pgc_write, value);
    pgc_write = (pgc_write + 1U) & 255U;
    --pgc_room;
}

static void pgc_int(int value)
{
    unsigned int bits = (unsigned int)value;
    pgc_byte((unsigned char)(bits & 0xFFU));
    pgc_byte((unsigned char)((bits >> 8) & 0xFFU));
}

/* 16.16 with the integer word first, each word low byte first. */
static void pgc_fixed(long value)
{
    unsigned long bits = (unsigned long)value;
    pgc_byte((unsigned char)((bits >> 16) & 0xFFUL));
    pgc_byte((unsigned char)((bits >> 24) & 0xFFUL));
    pgc_byte((unsigned char)(bits & 0xFFUL));
    pgc_byte((unsigned char)((bits >> 8) & 0xFFUL));
}

static void pgc_real(double value)
{
    long fixed;
    if (!(value == value)) fixed = 0L;
    else if (value >= 32767.0 + 65535.0 / 65536.0) fixed = 0x7FFFFFFFL;
    else if (value <= -32768.0) fixed = -0x7FFFFFFFL - 1L;
    else fixed = (long)(value * 65536.0);
    pgc_fixed(fixed);
}

/* Wait for count read-back bytes. A failed wait ends communication only
 * when fail is set; probes and identification just report it. */
static int pgc_read(unsigned char *reply, unsigned int count,
                    unsigned long limit, int fail)
{
    Pgc_wait wait;
    unsigned int read;
    if (!pgc_found || pgc_stopped) return FALSE;
    pgc_publish();
    pgc_wait_begin(&wait);
    while (count != 0U)
    {
        read = PGC_READ(PGC_IN_READ);
        if (read != PGC_READ(PGC_IN_WRITE))
        {
            *reply++ = PGC_READ(PGC_IN + read);
            PGC_WRITE(PGC_IN_READ, (read + 1U) & 255U);
            --count;
            continue;
        }
        pgc_drain_errors();
        if (pgc_wait_over(&wait, limit))
        {
            if (fail) pgc_stopped = TRUE;
            return FALSE;
        }
        PGC_PAUSE();
    }
    /* Commands run in order: errors from those before the request are
     * already in the error FIFO once its reply is. */
    pgc_drain_errors();
    return TRUE;
}

int pgc_flagrd(unsigned char flag, unsigned char *reply, unsigned int bytes)
{
    pgc_byte(0x51U);
    pgc_byte(flag);
    return pgc_read(reply, bytes, pgc_limit, TRUE);
}

/* TRUE once the board clears a restart flag within limit ticks. */
static int pgc_flag_clears(unsigned int offset, unsigned long limit)
{
    Pgc_wait wait;
    pgc_wait_begin(&wait);
    while (PGC_READ(offset) != 0U)
    {
        if (pgc_wait_over(&wait, limit)) return FALSE;
        PGC_PAUSE();
    }
    return TRUE;
}

/* Shared memory must keep the error enable flag's 0 and 1, and the board
 * must answer FLAGRD 4 after "CX ", which selects hex mode from either
 * mode. Otherwise the flags are restored and nothing else is touched but
 * the five bytes sent and the pointers.
 *   A cold restart in progress (flag 1) is waited out first. A warm
 * restart then stops a command list or DMA left by another program and
 * resets the FIFO pointers; on a PG-640A it also ends Self-test B, whose
 * last test exercises the FIFOs until the host writes this flag, so a
 * board with its self-test switch on answers nothing until then. The
 * board clears the flag when done; a board that leaves it set gets its
 * old value back and is probed as before. */
static int pgc_try(unsigned int segment)
{
    unsigned char saved, warm, reply;
    int restarted;
    pgc_seg = segment;
#ifdef MTX_PGC_TEST
    mtx_pgc_test_select(segment);
#else
    pgc_area = (volatile unsigned char _far *)((unsigned long)segment << 16);
#endif
    saved = PGC_READ(PGC_ERROR_ENABLE);
    PGC_WRITE(PGC_ERROR_ENABLE, 0U);
    if (PGC_READ(PGC_ERROR_ENABLE) != 0U) goto absent;
    PGC_WRITE(PGC_ERROR_ENABLE, 1U);
    if (PGC_READ(PGC_ERROR_ENABLE) != 1U) goto absent;
    if (PGC_READ(PGC_COLD_RESTART) == 1U &&
        !pgc_flag_clears(PGC_COLD_RESTART, PGC_RESET_TICKS)) goto absent;
    warm = PGC_READ(PGC_WARM_RESTART);
    PGC_WRITE(PGC_WARM_RESTART, 1U);
    restarted = pgc_flag_clears(PGC_WARM_RESTART, PGC_PROBE_TICKS);
    if (!restarted) PGC_WRITE(PGC_WARM_RESTART, warm);
    /* Stale replies and errors from an earlier program are discarded. */
    PGC_WRITE(PGC_IN_READ, PGC_READ(PGC_IN_WRITE));
    PGC_WRITE(PGC_ERR_READ, PGC_READ(PGC_ERR_WRITE));
    pgc_found = TRUE;
    pgc_stopped = FALSE;
    pgc_errors = 0U;
    pgc_write = PGC_READ(PGC_OUT_WRITE);
    pgc_room = 0U;
    pgc_limit = PGC_PROBE_TICKS;
    pgc_byte('C'); pgc_byte('X'); pgc_byte(' ');
    pgc_byte(0x51U); pgc_byte(4U);
    /* A board that acknowledged the warm restart is there; allow it the
     * full reply deadline. */
    if (pgc_read(&reply, 1U, restarted ? PGC_WAIT_TICKS : PGC_PROBE_TICKS, FALSE))
    {
        pgc_saved_enable = saved;
        return TRUE;
    }
    pgc_found = FALSE;
    pgc_stopped = FALSE;
absent:
    PGC_WRITE(PGC_ERROR_ENABLE, saved);
    return FALSE;
}

int pgc_open(void)
{
    unsigned char replies[2];
    unsigned int i;
    pgc_is_640a = FALSE;
    for (i = 0; i < sizeof(pgc_segments) / sizeof(pgc_segments[0]); ++i)
        if (pgc_try(pgc_segments[i])) break;
    if (!pgc_found) return FALSE;
    /* Both manuals' FLAGRD takes one byte. Only the PG-640A answers flag
     * 30 (text window); an IBM PGC answers only the second request. */
    if (PGC_READ(PGC_BOARD) == PGC_BOARD_PG640A)
    {
        pgc_byte(0x51U); pgc_byte(30U);
        pgc_byte(0x51U); pgc_byte(4U);
        if (pgc_read(replies, 2U, PGC_PROBE_TICKS, FALSE)) pgc_is_640a = TRUE;
        else PGC_WRITE(PGC_IN_READ, PGC_READ(PGC_IN_WRITE));
    }
    pgc_limit = PGC_WAIT_TICKS;
    (void)pgc_take_errors();
    return TRUE;
}

void pgc_close(void)
{
    Pgc_wait wait;
    if (!pgc_found) return;
    if (!pgc_stopped)
    {
        pgc_byte('C'); pgc_byte('A'); pgc_byte(' ');
        pgc_publish();
        /* Let the board take the queued commands before reports change. */
        pgc_wait_begin(&wait);
        while (PGC_READ(PGC_OUT_READ) != pgc_write &&
               !pgc_wait_over(&wait, PGC_PROBE_TICKS))
        {
            pgc_drain_errors();
            PGC_PAUSE();
        }
    }
    PGC_WRITE(PGC_ERROR_ENABLE, pgc_saved_enable);
    pgc_found = FALSE;
}

void pgc_resetf(void) { pgc_byte(0x04U); pgc_publish(); }
void pgc_displa(unsigned char flag) { pgc_byte(0xD0U); pgc_byte(flag); pgc_publish(); }
void pgc_clears(unsigned char color) { pgc_byte(0x0FU); pgc_byte(color); pgc_publish(); }
void pgc_color(unsigned char color) { pgc_byte(0x06U); pgc_byte(color); pgc_publish(); }
void pgc_prmfil(unsigned char flag) { pgc_byte(0xE9U); pgc_byte(flag); pgc_publish(); }
void pgc_lutsav(void) { pgc_byte(0xEDU); pgc_publish(); }
void pgc_lutint(unsigned char state) { pgc_byte(0xECU); pgc_byte(state); pgc_publish(); }

/* LUTX (E6h) takes 8-bit components on the PG-640A; LUT (EEh) takes
 * values modulo 16, so an IBM PGC gets each component's high four bits. */
void pgc_lut(unsigned char index, unsigned char red, unsigned char green,
             unsigned char blue)
{
    if (pgc_is_640a)
    {
        pgc_byte(0xE6U); pgc_byte(index);
        pgc_byte(red); pgc_byte(green); pgc_byte(blue);
    }
    else
    {
        pgc_byte(0xEEU); pgc_byte(index);
        pgc_byte((unsigned char)(red >> 4));
        pgc_byte((unsigned char)(green >> 4));
        pgc_byte((unsigned char)(blue >> 4));
    }
    pgc_publish();
}

void pgc_vwport(int x1, int x2, int y1, int y2)
{
    pgc_byte(0xB2U);
    pgc_int(x1); pgc_int(x2); pgc_int(y1); pgc_int(y2);
    pgc_publish();
}

void pgc_window(double x1, double x2, double y1, double y2)
{
    pgc_byte(0xB3U);
    pgc_real(x1); pgc_real(x2); pgc_real(y1); pgc_real(y2);
    pgc_publish();
}

/* The IBM manual gives PROJCT a one-byte angle; the PG-640A manual an Int. */
void pgc_projct(int angle)
{
    pgc_byte(0xB0U);
    if (pgc_is_640a) pgc_int(angle);
    else pgc_byte((unsigned char)angle);
    pgc_publish();
}

void pgc_distan(double distance) { pgc_byte(0xB1U); pgc_real(distance); pgc_publish(); }
void pgc_cliph(unsigned char flag) { pgc_byte(0xAAU); pgc_byte(flag); pgc_publish(); }
void pgc_clipy(unsigned char flag) { pgc_byte(0xABU); pgc_byte(flag); pgc_publish(); }
void pgc_mdiden(void) { pgc_byte(0x90U); pgc_publish(); }

void pgc_mdorg(double x, double y, double z)
{
    pgc_byte(0x91U);
    pgc_real(x); pgc_real(y); pgc_real(z);
    pgc_publish();
}

void pgc_mdrotz(int degrees) { pgc_byte(0x95U); pgc_int(degrees); pgc_publish(); }
void pgc_vwiden(void) { pgc_byte(0xA0U); pgc_publish(); }

void pgc_vwrpt(double x, double y, double z)
{
    pgc_byte(0xA1U);
    pgc_real(x); pgc_real(y); pgc_real(z);
    pgc_publish();
}

void pgc_vwrotx(int degrees) { pgc_byte(0xA3U); pgc_int(degrees); pgc_publish(); }

void pgc_poly3(unsigned int count, const Vertex TG_HUGE *const *vertices)
{
    unsigned int i;
    pgc_byte(0x32U);
    pgc_byte((unsigned char)count);
    for (i = 0; i < count; ++i)
    {
        pgc_fixed(soft3d_fixed16(&vertices[i]->x));
        pgc_fixed(soft3d_fixed16(&vertices[i]->y));
        pgc_fixed(soft3d_fixed16(&vertices[i]->z));
    }
    pgc_publish();
}

void pgc_poly(unsigned int count, const Soft3d_screen_point *points)
{
    unsigned int i;
    pgc_byte(0x30U);
    pgc_byte((unsigned char)count);
    for (i = 0; i < count; ++i)
    {
        pgc_fixed((long)((unsigned long)points[i].x << 16));
        pgc_fixed((long)((unsigned long)points[i].y << 16));
    }
    pgc_publish();
}

void pgc_move(double x, double y) { pgc_byte(0x10U); pgc_real(x); pgc_real(y); pgc_publish(); }
void pgc_tsize(double size) { pgc_byte(0x81U); pgc_real(size); pgc_publish(); }

void pgc_tjust(unsigned char horizontal, unsigned char vertical)
{
    pgc_byte(0x85U); pgc_byte(horizontal); pgc_byte(vertical);
    pgc_publish();
}

void pgc_text(const char *text)
{
    pgc_byte(0x80U);
    pgc_byte(0x22U);
    for (; *text != '\0'; ++text)
        pgc_byte(*text == '"' ? (unsigned char)'\'' : (unsigned char)*text);
    pgc_byte(0x22U);
    pgc_publish();
}
#endif
