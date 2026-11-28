/*
 * testglobe - LibShell host binding for Matrox PG Series 1 boards.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The LIB Shell runs on the board's TMS34010 graphics processor (GSP). The
 * host reaches it through a 1 KB window in the PC's address space, at
 * segment C600h by default. The window starts with the IBM PGC
 * communication area; LibShell's own registers follow:
 *
 *   0x318/0x31A  GSP host interface address, low/high word
 *   0x31C        data at that GSP address
 *   0x31E        host interface control (bit 15 halts the GSP)
 *   0x320        I/O FIFO data: writes feed the command (write) FIFO,
 *                reads take from the reply (read) FIFO
 *   0x3A4        status clear (byte)
 *   0x3B0        ERROR register (byte)
 *
 * Word counts of the 4096-word write and read FIFOs, and the status word
 * whose bit 9 flags an unread error, are GSP addresses read through the
 * host interface. Each count is read three times after its address is set
 * and the last value is used. Commands are an opcode word and parameter
 * words, as the LIB Shell Command Reference gives them; a FIXPT parameter
 * is a 16.16 value sent as its fraction word, then its integer word.
 *
 * Unlike the vendor binding, iogeterror() does not first wait for the
 * write FIFO to drain, which deadlocks once a second error stalls the
 * shell, and ioflush() stops waiting when the FIFO makes no progress.
 */
#include "testglob.h"
#if defined(DRV_MTX) && !defined(MTX_LSCL)
#include <stdlib.h>
#include <string.h>
#include "mtx_ls.h"

#define MTX_LS_HSTADRL 0x318U
#define MTX_LS_HSTADRH 0x31AU
#define MTX_LS_HSTDATA 0x31CU
#define MTX_LS_HSTCTRL 0x31EU
#define MTX_LS_IODATA 0x320U
#define MTX_LS_CLEAR 0x3A4U
#define MTX_LS_ERROR 0x3B0U

#define MTX_LS_WRITE_COUNT 0x00180000UL
#define MTX_LS_READ_COUNT 0x00180010UL
#define MTX_LS_STATUS 0x00180020UL
#define MTX_LS_STATUS_ERROR 0x0200U
#define MTX_LS_CLEAR_ERROR 0xBFU
#define MTX_LS_FIFO_WORDS 4096U
/* Presence test: two scratch registers of the halted TMS34010 (CONVSP and
 * CONVDP) must keep the patterns written to them. */
#define MTX_LS_HALT 0x8000U
#define MTX_LS_SCRATCH1 0xC0000130UL
#define MTX_LS_SCRATCH2 0xC0000140UL
/* ioflush(): consecutive write-FIFO polls without progress, a fraction of
 * a second on an ISA bus, before giving up on a stalled shell. */
#define MTX_LS_FLUSH_POLLS 65535UL

#ifdef MTX_LS_TEST
/* Host tests supply the window as a simulated device. */
extern unsigned short mtx_ls_test_read(unsigned int offset, int bytes);
extern void mtx_ls_test_write(unsigned int offset, unsigned short value, int bytes);
#define MTX_LS_READ(offset) mtx_ls_test_read((offset), 2)
#define MTX_LS_WRITE(offset, value) \
    mtx_ls_test_write((offset), (unsigned short)(value), 2)
#define MTX_LS_READ8(offset) ((unsigned char)mtx_ls_test_read((offset), 1))
#define MTX_LS_WRITE8(offset, value) \
    mtx_ls_test_write((offset), (unsigned short)(value), 1)
#else
static volatile unsigned char _far *mtx_ls_window;
#define MTX_LS_WORD(offset) \
    (*(volatile unsigned short _far *)(mtx_ls_window + (offset)))
#define MTX_LS_READ(offset) MTX_LS_WORD(offset)
#define MTX_LS_WRITE(offset, value) \
    (MTX_LS_WORD(offset) = (unsigned short)(value))
#define MTX_LS_READ8(offset) (mtx_ls_window[offset])
#define MTX_LS_WRITE8(offset, value) \
    (mtx_ls_window[offset] = (unsigned char)(value))
