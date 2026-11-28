/* Direct LibShell binding against a simulated PG Series 1 host window. */
#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "testglob.h"
#include "mtx_ls.h"

#define FIFO_WORDS 4096U
#define LOG_WORDS 2048U

/* GSP host interface state. */
static unsigned long gsp_address;
static unsigned short gsp_control = 0x0040U;
static unsigned short scratch1 = 0x1111U, scratch2 = 0x2222U;
static int board_absent, scratch_stuck, scratch_writes_while_running;
/* Data reads since the address was last set, per address kind. */
static unsigned int data_reads;
static unsigned int count_triples;
/* Write FIFO: words the host sent, and the count the board reports. */
static unsigned short sent[LOG_WORDS];
static unsigned int sent_count;
static unsigned int write_used;
/* Sequence of write counts returned while the FIFO drains; when empty,
 * write_used is returned. */
static const unsigned short *write_script;
static unsigned int write_script_length, write_script_next;
static unsigned long write_count_reads;
static int write_while_full;
/* Normally the board takes every queued word between count reads. */
static int board_drains = 1;
/* Read FIFO. */
static unsigned short replies[16];
static unsigned int reply_count, reply_next;
static unsigned int read_delay;
static int endless_replies;
static unsigned long reply_reads;
/* ERROR register and status. */
static unsigned char error_register;
static int error_pending;
static unsigned int error_reads, error_clears, status_reads;
static unsigned long accesses;

static unsigned short count_for(unsigned long address)
{
    if (address == 0x00180000UL)
    {
        unsigned short used;
        ++write_count_reads;
        if (write_script_next < write_script_length)
            return write_script[write_script_next++];
        used = (unsigned short)write_used;
        if (board_drains) write_used = 0U;
        return used;
    }
    if (read_delay != 0U) { --read_delay; return 0; }
    if (endless_replies) return 1;
    return (unsigned short)(reply_count - reply_next);
}

unsigned short mtx_ls_test_read(unsigned int offset, int bytes)
{
    ++accesses;
    if (board_absent) return bytes == 1 ? 0xFFU : 0xFFFFU;
    if (bytes == 1)
    {
        assert(offset == 0x3B0U);
        ++error_reads;
        return error_register;
    }
    switch (offset)
    {
        case 0x31CU:
            ++data_reads;
            if (gsp_address == 0xC0000130UL) return scratch1;
            if (gsp_address == 0xC0000140UL) return scratch2;
            if (gsp_address == 0x00180020UL)
            {
                ++status_reads;
                return (unsigned short)(error_pending ? 0x0200U | 0x0010U : 0x0010U);
            }
            assert(gsp_address == 0x00180000UL || gsp_address == 0x00180010UL);
            /* The binding reads each count three times and keeps the last. */
            if (data_reads % 3U != 0U) return 0xDEADU;
            ++count_triples;
            return count_for(gsp_address);
        case 0x31EU:
            return gsp_control;
        case 0x320U:
            assert(gsp_address == 0x00180010UL);
            ++reply_reads;
            if (endless_replies) return 0x7777U;
            assert(reply_next < reply_count);
            return replies[reply_next++];
        default:
            assert(!"Unexpected host window read");
            return 0;
    }
}

void mtx_ls_test_write(unsigned int offset, unsigned short value, int bytes)
{
    ++accesses;
    if (board_absent) return;
    if (bytes == 1)
    {
        assert(offset == 0x3A4U && value == 0xBFU);
        ++error_clears;
        error_pending = 0;
        return;
    }
    switch (offset)
    {
        case 0x318U:
            gsp_address = (gsp_address & 0xFFFF0000UL) | value;
            data_reads = 0;
            return;
        case 0x31AU:
            gsp_address = (gsp_address & 0xFFFFUL) | ((unsigned long)value << 16);
            data_reads = 0;
            return;
        case 0x31CU:
            if ((gsp_control & 0x8000U) == 0U) scratch_writes_while_running = 1;
            if (scratch_stuck) return;
            if (gsp_address == 0xC0000130UL) scratch1 = value;
            else if (gsp_address == 0xC0000140UL) scratch2 = value;
            else assert(!"Unexpected GSP write");
            return;
        case 0x31EU:
            gsp_control = value;
            return;
        case 0x320U:
            if (write_used >= FIFO_WORDS) write_while_full = 1;
            assert(sent_count < LOG_WORDS);
            sent[sent_count++] = value;
            ++write_used;
            return;
        default:
            assert(!"Unexpected host window write");
    }
}

