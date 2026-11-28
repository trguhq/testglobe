/*
 * testglobe - LibShell calls for Matrox PG Series 1 boards.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The subset of the LIB Shell C binding that the Matrox driver uses, with
 * the binding's names, argument types and structures. mtx_ls.c implements
 * them directly on the board's host interface, so no vendor library or
 * header is needed. Defining MTX_LSCL builds the driver against the
 * vendor's headers and LSCL.LIB instead.
 */
#ifndef mtx_ls_h
#define mtx_ls_h

/* FIXPT values are floats in the C binding and 16.16 words on the board. */
struct V3coord
{
    float x, y, z;
};

struct Phcoord
{
    short x, y;
};

struct Lutval
{
    unsigned short red, green, blue;
};

/* Communication. lsinit(0, 1..4) selects segment C600h, C640h, CE00h or
 * CE40h, or the first segment in the PG1281 environment variable, and
 * checks that a board answers there. */
extern void lsinit(short fhandle, short addr);
extern void ioflush(void);
extern short iogeterror(void);
extern short iogetw(void);
extern short rdfifocnt(void);

/* Shell state. */
extern void lsemulen(short restore_flag);
extern void lserror(short flag);
extern void lsinq(unsigned short inq_code);
extern void lsinqsys(void);
extern void lslutmd(unsigned short log_lut, unsigned short first_entry,
                    unsigned short nentries, struct Lutval *colors);
extern void lslutpm(unsigned short phy_lut, unsigned short bitmask);
extern void lslutrd(unsigned short log_lut, unsigned short first_entry,
                    unsigned short nentries);
extern void lslutsl(unsigned short log_lut, unsigned short phy_lut);
extern void lssafapat(short apat_id);
extern void lssbm(short bm_id);
extern void lssclpl(short id);
extern void lssfont(short font_id);

/* Drawing attributes and screen-coordinate drawing. */
extern void krgcdi(void);
extern void krhwcclr(unsigned short color_index);
extern void krhwsbop(short bool_operator);
extern void krhwsfcol(unsigned short color_index);
extern void krhwsmsk(unsigned short mask);
extern void krhwstdef(unsigned short mode);
extern void scarwplygn(unsigned short nvert, struct Phcoord *vertptr);
extern void scsorg(unsigned short x, unsigned short y);
extern void sctxbstring(short x, short y, char *strptr);
extern void sctxsalign(short halign, short valign);
extern void sctxsdmode(short mode);
extern void sctxssf(double xscale, double yscale);

/* 2D and 3D viewing. */
extern void v2gpsvwp(short xmin, short xmax, short ymin, short ymax);
extern void v2gpswdw(double xmin, double xmax, double ymin, double ymax);
extern void v3arwplygn(unsigned short nvert, struct V3coord *vertptr);
extern void v3gpsdist(double dist);
extern void v3gpshclip(short flag);
extern void v3gpsproj(short theta);
extern void v3gpsyclip(short flag);
extern void v3mdid(void);
extern void v3mdorg(double x, double y, double z);
extern void v3mdrotz(short theta);
extern void v3vwid(void);
extern void v3vwrotx(short theta);
extern void v3vwrpt(double x, double y, double z);

/* Not in the vendor binding, whose lsinit exits when no board answers:
 * whether the last lsinit found a board, and the segment it checked. */
extern int mtx_ls_present(void);
extern unsigned int mtx_ls_segment(void);

#endif /* mtx_ls_h */
