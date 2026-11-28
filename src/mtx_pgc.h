/*
 * testglobe - IBM Professional Graphics Controller commands.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Hexadecimal-mode commands for the IBM PGC and compatible boards, such as
 * the Matrox PG-640A, through the 1 KB communication area at C6000h or
 * C6400h. Each command is written into the 256-byte command FIFO as the
 * IBM PGC Technical Reference gives it; where the PG-640A manual gives a
 * different form, a PG-640A receives that form instead.
 */
#ifndef mtx_pgc_h
#define mtx_pgc_h

#include "types.h"
#include "soft3d.h"

/* Find a board that answers at C6000h, then C6400h: switch it to hex
 * mode, enable error reports and discard stale replies and errors. */
extern int pgc_open(void);
extern unsigned int pgc_segment(void);
/* A Matrox PG-640A (board-type byte 2 and its own FLAGRD 30): 8-bit LUT
 * entries and a two-byte PROJCT angle. */
extern int pgc_pg640a(void);
/* The board stopped taking commands or answering within the deadline;
 * later commands are dropped. */
extern int pgc_failed(void);
/* Error codes reported since the last call: bit n for code n (1 range,
 * 3 memory, 4 overflow, 6 opcode, 10 area fill; larger codes set bit 15). */
extern unsigned int pgc_take_errors(void);
/* Send FLAGRD flag and read its reply bytes; FALSE on timeout. */
extern int pgc_flagrd(unsigned char flag, unsigned char *reply,
                      unsigned int bytes);
/* Let queued commands run, restore error reporting and ASCII mode. */
extern void pgc_close(void);

extern void pgc_resetf(void);
extern void pgc_displa(unsigned char flag);
extern void pgc_clears(unsigned char color);
extern void pgc_color(unsigned char color);
extern void pgc_prmfil(unsigned char flag);
/* 8-bit components; an IBM PGC keeps their high four bits. */
extern void pgc_lut(unsigned char index, unsigned char red,
                    unsigned char green, unsigned char blue);
extern void pgc_lutsav(void);
extern void pgc_lutint(unsigned char state);

extern void pgc_vwport(int x1, int x2, int y1, int y2);
extern void pgc_window(double x1, double x2, double y1, double y2);
extern void pgc_projct(int angle);
extern void pgc_distan(double distance);
extern void pgc_cliph(unsigned char flag);
extern void pgc_clipy(unsigned char flag);
extern void pgc_mdiden(void);
extern void pgc_mdorg(double x, double y, double z);
extern void pgc_mdrotz(int degrees);
extern void pgc_vwiden(void);
extern void pgc_vwrpt(double x, double y, double z);
extern void pgc_vwrotx(int degrees);

/* 3D polygon from mesh vertices; 2D polygon in window coordinates. */
extern void pgc_poly3(unsigned int count,
                      const Vertex TG_HUGE *const *vertices);
extern void pgc_poly(unsigned int count, const Soft3d_screen_point *points);
extern void pgc_move(double x, double y);
extern void pgc_tsize(double size);
extern void pgc_tjust(unsigned char horizontal, unsigned char vertical);
/* Double quotes in the text are sent as single quotes. */
extern void pgc_text(const char *text);

#endif /* mtx_pgc_h */