#endif

/* Segments where the boards can be strapped, each also at +40h. */
static const unsigned int mtx_ls_segments[] = {
    0xC600U, 0xCA00U, 0xCE00U, 0xD200U, 0xD400U,
    0xD600U, 0xDE00U, 0xE200U, 0xE400U, 0xF600U
};

static unsigned int mtx_ls_seg = 0xC600U;
static int mtx_ls_found;
/* Write-FIFO words known to be free; re-read when it runs out. */
static unsigned int mtx_ls_room;

int mtx_ls_present(void) { return mtx_ls_found; }
unsigned int mtx_ls_segment(void) { return mtx_ls_seg; }

static void mtx_ls_address(unsigned long address)
{
    MTX_LS_WRITE(MTX_LS_HSTADRL, address & 0xFFFFUL);
    MTX_LS_WRITE(MTX_LS_HSTADRH, address >> 16);
}

static unsigned short mtx_ls_count(unsigned long address)
{
    mtx_ls_address(address);
    (void)MTX_LS_READ(MTX_LS_HSTDATA);
    (void)MTX_LS_READ(MTX_LS_HSTDATA);
    return MTX_LS_READ(MTX_LS_HSTDATA);
}

static void mtx_ls_word(unsigned short word)
{
    unsigned short used;
    if (!mtx_ls_found) return;
    while (mtx_ls_room == 0U)
    {
        used = mtx_ls_count(MTX_LS_WRITE_COUNT);
        mtx_ls_room = used < MTX_LS_FIFO_WORDS ? MTX_LS_FIFO_WORDS - used : 0U;
    }
    MTX_LS_WRITE(MTX_LS_IODATA, word);
    --mtx_ls_room;
}

static void mtx_ls_words(const unsigned short *words, unsigned int count)
{
    unsigned int i;
    for (i = 0; i < count; ++i) mtx_ls_word(words[i]);
}

static void mtx_ls_command(unsigned short opcode, unsigned short parameter)
{
    mtx_ls_word(opcode);
    mtx_ls_word(parameter);
}

/* 16.16 truncated toward zero, as the vendor's float conversion; values
 * outside the format saturate. */
static void mtx_ls_fixpt(double value)
{
    long fixed;
    if (!(value == value)) fixed = 0L;
    else if (value >= 32767.0 + 65535.0 / 65536.0) fixed = 0x7FFFFFFFL;
    else if (value <= -32768.0) fixed = -0x7FFFFFFFL - 1L;
    else fixed = (long)(value * 65536.0);
    mtx_ls_word((unsigned short)((unsigned long)fixed & 0xFFFFUL));
    mtx_ls_word((unsigned short)(((unsigned long)fixed >> 16) & 0xFFFFUL));
}

/* The same conversion for vertex coordinates, decoded from the IEEE single
 * bits so that a machine without an x87 does no floating-point work. */
static void mtx_ls_float_fixpt(const float *value)
{
    const unsigned char *bytes = (const unsigned char *)value;
    const unsigned short endian = 1;
    unsigned long bits, magnitude;
    int exponent, shift;
    long fixed;
    if (*(const unsigned char *)&endian)
        bits = (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) |
               ((unsigned long)bytes[2] << 16) | ((unsigned long)bytes[3] << 24);
    else
        bits = (unsigned long)bytes[3] | ((unsigned long)bytes[2] << 8) |
               ((unsigned long)bytes[1] << 16) | ((unsigned long)bytes[0] << 24);
    exponent = (int)((bits >> 23) & 255UL);
    if (exponent < 111) fixed = 0L;                       /* below 2^-16 */
    else if (exponent > 141)                              /* 32768 or more */
        fixed = (bits & 0x80000000UL) ? -0x7FFFFFFFL - 1L : 0x7FFFFFFFL;
    else
    {
        /* The value is magnitude * 2^(exponent - 150); 16.16 scales it by
         * 2^16, and shift <= 7 keeps the result below 2^31. */
        magnitude = (bits & 0x7FFFFFUL) | 0x800000UL;
        shift = exponent - 134;
        if (shift < 0) magnitude >>= -shift;
        else magnitude <<= shift;
        fixed = (bits & 0x80000000UL) ? -(long)magnitude : (long)magnitude;
    }
    mtx_ls_word((unsigned short)((unsigned long)fixed & 0xFFFFUL));
    mtx_ls_word((unsigned short)(((unsigned long)fixed >> 16) & 0xFFFFUL));
}