static void reset_board(void)
{
    sent_count = 0; write_used = 0;
    write_script = NULL; write_script_length = write_script_next = 0;
    write_count_reads = 0; write_while_full = 0;
    reply_count = reply_next = 0; read_delay = 0; endless_replies = 0;
    reply_reads = 0;
    error_pending = 0; error_reads = error_clears = status_reads = 0;
    count_triples = 0;
}

static void expect(const unsigned short *words, unsigned int count)
{
    unsigned int i;
    assert(sent_count == count);
    for (i = 0; i < count; ++i) assert(sent[i] == words[i]);
    sent_count = 0;
}

/* The format's 16.16 value: truncated toward zero, fraction word first. */
static void expect_fixpt(unsigned int at, double value)
{
    long fixed = (long)(value * 65536.0);
    assert(sent[at] == (unsigned short)((unsigned long)fixed & 0xFFFFUL));
    assert(sent[at + 1U] == (unsigned short)(((unsigned long)fixed >> 16) & 0xFFFFUL));
}

static void check_presence(void)
{
    assert(unsetenv("PG1281") == 0);
    lsinit(0, 1);
    assert(mtx_ls_present() && mtx_ls_segment() == 0xC600U);
    /* The probe halts the GSP, restores both scratch registers and the
     * control word, and writes them only while halted. */
    assert(gsp_control == 0x0040U && scratch1 == 0x1111U && scratch2 == 0x2222U);
    assert(!scratch_writes_while_running);
    lsinit(0, 2); assert(mtx_ls_present() && mtx_ls_segment() == 0xC640U);
    lsinit(0, 3); assert(mtx_ls_present() && mtx_ls_segment() == 0xCE00U);
    lsinit(0, 4); assert(mtx_ls_present() && mtx_ls_segment() == 0xCE40U);
    lsinit(0, 5); assert(!mtx_ls_present());
    lsinit(3, 1); assert(!mtx_ls_present());
    assert(setenv("PG1281", " 0xD240, 0xC600", 1) == 0);
    lsinit(0, 1); assert(mtx_ls_present() && mtx_ls_segment() == 0xD240U);
    assert(setenv("PG1281", "0xC680", 1) == 0);
    lsinit(0, 1); assert(!mtx_ls_present());
    assert(setenv("PG1281", "C600", 1) == 0);
    lsinit(0, 1); assert(!mtx_ls_present());
    assert(unsetenv("PG1281") == 0);
    scratch_stuck = 1;
    lsinit(0, 1); assert(!mtx_ls_present() && gsp_control == 0x0040U);
    scratch_stuck = 0;
    board_absent = 1;
    lsinit(0, 1); assert(!mtx_ls_present());
    /* Without a board, every call is a no-op that touches nothing. */
    accesses = 0;
    lsinq(77); ioflush(); v3mdid();
    assert(iogeterror() == 0 && rdfifocnt() == 0 && iogetw() == 0);
    assert(accesses == 0);
    board_absent = 0;
    lsinit(0, 1); assert(mtx_ls_present());
    puts("PASS LibShell binding presence: C600h/C640h/CE00h/CE40h, PG1281 override and validation, halted scratch test with restoration, and no access without a board");
}

