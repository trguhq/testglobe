/*
 * testglobe - GLUT platform support for drv_ogl.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ogl_glut_h
#define ogl_glut_h

#if defined(__APPLE__)
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

#define OGL_GLUT_LEFT_BUTTON GLUT_LEFT_BUTTON
#define OGL_GLUT_RIGHT_BUTTON GLUT_RIGHT_BUTTON
#define OGL_GLUT_DOWN GLUT_DOWN
#define OGL_GLUT_UP GLUT_UP

/* Attributes GLUT reports for the window it actually created. */
typedef struct Ogl_glut_format
{
    int rgba, red, green, blue, alpha, buffer, depth, double_buffer, palette;
    int alpha_known;
} Ogl_glut_format;

extern void ogl_glut_init(int *argc, char **argv);
/* Choose a validated RGB(A) or 8-bit index format and create the hidden
 * application window at its final size; 0 if no usable format exists. */
extern int ogl_glut_create_window(Ogl_glut_format *format, int x, int y,
                                  int width, int height);
extern void ogl_glut_callbacks(void (*resize)(int, int),
    void (*key)(unsigned char, int, int), void (*mouse)(int, int, int, int),
    void (*motion)(int, int));
extern void ogl_glut_show(void);
extern int ogl_glut_ready(void);
/* Delete GLUT-owned GL objects and destroy the window, if it still exists. */
extern void ogl_glut_cleanup(int window);
extern void ogl_glut_post_redisplay(void);
extern void ogl_glut_present(int double_buffer);
extern void ogl_glut_loop(void (*draw)(void));
/* Load the Earth palette; returns its darkest and brightest entries. */
extern void ogl_glut_install_palette(unsigned int *background,
                                     unsigned int *foreground);

/* OSD text. Selecting a font needs no GL context. Glyph lists are built
 * separately, with a current context, and the caller checks GL errors. */
extern void ogl_glut_select_font(int width, int height);
extern int ogl_glut_font_pending(void);
/* FALSE if no list block was available; text is then drawn directly. */
extern int ogl_glut_build_font(void);
/* Discard the current font's lists after a GL error; draw it directly. */
extern void ogl_glut_discard_font(void);
extern void ogl_glut_draw_osd(const char *status, const char *help);

#endif
