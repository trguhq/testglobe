/* PGC command module against simulated IBM PGC and Matrox PG-640A boards. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "testglob.h"
#include "mtx_pgc.h"

#define KIND_ABSENT 0
#define KIND_RAM 1
#define KIND_PGC 2
#define LOG_BYTES 8192U

/* One communication area per probed segment: C6000h and C6400h. */
static unsigned char areas[2][1024];
static int kinds[2];
static int selected = -1;
/* Matrox firmware (FLAGRD 30, LUTX, two-byte PROJCT) or IBM's, and the
 * screen RESETF's viewport reports through FLAGRD 21. */
static int pg640a_model;
static unsigned int screen_x2 = 639U, screen_y2 = 479U;
/* Firmware parser state. */
static int hex_mode[2];
static unsigned char pending[64];
static unsigned int pending_count;
static unsigned char logged[LOG_BYTES];
static unsigned int logged_count;
static unsigned int paused_polls;
static int overrun, stall_errors;
static unsigned int queued_errors;
static unsigned char error_codes[8];
static clock_t now;
static unsigned long writes_to_area;
/* Restarts and bus sharing. hold_model: the board's processor runs only
 * after the host paused since its last turn, as HOLD stops it during host
 * accesses. warm_ignored: a board that leaves the warm restart flag set.
 * self_test_loop: PG-640A Self-test B5, which ends only on a warm restart.
 * cold_polls: polls left before a cold restart in progress clears 306h. */
static unsigned long pauses, pauses_seen;
static int hold_model, warm_ignored, self_test_loop;
static unsigned int cold_polls, warm_restarts;
void mtx_pgc_test_pause(void) { ++pauses; }

clock_t mtx_pgc_test_clock(void) { now += CLOCKS_PER_SEC / 20; return now; }

/* Parameter bytes after each hex opcode, -1 for variable length. */
static int parameter_length(unsigned char opcode, const unsigned char *bytes,
                            unsigned int available)
{
    switch (opcode)
    {
        case 0x04: case 0x90: case 0xA0: case 0xED: return 0;
        case 0x51: case 0xD0: case 0x0F: case 0x06: case 0xE9: case 0xEC:
        case 0xAA: case 0xAB: return 1;
        case 0x95: case 0xA3: case 0x85: return 2;
        case 0xB0: return pg640a_model ? 2 : 1;
        case 0xB1: case 0x81: case 0xEE: return 4;
        case 0xE6: assert(pg640a_model); return 4;
        case 0xB2: case 0x10: return 8;
        case 0x91: case 0xA1: return 12;
        case 0xB3: return 16;
        case 0x32: return available < 2U ? -1 : 1 + 12 * bytes[1];
        case 0x30: return available < 2U ? -1 : 1 + 8 * bytes[1];
        case 0x43: return 2;
        case 0x80:
        {
            unsigned int i;
            for (i = 2; i < available; ++i) if (bytes[i] == 0x22) return (int)i;
            return -1;
        }
        default:
            assert(!"Unknown PGC opcode");
            return 0;
    }
}

static void reply(unsigned char *area, unsigned char value)
{
    area[0x100 + area[0x302]] = value;
    area[0x302] = (unsigned char)(area[0x302] + 1);
}

static void execute(int which, const unsigned char *command, unsigned int length)
{
    unsigned char *area = areas[which];
    assert(logged_count + length < LOG_BYTES);
    memcpy(logged + logged_count, command, length);
    logged_count += length;
    if (command[0] == 0x43)
    {
        assert(command[2] == 0x20 && (command[1] == 'X' || command[1] == 'A'));
        hex_mode[which] = command[1] == 'X';
        return;
    }
    if (command[0] == 0x51)
    {
        if (command[1] == 30 && !pg640a_model) return; /* IBM: no such flag */
        if (command[1] == 21)
        {
            reply(area, 0); reply(area, 0);
            reply(area, (unsigned char)screen_x2); reply(area, (unsigned char)(screen_x2 >> 8));
            reply(area, 0); reply(area, 0);
            reply(area, (unsigned char)screen_y2); reply(area, (unsigned char)(screen_y2 >> 8));
            return;
        }
        reply(area, (unsigned char)(command[1] + 100));
    }
}

/* The board takes every published byte unless paused or its error FIFO
 * is full; ASCII mode only looks for "CX ". */
