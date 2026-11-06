/* Host-only TIGA 2.0 call recorder. Native headers turn many of these
 * operations into command packets; this shim does not emulate that ABI. */
#ifndef TEST_TIGA_H
#define TEST_TIGA_H
#include "typedefs.h"
#define CD_CLOSE 0
#define CD_OPEN 1
#define CD_STATUS 2
#define CD_NOT_INSTALLED -4
#define TIGA -1
#define PREVIOUS 0
#define VGA 5
#define NO_INIT 0
#define INIT_GLOBALS 1
#define INIT 3
#define CLR_SCREEN 0x8000
#define USER_CP(a) ((a) | 0x4000)
/* The tags/counts allow the recorder to validate the native packet shape.
 * Host long is wider than the native DWORD; counts are native wire bytes. */
#define _WORD(a) 0, 2, (short)(a)
#define _DWORD(a) 0, 4, (long)(a)
#define _ALTDWORD_PTR(n,p) 129, (short)((n) * 4U), (long *)(p)

long tiga_set(short state);
unsigned short get_videomode(void);
short set_videomode(unsigned short mode, unsigned short flags);
void get_config(CONFIG *config);
unsigned short get_modeinfo(short index, void *mode);
short set_config(short mode, short flags);
short install_primitives(void);
short install_rlm(char *name);
long del_module(short module);
short uninstall_module(short module);
short flush_module(short module);
void install_usererror(void (*handler)());
void set_timeout(short seconds);
void synchronize(void);
short tiga_busy(void);
void get_palet(short count, PALET *entries);
short get_palet_entry(long index, unsigned char *r, unsigned char *g,
                       unsigned char *b, unsigned char *intensity);
void set_palet(long count, long first, PALET *entries);
void set_fcolor(long color);
void set_bcolor(long color);
void set_pmask(unsigned long mask);
void set_ppop(short operation);
void set_clip_rect(short left, short top, short right, short bottom);
void set_windowing(short mode);
void set_transp(short transparency);
void transp_off(void);
void set_curs_state(short state);
void clear_page(long color);
void clear_screen(long color);
void page_flip(short display_page, short draw_page);
short page_busy(void);
short text_out(short x, short y, char *text);
void init_text(void);
short get_fontinfo(short id, FONTINFO *font);
long cp_alt(short command, short packets, ...);
#endif