static int mtx_ls_valid_segment(unsigned long segment)
{
    unsigned int i;
    for (i = 0; i < sizeof(mtx_ls_segments) / sizeof(mtx_ls_segments[0]); ++i)
        if (segment == mtx_ls_segments[i] || segment == mtx_ls_segments[i] + 0x40U)
            return TRUE;
    return FALSE;
}

/* Halt the GSP, check that two of its scratch registers keep test
 * patterns, then restore them and the GSP's control word. */
static int mtx_ls_probe(void)
{
    unsigned short control, first, second, read1, read2;
    control = MTX_LS_READ(MTX_LS_HSTCTRL);
    MTX_LS_WRITE(MTX_LS_HSTCTRL, control | MTX_LS_HALT);
    mtx_ls_address(MTX_LS_SCRATCH1);
    first = MTX_LS_READ(MTX_LS_HSTDATA);
    MTX_LS_WRITE(MTX_LS_HSTDATA, 0x5A5AU);
    mtx_ls_address(MTX_LS_SCRATCH2);
    second = MTX_LS_READ(MTX_LS_HSTDATA);
    MTX_LS_WRITE(MTX_LS_HSTDATA, 0xA5A5U);
    mtx_ls_address(MTX_LS_SCRATCH1);
    read1 = MTX_LS_READ(MTX_LS_HSTDATA);
    mtx_ls_address(MTX_LS_SCRATCH2);
    read2 = MTX_LS_READ(MTX_LS_HSTDATA);
    mtx_ls_address(MTX_LS_SCRATCH1);
    MTX_LS_WRITE(MTX_LS_HSTDATA, first);
    mtx_ls_address(MTX_LS_SCRATCH2);
    MTX_LS_WRITE(MTX_LS_HSTDATA, second);
    MTX_LS_WRITE(MTX_LS_HSTCTRL, control);
    return read1 == 0x5A5AU && read2 == 0xA5A5U;
}

void lsinit(short fhandle, short addr)
{
    const char *setting;
    char *end;
    unsigned long segment;
    mtx_ls_found = FALSE;
    mtx_ls_room = 0U;
    /* The vendor binding can also write commands to a file; not here. */
    if (fhandle != 0) return;
    switch (addr)
    {
        case 1: mtx_ls_seg = 0xC600U; break;
        case 2: mtx_ls_seg = 0xC640U; break;
        case 3: mtx_ls_seg = 0xCE00U; break;
        case 4: mtx_ls_seg = 0xCE40U; break;
        default: return;
    }
    /* As in the vendor binding, PG1281 (for example "0xC600" or several
     * segments separated by spaces, commas, semicolons or plus signs)
     * overrides addr. The first board listed is the one used. */
    setting = getenv("PG1281");
    if (setting != NULL)
    {
        while (*setting == ' ' || *setting == ',' || *setting == ';' ||
               *setting == '+')
            ++setting;
        segment = strtoul(setting, &end, 0);
        if (end == setting || !mtx_ls_valid_segment(segment)) return;
        mtx_ls_seg = (unsigned int)segment;
    }
#ifndef MTX_LS_TEST
    mtx_ls_window = (volatile unsigned char _far *)((unsigned long)mtx_ls_seg << 16);
#endif
    mtx_ls_found = mtx_ls_probe();
}