static void run_board(int which)
{
    unsigned char *area = areas[which];
    int length;
    if (kinds[which] != KIND_PGC) return;
    if (hold_model)
    {
        if (pauses == pauses_seen) return;
        pauses_seen = pauses;
    }
    if (area[0x306] == 1U)
    {
        if (cold_polls != 0U) { --cold_polls; return; }
        area[0x306] = 0;
    }
    if (area[0x307] != 0U && !warm_ignored)
    {
        /* Halt, reset the FIFO pointers, then clear the flag. */
        memset(area + 0x300, 0, 6);
        pending_count = 0;
        self_test_loop = 0;
        area[0x307] = 0;
        ++warm_restarts;
    }
    if (self_test_loop) return;
    if (paused_polls != 0U) { --paused_polls; return; }
    while (queued_errors != 0U &&
           (unsigned char)(area[0x304] + 1) != area[0x305])
    {
        area[0x200 + area[0x304]] = error_codes[--queued_errors];
        area[0x304] = (unsigned char)(area[0x304] + 1);
    }
    if (stall_errors && (unsigned char)(area[0x304] + 1) == area[0x305]) return;
    while (area[0x301] != area[0x300])
    {
        pending[pending_count++] = area[area[0x301]];
        area[0x301] = (unsigned char)(area[0x301] + 1);
        if (!hex_mode[which])
        {
            if (pending_count == 3U)
            {
                if (memcmp(pending, "CX ", 3) == 0) execute(which, pending, 3);
                memmove(pending, pending + 1, 2);
                pending_count = 2;
                if (hex_mode[which]) pending_count = 0;
            }
            continue;
        }
        length = parameter_length(pending[0], pending, pending_count);
        if (length >= 0 && pending_count == (unsigned int)length + 1U)
        {
            execute(which, pending, pending_count);
            pending_count = 0;
        }
        assert(pending_count < sizeof(pending));
    }
}

static int area_for(void) { return selected; }
void mtx_pgc_test_select(unsigned int segment)
{
    assert(segment == 0xC600U || segment == 0xC640U);
    selected = segment == 0xC640U ? 1 : 0;
}

unsigned char mtx_pgc_test_read(unsigned int offset)
{
    int which = area_for();
    assert(which >= 0 && offset < 1024U);
    if (kinds[which] == KIND_ABSENT) return 0xFF;
    if (offset == 0x301U || offset == 0x302U || offset == 0x304U ||
        offset == 0x306U || offset == 0x307U) run_board(which);
    return areas[which][offset];
}

void mtx_pgc_test_write(unsigned int offset, unsigned char value)
{
    int which = area_for();
    unsigned char *area;
    assert(which >= 0 && offset < 1024U);
    ++writes_to_area;
    if (kinds[which] == KIND_ABSENT) return;
    area = areas[which];
    if (offset == 0x300U)
    {
        /* The new write pointer may never pass the board's read pointer. */
        unsigned int unread = (unsigned char)(area[0x300] - area[0x301]);
        unsigned int added = (unsigned char)(value - area[0x300]);
        if (unread + added > 255U) overrun = 1;
    }
    area[offset] = value;
}

static void reset(int kind0, int kind1, int model_640a)
{
    memset(areas, 0, sizeof(areas));
    kinds[0] = kind0; kinds[1] = kind1;
    hex_mode[0] = hex_mode[1] = 0;
    pending_count = 0; logged_count = 0;
    paused_polls = 0; overrun = 0; stall_errors = 0; queued_errors = 0;
    pauses = pauses_seen = 0UL; hold_model = warm_ignored = self_test_loop = 0;
    cold_polls = warm_restarts = 0U;
    pg640a_model = model_640a;
    screen_x2 = 639U; screen_y2 = 479U;
    /* The IBM ROM marks its board with 3 at 3F8h at power-up. */
    if (!model_640a)
    {
        if (kind0 == KIND_PGC) areas[0][0x3F8] = 3;
        if (kind1 == KIND_PGC) areas[1][0x3F8] = 3;
    }
    selected = 0;
}

static void settle(void);
static void expect(const unsigned char *bytes, unsigned int count)
{
    unsigned int i;
    settle();
    assert(logged_count == count);
    for (i = 0; i < count; ++i) assert(logged[i] == bytes[i]);
    logged_count = 0;
}

static int open_board(void) { return pgc_open(); }
static void settle(void) { run_board(selected); }