static void check_commands(void)
{
    static const unsigned short simple[] = {
        0x0013, 1, 0x012F, 0x0130, 88, 0x0034, 0, 0, 256, 0x0032, 3, 0,
        0x0125, 0, 0x7F, 0x0129, 0xFFFF, 0x0137, 0xFFFF, 0x0048, 0, 0,
        0x0090, 0, 639, 0, 479, 0x001B, 0, 0x001C, 0xFF, 0x0018, 0,
        0x0059, 0xFFFF, 0x0084, 0x0066, 0xFFFC, 0x006B, 0, 0, 0x0079, 0,
        0x00CF, 69, 0x00CD, 0, 0x00CE, 0, 0x00DE, 0x00E5, 359, 0x00F0,
        0x00F3, 103, 0x0023, 7, 0x0019, 200, 0x0124, 1
    };
    static const char text[] = "Key 1 \xe9";
    struct Lutval colors[2];
    struct Phcoord screen[3];
    struct V3coord world[2];
    unsigned int i;
    reset_board();
    lserror(1); lsinqsys(); lsinq(88); lslutrd(0, 0, 256); lslutsl(3, 0);
    lslutpm(0, 0x7F); lssbm(-1); lssclpl(-1); scsorg(0, 0);
    v2gpsvwp(0, 639, 0, 479); krhwsbop(0); krhwsmsk(0xFF); krhwstdef(0);
    lssafapat(-1); krgcdi(); lssfont(-4); sctxsalign(0, 0); sctxsdmode(0);
    v3gpsproj(69); v3gpshclip(0); v3gpsyclip(0); v3mdid(); v3mdrotz(359);
    v3vwid(); v3vwrotx(103); krhwcclr(7); krhwsfcol(200); lsemulen(1);
    expect(simple, sizeof(simple) / sizeof(simple[0]));

    colors[0].red = 1; colors[0].green = 2; colors[0].blue = 3;
    colors[1].red = 255; colors[1].green = 128; colors[1].blue = 0;
    lslutmd(1, 16, 2, colors);
    {
        static const unsigned short lut[] = { 0x0033, 1, 16, 2, 1, 2, 3, 255, 128, 0 };
        expect(lut, 10);
    }
    screen[0].x = 10; screen[0].y = 20; screen[1].x = -1; screen[1].y = 479;
    screen[2].x = 639; screen[2].y = 0;
    scarwplygn(3, screen);
    {
        static const unsigned short fill[] = { 0x0052, 3, 10, 20, 0xFFFF, 479, 639, 0 };
        expect(fill, 8);
    }
    sctxbstring(8, 460, (char *)text);
    assert(sent_count == 4U + strlen(text));
    assert(sent[0] == 0x0072 && sent[1] == 8 && sent[2] == 460 && sent[3] == strlen(text));
    for (i = 0; i < strlen(text); ++i) assert(sent[4U + i] == (unsigned char)text[i]);
    sent_count = 0;

    /* FIXPT parameters: two words each, fraction first. */
    v2gpswdw(-1657.7094, 1657.7094, -1242.6407, 1242.6407);
    assert(sent_count == 9U && sent[0] == 0x008F);
    expect_fixpt(1, -1657.7094); expect_fixpt(3, 1657.7094);
    expect_fixpt(5, -1242.6407); expect_fixpt(7, 1242.6407);
    sent_count = 0;
    v3gpsdist(3000.0);
    assert(sent_count == 3U && sent[0] == 0x00D0 && sent[1] == 0 && sent[2] == 3000);
    sent_count = 0;
    sctxssf(1.0, 0.5);
    assert(sent_count == 5U && sent[0] == 0x0068 && sent[1] == 0 && sent[2] == 1 &&
           sent[3] == 0x8000 && sent[4] == 0);
    sent_count = 0;
    v3mdorg(-0.75, 0.0, 12.5);
    assert(sent_count == 7U && sent[0] == 0x00E0);
    expect_fixpt(1, -0.75); expect_fixpt(3, 0.0); expect_fixpt(5, 12.5);
    sent_count = 0;
    v3vwrpt(0.0, -1.0 / 131072.0, 32767.5);
    assert(sent_count == 7U && sent[0] == 0x00F2);
    expect_fixpt(1, 0.0); expect_fixpt(3, 0.0); expect_fixpt(5, 32767.5);
    sent_count = 0;
    /* Saturation and NaN instead of undefined conversions. */
    v3gpsdist(40000.0);
    assert(sent[1] == 0xFFFF && sent[2] == 0x7FFF);
    sent_count = 0;
    v3gpsdist(-40000.0);
    assert(sent[1] == 0x0000 && sent[2] == 0x8000);
    sent_count = 0;
    v3gpsdist(sqrt(-1.0));
    assert(sent[1] == 0 && sent[2] == 0);
    sent_count = 0;

    /* Vertex coordinates use the integer IEEE decoding. */
    world[0].x = 1000.0f; world[0].y = -999.123f; world[0].z = 0.0f;
    world[1].x = -1.0f / 65536.0f; world[1].y = 1.0f / 131072.0f; world[1].z = 0.33f;
    v3arwplygn(2, world);
    assert(sent_count == 14U && sent[0] == 0x0115 && sent[1] == 2);
    expect_fixpt(2, 1000.0); expect_fixpt(4, (double)-999.123f); expect_fixpt(6, 0.0);
    expect_fixpt(8, -1.0 / 65536.0); expect_fixpt(10, 0.0); expect_fixpt(12, (double)0.33f);
    sent_count = 0;
    puts("PASS LibShell binding commands: 37 calls send the Command Reference's opcode and parameter words; FIXPT is 16.16 truncated toward zero, fraction first, with saturation");
}