void ioflush(void)
{
    unsigned short used, previous;
    unsigned long idle;
    if (!mtx_ls_found) return;
    previous = mtx_ls_count(MTX_LS_WRITE_COUNT);
    idle = 0UL;
    while (previous != 0U && idle < MTX_LS_FLUSH_POLLS)
    {
        used = mtx_ls_count(MTX_LS_WRITE_COUNT);
        if (used < previous) idle = 0UL;
        else ++idle;
        previous = used;
    }
    mtx_ls_room = 0U;
    /* Discard unread replies; a full FIFO is 4096 words. */
    for (idle = 0UL; idle < 2UL * MTX_LS_FIFO_WORDS &&
                     mtx_ls_count(MTX_LS_READ_COUNT) != 0U; ++idle)
        (void)MTX_LS_READ(MTX_LS_IODATA);
}

short iogeterror(void)
{
    unsigned short status;
    unsigned char error_bits;
    if (!mtx_ls_found) return 0;
    mtx_ls_address(MTX_LS_STATUS);
    status = MTX_LS_READ(MTX_LS_HSTDATA);
    if ((status & MTX_LS_STATUS_ERROR) == 0U) return 0;
    error_bits = MTX_LS_READ8(MTX_LS_ERROR);
    MTX_LS_WRITE8(MTX_LS_CLEAR, MTX_LS_CLEAR_ERROR);
    return (short)error_bits;
}

short rdfifocnt(void)
{
    if (!mtx_ls_found) return 0;
    return (short)mtx_ls_count(MTX_LS_READ_COUNT);
}

/* As the vendor's: waits for a word, so call it after rdfifocnt() > 0. */
short iogetw(void)
{
    if (!mtx_ls_found) return 0;
    while (mtx_ls_count(MTX_LS_READ_COUNT) == 0U)
        ;
    return (short)MTX_LS_READ(MTX_LS_IODATA);
}

void lsemulen(short restore_flag) { mtx_ls_command(0x0124U, (unsigned short)restore_flag); }
void lserror(short flag) { mtx_ls_command(0x0013U, (unsigned short)flag); }
void lsinq(unsigned short inq_code) { mtx_ls_command(0x0130U, inq_code); }
void lsinqsys(void) { mtx_ls_word(0x012FU); }

void lslutmd(unsigned short log_lut, unsigned short first_entry,
             unsigned short nentries, struct Lutval *colors)
{
    unsigned short header[4];
    unsigned int i;
    header[0] = 0x0033U; header[1] = log_lut;
    header[2] = first_entry; header[3] = nentries;
    mtx_ls_words(header, 4U);
    for (i = 0; i < nentries; ++i)
    {
        mtx_ls_word(colors[i].red);
        mtx_ls_word(colors[i].green);
        mtx_ls_word(colors[i].blue);
    }
}

void lslutpm(unsigned short phy_lut, unsigned short bitmask)
{
    mtx_ls_command(0x0125U, phy_lut);
    mtx_ls_word(bitmask);
}

void lslutrd(unsigned short log_lut, unsigned short first_entry,
             unsigned short nentries)
{
    unsigned short words[4];
    words[0] = 0x0034U; words[1] = log_lut;
    words[2] = first_entry; words[3] = nentries;
    mtx_ls_words(words, 4U);
}

void lslutsl(unsigned short log_lut, unsigned short phy_lut)
{
    mtx_ls_command(0x0032U, log_lut);
    mtx_ls_word(phy_lut);
}

void lssafapat(short apat_id) { mtx_ls_command(0x0059U, (unsigned short)apat_id); }
void lssbm(short bm_id) { mtx_ls_command(0x0129U, (unsigned short)bm_id); }
void lssclpl(short id) { mtx_ls_command(0x0137U, (unsigned short)id); }
void lssfont(short font_id) { mtx_ls_command(0x0066U, (unsigned short)font_id); }

void krgcdi(void) { mtx_ls_word(0x0084U); }
void krhwcclr(unsigned short color_index) { mtx_ls_command(0x0023U, color_index); }
void krhwsbop(short bool_operator) { mtx_ls_command(0x001BU, (unsigned short)bool_operator); }
void krhwsfcol(unsigned short color_index) { mtx_ls_command(0x0019U, color_index); }
void krhwsmsk(unsigned short mask) { mtx_ls_command(0x001CU, mask); }
void krhwstdef(unsigned short mode) { mtx_ls_command(0x0018U, mode); }