int main(void)
{
    unsigned char value;
    unsigned int i;
    Vertex quad[3];
    const Vertex *corners[3];
    Soft3d_screen_point screen[3];

    /* IBM PGC at C6000h, with stale replies and errors and an ASCII-mode
     * board: hex mode, error reports on, stale data discarded. */
    reset(KIND_PGC, KIND_ABSENT, 0);
    areas[0][0x302] = 3; areas[0][0x304] = 2; areas[0][0x308] = 0;
    assert(open_board());
    assert(pgc_segment() == 0xC600U && !pgc_matrox() && !pgc_failed());
    assert(hex_mode[0] && areas[0][0x308] == 1);
    assert(warm_restarts == 1U && areas[0][0x307] == 0);
    assert(areas[0][0x303] == areas[0][0x302] && areas[0][0x305] == areas[0][0x304]);
    {
        static const unsigned char probe[] = { 0x43, 'X', ' ', 0x51, 4 };
        expect(probe, sizeof(probe));
    }
    assert(pgc_take_errors() == 0U);
    puts("PASS PGC probe: shared-memory flag test, warm restart, hex mode from ASCII, a FLAGRD answer, stale replies and errors discarded");

    /* Commands, as the IBM Technical Reference encodes them. */
    pgc_resetf(); pgc_displa(0); pgc_clears(7); pgc_color(200); pgc_prmfil(1);
    pgc_lutsav(); pgc_lutint(255); pgc_lut(5, 0xFF, 0x80, 0x1F);
    pgc_vwport(0, 639, 0, 479); pgc_projct(69); pgc_cliph(0); pgc_clipy(0);
    pgc_mdiden(); pgc_mdrotz(-1); pgc_vwiden(); pgc_vwrotx(359); pgc_tjust(1, 3);
    {
        static const unsigned char simple[] = {
            0x04, 0xD0, 0, 0x0F, 7, 0x06, 200, 0xE9, 1, 0xED, 0xEC, 255,
            0xEE, 5, 15, 8, 1, 0xB2, 0, 0, 0x7F, 2, 0, 0, 0xDF, 1, 0xB0, 69,
            0xAA, 0, 0xAB, 0, 0x90, 0x95, 0xFF, 0xFF, 0xA0, 0xA3, 0x67, 1,
            0x85, 1, 3
        };
        expect(simple, sizeof(simple));
    }
    /* Reals: integer word first, each word low byte first. */
    pgc_distan(3000.0);
    {
        static const unsigned char real[] = { 0xB1, 0xB8, 0x0B, 0, 0 };
        expect(real, sizeof(real));
    }
    pgc_window(-3.142, 3.142, -0.5, 1657.25);
    {
        static const unsigned char window[] = {
            0xB3, 0xFC, 0xFF, 0xA6, 0xDB, 0x03, 0x00, 0x5A, 0x24,
            0xFF, 0xFF, 0x00, 0x80, 0x79, 0x06, 0x00, 0x40
        };
        expect(window, sizeof(window));
    }
    pgc_mdorg(0.0, 0.0, 40000.0); pgc_vwrpt(0.0, -40000.0, sqrt(-1.0));
    settle();
    assert(logged_count == 26U && logged[0] == 0x91 && logged[13] == 0xA1);
    assert(logged[9] == 0xFF && logged[10] == 0x7F && logged[11] == 0xFF && logged[12] == 0xFF);
    assert(logged[18] == 0x00 && logged[19] == 0x80 && logged[20] == 0 && logged[21] == 0);
    assert(logged[22] == 0 && logged[23] == 0 && logged[24] == 0 && logged[25] == 0);
    logged_count = 0;
    quad[0].x = 1000.0f; quad[0].y = -0.5f; quad[0].z = 0.0f;
    quad[1].x = -999.25f; quad[1].y = 1.0f / 131072.0f; quad[1].z = 12.0f;
    quad[2].x = 0.0f; quad[2].y = 0.0f; quad[2].z = -1000.0f;
    for (i = 0; i < 3U; ++i) corners[i] = quad + i;
    pgc_poly3(3, corners);
    {
        static const unsigned char poly3[] = {
            0x32, 3,
            0xE8, 0x03, 0, 0, 0xFF, 0xFF, 0x00, 0x80, 0, 0, 0, 0,
            0x18, 0xFC, 0x00, 0xC0, 0, 0, 0, 0, 0x0C, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0x18, 0xFC, 0, 0
        };
        expect(poly3, sizeof(poly3));
    }
    screen[0].x = 0; screen[0].y = 479; screen[1].x = 639; screen[1].y = 0;
    screen[2].x = 320; screen[2].y = 240;
    pgc_poly(3, screen);
    {
        static const unsigned char poly[] = {
            0x30, 3, 0, 0, 0, 0, 0xDF, 1, 0, 0, 0x7F, 2, 0, 0, 0, 0, 0, 0,
            0x40, 1, 0, 0, 0xF0, 0, 0, 0
        };
        expect(poly, sizeof(poly));
    }
    pgc_move(8.0, 471.0); pgc_tsize(8.0); pgc_text("FPS \"7\"");
    {
        static const unsigned char text[] = {
            0x10, 8, 0, 0, 0, 0xD7, 1, 0, 0, 0x81, 8, 0, 0, 0,
            0x80, 0x22, 'F', 'P', 'S', ' ', '\'', '7', '\'', 0x22
        };
        expect(text, sizeof(text));
    }
    assert(pgc_flagrd(4, &value, 1) && value == 104);
    settle();
    logged_count = 0;
    puts("PASS PGC commands: opcodes and parameters as the IBM reference gives them, Reals integer word first with saturation, 4-bit LUT components and quoted text");

    /* Flow control: the board pauses; more than a FIFO's worth waits for
     * room instead of overrunning, across the pointer wrap. */
    paused_polls = 40;
    for (i = 0; i < 300U; ++i) pgc_color((unsigned char)i);
    settle();
    assert(!overrun && !pgc_failed() && logged_count == 600U);
    for (i = 0; i < 300U; ++i) assert(logged[2U * i] == 0x06 && logged[2U * i + 1U] == (unsigned char)i);
    logged_count = 0;
    /* A full error FIFO stops the board; waits drain it. */
    stall_errors = 1;
    areas[0][0x304] = (unsigned char)(areas[0][0x305] - 1);
    for (i = 0; i < 300U; ++i) pgc_color(9);
    settle();
    assert(!pgc_failed() && logged_count == 600U && !overrun);
    stall_errors = 0;
    logged_count = 0;
    (void)pgc_take_errors();
    error_codes[0] = 4; error_codes[1] = 1; error_codes[2] = 99; queued_errors = 3;
    assert(pgc_flagrd(4, &value, 1));
    assert(pgc_take_errors() == ((1U << 4) | (1U << 1) | 0x8000U));
    assert(pgc_take_errors() == 0U);
    logged_count = 0;
    puts("PASS PGC flow control: waits for FIFO room across the pointer wrap, drains a full error FIFO that stops the board, and reports error codes as bits");

    /* A board that stops: the deadline ends communication; later commands
     * are dropped and reads fail. */
    paused_polls = 0xFFFFFFFFU;
    for (i = 0; i < 300U; ++i) pgc_color(1);
    assert(pgc_failed() && !overrun);
    writes_to_area = 0;
    pgc_color(2);
    assert(!pgc_flagrd(4, &value, 1) && writes_to_area == 0UL);
    paused_polls = 0;
    pgc_close();
    puts("PASS PGC deadline: a board that stops taking commands ends communication without overrun or further writes");

    /* close: ASCII mode again, error reports restored. */
    reset(KIND_PGC, KIND_ABSENT, 0);
    areas[0][0x308] = 0;
    assert(open_board());
    logged_count = 0;
    pgc_close();
    {
        static const unsigned char ascii[] = { 0x43, 'A', ' ' };
        expect(ascii, sizeof(ascii));
    }
    assert(areas[0][0x308] == 0 && areas[0][0x301] == areas[0][0x300]);
    puts("PASS PGC close: queued commands run, ASCII mode is restored and error reporting returns to its saved state");

    /* PG-640A: board type 2 and FLAGRD 30 select LUTX and the Int angle. */
    reset(KIND_PGC, KIND_ABSENT, 1);
    areas[0][0x3F8] = 2;
    assert(open_board() && pgc_matrox() && pgc_board() == 2U);
    logged_count = 0;
    pgc_lut(5, 0xFF, 0x80, 0x1F); pgc_projct(69);
    {
        static const unsigned char matrox[] = { 0xE6, 5, 0xFF, 0x80, 0x1F, 0xB0, 69, 0 };
        expect(matrox, sizeof(matrox));
    }
    pgc_close();
    /* Any other Matrox board that answers flag 30, such as a PG-1280
     * (type 0), gets the same forms. */
    reset(KIND_PGC, KIND_ABSENT, 1);
    areas[0][0x3F8] = 0;
    assert(open_board() && pgc_matrox() && pgc_board() == 0U);
    pgc_close();
    /* A board that does not answer flag 30 keeps the IBM forms, whatever
     * its type byte reads. */
    reset(KIND_PGC, KIND_ABSENT, 0);
    areas[0][0x3F8] = 2;
    assert(open_board() && !pgc_matrox() && !pgc_failed());
    logged_count = 0;
    pgc_projct(60);
    {
        static const unsigned char ibm[] = { 0xB0, 60 };
        expect(ibm, sizeof(ibm));
    }
    pgc_close();
    /* The IBM PGC's 3 is not asked for flag 30 at all. */
    reset(KIND_PGC, KIND_ABSENT, 0);
    assert(open_board() && !pgc_matrox() && pgc_board() == 3U);
    {
        static const unsigned char probe[] = { 0x43, 'X', ' ', 0x51, 4 };
        expect(probe, sizeof(probe));
    }
    pgc_close();
    puts("PASS PGC identification: Matrox boards answering FLAGRD 30 get 8-bit LUTX and the two-byte PROJCT; the IBM PGC (type 3) and others keep LUT and the one-byte angle");

    /* FLAGRD 21 reads the viewport as four hex-mode Ints. */
    reset(KIND_PGC, KIND_ABSENT, 1);
    screen_x2 = 1279U; screen_y2 = 1023U;
    assert(open_board());
    {
        unsigned int x1 = 9U, x2 = 9U, y1 = 9U, y2 = 9U;
        assert(pgc_viewport(&x1, &x2, &y1, &y2));
        assert(x1 == 0U && x2 == 1279U && y1 == 0U && y2 == 1023U);
    }
    pgc_close();
    puts("PASS PGC viewport: FLAGRD 21 returns x1, x2, y1 and y2 as two-byte Ints");

    /* No board at C6000h: shared memory without a processor times out and
     * gets its flag back; nothing answers at C6400h either. */
    reset(KIND_RAM, KIND_ABSENT, 0);
    areas[0][0x308] = 0x5C;
    selected = 0;
    assert(!pgc_open());
    assert(areas[0][0x308] == 0x5C);
    reset(KIND_ABSENT, KIND_ABSENT, 0);
    assert(!pgc_open());
    /* A board strapped at C6400h is found after C6000h fails. */
    reset(KIND_ABSENT, KIND_PGC, 0);
    assert(pgc_open() && pgc_segment() == 0xC640U && hex_mode[1]);
    pgc_close();
    puts("PASS PGC absence: memory without a board processor and an empty address both fail the probe, restoring the flag they touched; a board at C6400h is found next");

    /* A fast host: the board's processor runs only between host accesses,
     * so every wait must leave the bus alone between polls. */
    reset(KIND_PGC, KIND_ABSENT, 0);
    hold_model = 1;
    assert(open_board() && !pgc_failed());
    logged_count = 0;
    for (i = 0; i < 300U; ++i) pgc_color((unsigned char)i);
    assert(pgc_flagrd(4, &value, 1) && value == 104 && !pgc_failed() && !overrun);
    assert(logged_count == 602U && pauses != 0UL);
    pgc_close();
    puts("PASS PGC fast host: waits pause between polls, so a board stopped by every host access still runs and answers");

    /* Restarts. A PG-640A with its self-test switch on loops in Self-test
     * B5 until the warm restart; a cold restart in progress is waited out,
     * but not forever; a board that ignores the warm restart flag gets its
     * value back, still has stale data discarded and is still found. */
    reset(KIND_PGC, KIND_ABSENT, 1);
    areas[0][0x3F8] = 2;
    self_test_loop = 1;
    assert(open_board() && pgc_matrox() && warm_restarts == 1U);
    pgc_close();
    reset(KIND_PGC, KIND_ABSENT, 0);
    areas[0][0x306] = 1;
    cold_polls = 50;
    assert(open_board() && areas[0][0x306] == 0 && warm_restarts == 1U);
    pgc_close();
    reset(KIND_PGC, KIND_PGC, 0);
    areas[0][0x306] = 1; areas[0][0x308] = 0x21;
    cold_polls = 0xFFFFFFFFU;
    assert(open_board() && pgc_segment() == 0xC640U && areas[0][0x308] == 0x21);
    pgc_close();
    reset(KIND_PGC, KIND_ABSENT, 0);
    warm_ignored = 1;
    areas[0][0x307] = 0x40; areas[0][0x302] = 3; areas[0][0x304] = 2;
    assert(open_board() && areas[0][0x307] == 0x40 && warm_restarts == 0U);
    assert(areas[0][0x303] == areas[0][0x302] && areas[0][0x305] == areas[0][0x304]);
    pgc_close();
    puts("PASS PGC restarts: a warm restart ends the PG-640A's endless self-test, a cold restart in progress is waited out within a limit, and a board that ignores the flag gets it back and is probed as before");
    return 0;
}