static void check_float_decoding(void)
{
    struct V3coord world;
    unsigned long i, seed = 12345UL;
    float value;
    for (i = 0; i < 200000UL; ++i)
    {
        seed = seed * 1103515245UL + 12345UL;
        value = (float)((double)(seed % 2000001UL) / 1000.0 - 1000.0);
        if (i % 7UL == 0UL) value = (float)ldexp((double)value, -(int)(seed % 30UL));
        world.x = value; world.y = -value; world.z = value * 0.5f;
        sent_count = 0;
        v3arwplygn(1, &world);
        expect_fixpt(2, (double)world.x);
        expect_fixpt(4, (double)world.y);
        expect_fixpt(6, (double)world.z);
    }
    world.x = 32768.0f; world.y = -32768.0f; world.z = 32767.99f;
    sent_count = 0;
    v3arwplygn(1, &world);
    assert(sent[2] == 0xFFFF && sent[3] == 0x7FFF);
    assert(sent[4] == 0x0000 && sent[5] == 0x8000);
    expect_fixpt(6, (double)32767.99f);
    sent_count = 0;
    puts("PASS LibShell binding vertex conversion: 200,000 floats decode without floating point to the same 16.16 words as truncating conversion");
}

static void check_flow_control(void)
{
    static const unsigned short draining[] = { 4096, 4096, 4095 };
    unsigned int i;
    reset_board();
    lsinit(0, 1);
    board_drains = 0;
    /* The first word reads the count; later words use the known room. */
    write_used = 4090U;
    for (i = 0; i < 6U; ++i) krgcdi();
    assert(write_count_reads == 1UL && sent_count == 6U && !write_while_full);
    /* The FIFO is now full: the next word waits until the board takes one. */
    write_script = draining; write_script_length = 3; write_script_next = 0;
    write_used = 4095U;
    krgcdi();
    assert(write_count_reads == 4UL && sent_count == 7U && !write_while_full);
    /* Each count read sets the address and keeps the third read. */
    assert(count_triples == 4U);
    board_drains = 1;
    puts("PASS LibShell binding flow control: counts read through the host interface (third read kept), known room reused, and no word written into a full FIFO");
}

static void check_reads_and_errors(void)
{
    static const unsigned short stalled[] = { 9, 9, 9 };
    reset_board();
    lsinit(0, 1);
    replies[0] = 0x1234; replies[1] = 0xFEDC; reply_count = 2;
    assert(rdfifocnt() == 2);
    read_delay = 2;
    assert((unsigned short)iogetw() == 0x1234);
    assert((unsigned short)iogetw() == 0xFEDC);
    assert(rdfifocnt() == 0);
    /* Errors: no register access without the status bit; with it, the
     * byte is returned and cleared, without waiting for the write FIFO. */
    write_used = 100U;
    assert(iogeterror() == 0 && error_reads == 0U && status_reads == 1U);
    error_pending = 1; error_register = 0x0A;
    assert(iogeterror() == 0x0A && error_reads == 1U && error_clears == 1U);
    assert(!error_pending && write_count_reads == 0UL);
    /* ioflush: wait for the command FIFO to drain, then discard replies. */
    write_script = stalled; write_script_length = 0;
    write_used = 0U;
    replies[0] = 1; replies[1] = 2; replies[2] = 3;
    reply_count = 3; reply_next = 0;
    ioflush();
    assert(reply_next == 3U && rdfifocnt() == 0);
    /* A stalled shell never drains its FIFO: the wait ends without progress,
     * and an endless reply stream is bounded at two FIFOs. */
    write_used = 9U; write_count_reads = 0UL; reply_reads = 0UL;
    board_drains = 0; endless_replies = 1;
    ioflush();
    assert(write_count_reads == 65536UL && reply_reads == 2UL * FIFO_WORDS);
    board_drains = 1; endless_replies = 0;
    (void)stalled;
    puts("PASS LibShell binding reads: word counts, waiting single-word reads, non-blocking error reads that clear the ERROR register, and a bounded flush of a stalled shell");
}

int main(void)
{
    check_presence();
    check_commands();
    check_float_decoding();
    check_flow_control();
    check_reads_and_errors();
    return 0;
}