void scarwplygn(unsigned short nvert, struct Phcoord *vertptr)
{
    unsigned int i;
    mtx_ls_command(0x0052U, nvert);
    for (i = 0; i < nvert; ++i)
    {
        mtx_ls_word((unsigned short)vertptr[i].x);
        mtx_ls_word((unsigned short)vertptr[i].y);
    }
}

void scsorg(unsigned short x, unsigned short y)
{
    mtx_ls_command(0x0048U, x);
    mtx_ls_word(y);
}

/* One word per character, as the vendor sends them. */
void sctxbstring(short x, short y, char *strptr)
{
    unsigned short length = (unsigned short)strlen(strptr);
    unsigned int i;
    mtx_ls_command(0x0072U, (unsigned short)x);
    mtx_ls_word((unsigned short)y);
    mtx_ls_word(length);
    for (i = 0; i < length; ++i)
        mtx_ls_word((unsigned short)(unsigned char)strptr[i]);
}

void sctxsalign(short halign, short valign)
{
    mtx_ls_command(0x006BU, (unsigned short)halign);
    mtx_ls_word((unsigned short)valign);
}

void sctxsdmode(short mode) { mtx_ls_command(0x0079U, (unsigned short)mode); }

void sctxssf(double xscale, double yscale)
{
    mtx_ls_word(0x0068U);
    mtx_ls_fixpt(xscale);
    mtx_ls_fixpt(yscale);
}

void v2gpsvwp(short xmin, short xmax, short ymin, short ymax)
{
    unsigned short words[5];
    words[0] = 0x0090U;
    words[1] = (unsigned short)xmin; words[2] = (unsigned short)xmax;
    words[3] = (unsigned short)ymin; words[4] = (unsigned short)ymax;
    mtx_ls_words(words, 5U);
}

void v2gpswdw(double xmin, double xmax, double ymin, double ymax)
{
    mtx_ls_word(0x008FU);
    mtx_ls_fixpt(xmin);
    mtx_ls_fixpt(xmax);
    mtx_ls_fixpt(ymin);
    mtx_ls_fixpt(ymax);
}

void v3arwplygn(unsigned short nvert, struct V3coord *vertptr)
{
    unsigned int i;
    mtx_ls_command(0x0115U, nvert);
    for (i = 0; i < nvert; ++i)
    {
        mtx_ls_float_fixpt(&vertptr[i].x);
        mtx_ls_float_fixpt(&vertptr[i].y);
        mtx_ls_float_fixpt(&vertptr[i].z);
    }
}

void v3gpsdist(double dist)
{
    mtx_ls_word(0x00D0U);
    mtx_ls_fixpt(dist);
}

void v3gpshclip(short flag) { mtx_ls_command(0x00CDU, (unsigned short)flag); }
void v3gpsproj(short theta) { mtx_ls_command(0x00CFU, (unsigned short)theta); }
void v3gpsyclip(short flag) { mtx_ls_command(0x00CEU, (unsigned short)flag); }
void v3mdid(void) { mtx_ls_word(0x00DEU); }

void v3mdorg(double x, double y, double z)
{
    mtx_ls_word(0x00E0U);
    mtx_ls_fixpt(x);
    mtx_ls_fixpt(y);
    mtx_ls_fixpt(z);
}

void v3mdrotz(short theta) { mtx_ls_command(0x00E5U, (unsigned short)theta); }
void v3vwid(void) { mtx_ls_word(0x00F0U); }
void v3vwrotx(short theta) { mtx_ls_command(0x00F3U, (unsigned short)theta); }

void v3vwrpt(double x, double y, double z)
{
    mtx_ls_word(0x00F2U);
    mtx_ls_fixpt(x);
    mtx_ls_fixpt(y);
    mtx_ls_fixpt(z);
}
#endif
