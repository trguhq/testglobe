/*
 * testglobe - GLUT windows, pixel formats, palettes and OSD text for the
 * shared OpenGL renderer.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdio.h>
#include "testglob.h"
#include "drv.h"
#include "drv_ext.h"
#include "e_ext.h"

#if defined(DRV_OGL) && !defined(OGL_AMIGA)
#include "ogl_glut.h"

/* GLUT 3.4 and later declare display strings even with API version 3.
 * FreeGLUT parses them but ignores their comparators, so its mode bits
 * request the same visuals. */
#if !defined(FREEGLUT) && \
    ((defined(GLUT_API_VERSION) && GLUT_API_VERSION >= 4) || \
     (defined(GLUT_XLIB_IMPLEMENTATION) && GLUT_XLIB_IMPLEMENTATION >= 9))
#define OGL_GLUT_STRINGS
#endif

/* Classic GLUT fonts, their measured widths and one display-list block of
 * glyphs per font size. A block is built once, with a current context. */
#define OGL_GLUT_GLYPHS 256
static void *ogl_glut_font;
static int ogl_glut_tier = 1;
static int ogl_glut_font_ascent = 12, ogl_glut_font_descent = 4;
static int ogl_glut_font_step = 18;
static int ogl_glut_widths[OGL_GLUT_GLYPHS];
static GLuint ogl_glut_glyphs[3];
/* 0: not built yet, 1: glyph lists, -1: draw this font directly. */
static int ogl_glut_glyph_state[3];

/* The fixed help text wraps the same way until the font or width changes.
 * drv_help holds fewer than 256 bytes and every line holds at least one. */
#define OGL_GLUT_HELP_LINES 256
static const char *ogl_glut_help_text;
static int ogl_glut_help_tier = -1, ogl_glut_help_width;
static unsigned int ogl_glut_help_lines;
static unsigned int ogl_glut_help_start[OGL_GLUT_HELP_LINES];
static unsigned int ogl_glut_help_count[OGL_GLUT_HELP_LINES];

void ogl_glut_init(int *argc, char **argv)
{
    glutInit(argc, argv);
}

/* Apple GLUT can service pending redraws during window management. Every
 * window needs a display callback, even a short-lived format probe. Keep
 * drawing disabled until the application window and globe are ready. */
static void ogl_glut_pending_display(void)
{
}

/* These queries describe the window actually created, including legacy GLUT
 * implementations which cannot check a format before creating a window. */
static int ogl_glut_window_format(Ogl_glut_format *format)
{
    memset(format, 0, sizeof(*format));
    format->rgba = glutGet(GLUT_WINDOW_RGBA) > 0;
    format->buffer = glutGet(GLUT_WINDOW_BUFFER_SIZE);
    format->depth = glutGet(GLUT_WINDOW_DEPTH_SIZE);
    format->double_buffer = glutGet(GLUT_WINDOW_DOUBLEBUFFER) > 0;
    if (format->depth < 0) format->depth = 0;
    if (format->rgba)
    {
        format->red = glutGet(GLUT_WINDOW_RED_SIZE);
        format->green = glutGet(GLUT_WINDOW_GREEN_SIZE);
        format->blue = glutGet(GLUT_WINDOW_BLUE_SIZE);
        format->alpha = glutGet(GLUT_WINDOW_ALPHA_SIZE);
        format->alpha_known = format->alpha >= 0;
        if (format->alpha < 0) format->alpha = 0;
        if (format->red <= 0 || format->green <= 0 || format->blue <= 0)
            return FALSE;
        if (format->buffer <= 0)
            format->buffer = format->red + format->green + format->blue + format->alpha;
    }
    else
    {
        format->palette = glutGet(GLUT_WINDOW_COLORMAP_SIZE);
        if (format->buffer < 8 || format->palette < 256) return FALSE;
    }
    return TRUE;
}

#ifdef GLUT_DISPLAY_MODE_POSSIBLE
/* Request classes in preference order: 8-bit RGBA, four RGB classes that
 * differ only in display-string comparators, then color index. */
#define OGL_GLUT_INDEX_CLASS 5
static unsigned int ogl_glut_mode(int quality, int double_buffer, int depth)
{
    unsigned int mode;
    mode = quality == OGL_GLUT_INDEX_CLASS ? GLUT_INDEX : GLUT_RGBA;
    if (quality == 0) mode |= GLUT_ALPHA;
    mode |= double_buffer ? GLUT_DOUBLE : GLUT_SINGLE;
    if (depth) mode |= GLUT_DEPTH;
    return mode;
}

/* Exact Earth palette indices look better than RGB too shallow for the
 * palette (fewer than 12 bits, such as 3/3/2), but not than deeper RGB. */
static int ogl_glut_rank(const Ogl_glut_format *format)
{
    if (!format->rgba) return 1;
    return format->red + format->green + format->blue >= 12 ? 2 : 0;
}

#ifndef OGL_GLUT_STRINGS
/* Mode bits alone repeat for the RGB classes; request each one once. */
static int ogl_glut_seen(unsigned int *tried, unsigned int *count, unsigned int mode)
{
    unsigned int i;
    for (i = 0; i < *count; ++i)
        if (tried[i] == mode) return TRUE;
    tried[(*count)++] = mode;
    return FALSE;
}
#endif
#endif

#if defined(GLUT_DISPLAY_MODE_POSSIBLE) && defined(FREEGLUT)
/* FreeGLUT maps each window when it is created and destroys windows only
 * from its event loop, so probe windows would stay visible. Its availability
 * query makes the same visual choice as window creation. */
static int ogl_glut_create_mode(unsigned int mode, Ogl_glut_format *format)
{
    int window;
    glutInitDisplayMode(mode);
    if (!glutGet(GLUT_DISPLAY_MODE_POSSIBLE)) return 0;
    window = glutCreateWindow(TESTGLOBE_NAME);
    if (window <= 0) return 0;
    glutDisplayFunc(ogl_glut_pending_display);
    if (ogl_glut_window_format(format)) return window;
    glutDestroyWindow(window);
    return 0;
}

static int ogl_glut_index_possible(void)
{
    int double_buffer, depth;
    for (double_buffer = 1; double_buffer >= 0; --double_buffer)
        for (depth = 1; depth >= 0; --depth)
        {
            glutInitDisplayMode(ogl_glut_mode(OGL_GLUT_INDEX_CLASS, double_buffer, depth));
            if (glutGet(GLUT_DISPLAY_MODE_POSSIBLE)) return TRUE;
        }
    return FALSE;
}

/* Walk the distinct requests in preference order and create only the
 * window that will be used. RGB precision is known only after creation, so
 * a shallow RGB window is replaced by color index when one is possible. */
static int ogl_glut_select_window(Ogl_glut_format *selected, int x, int y,
                                  int width, int height)
{
    unsigned int tried[24], count, mode, shallow_mode;
    int quality, double_buffer, depth, window, shallow;
    count = 0;
    shallow = FALSE;
    shallow_mode = 0;
    glutInitWindowSize(width, height);
    glutInitWindowPosition(x, y);
    for (quality = 0; quality <= OGL_GLUT_INDEX_CLASS; ++quality)
        for (double_buffer = 1; double_buffer >= 0; --double_buffer)
            for (depth = 1; depth >= 0; --depth)
            {
                mode = ogl_glut_mode(quality, double_buffer, depth);
                if (ogl_glut_seen(tried, &count, mode)) continue;
                if (shallow && quality != OGL_GLUT_INDEX_CLASS) continue;
                window = ogl_glut_create_mode(mode, selected);
                if (window <= 0) continue;
                if (!shallow && ogl_glut_rank(selected) == 0 && ogl_glut_index_possible())
                {
                    shallow = TRUE;
                    shallow_mode = mode;
                    glutDestroyWindow(window);
                    continue;
                }
                return window;
            }
    return shallow ? ogl_glut_create_mode(shallow_mode, selected) : 0;
}
#elif defined(GLUT_DISPLAY_MODE_POSSIBLE)
static int ogl_glut_better(const Ogl_glut_format *candidate, const Ogl_glut_format *best)
{
    int total, previous, rgb, old_rgb;
    if (ogl_glut_rank(candidate) != ogl_glut_rank(best))
        return ogl_glut_rank(candidate) > ogl_glut_rank(best);
    if (!candidate->rgba)
    {
        if (candidate->buffer != best->buffer) return candidate->buffer > best->buffer;
    }
    else
    {
        rgb = candidate->red + candidate->green + candidate->blue;
        old_rgb = best->red + best->green + best->blue;
        total = rgb + candidate->alpha; previous = old_rgb + best->alpha;
        if (total != previous) return total > previous;
        if (candidate->alpha != best->alpha) return candidate->alpha > best->alpha;
        if (rgb != old_rgb) return rgb > old_rgb;
        if (candidate->alpha_known != best->alpha_known)
            return candidate->alpha_known > best->alpha_known;
        if (candidate->buffer != best->buffer) return candidate->buffer > best->buffer;
    }
    if (candidate->double_buffer != best->double_buffer)
        return candidate->double_buffer > best->double_buffer;
    return candidate->depth > best->depth;
}

/* GLUT exposes requests and selected-window queries, rather than a portable
 * enumeration of every native pixel format. Probe supported request classes
 * and rank their actual results, giving color precision priority over buffers. */
static int ogl_glut_select_window(Ogl_glut_format *selected, int x, int y,
                                  int width, int height)
{
#ifdef OGL_GLUT_STRINGS
    static const char *requests[] = {
        "rgba red>=8 green>=8 blue>=8 alpha>=8",
        /* Classic GLUT's bare "rgba" also requires an alpha channel. */
        "rgb red>=8 green>=8 blue>=8",
        "rgb red>=5 green>=6 blue>=5",
        "rgb red>=5 green>=5 blue>=5",
        "rgb", "index buffer>=8"
    };
    char request[160], best_request[160];
#else
    unsigned int tried[24], count;
#endif
    Ogl_glut_format current;
    unsigned int mode, best_mode;
    int quality, double_buffer, depth, window, best;
    best = 0;
    best_mode = 0;
#ifdef OGL_GLUT_STRINGS
    best_request[0] = 0;
#else
    count = 0;
#endif
    glutInitWindowSize(1, 1);
    glutInitWindowPosition(-32768, -32768);
    for (quality = 0; quality <= OGL_GLUT_INDEX_CLASS; ++quality)
        for (double_buffer = 1; double_buffer >= 0; --double_buffer)
            for (depth = 1; depth >= 0; --depth)
            {
                mode = ogl_glut_mode(quality, double_buffer, depth);
#ifdef OGL_GLUT_STRINGS
                glutInitDisplayMode(mode);
                sprintf(request, "%s %s%s", requests[quality],
                         double_buffer ? "double" : "single",
                         depth ? " depth>=16" : "");
                glutInitDisplayString(request);
#else
                if (ogl_glut_seen(tried, &count, mode)) continue;
                glutInitDisplayMode(mode);
#endif
                if (!glutGet(GLUT_DISPLAY_MODE_POSSIBLE)) continue;
                window = glutCreateWindow("testglobe pixel-format probe");
                if (window <= 0) continue;
                glutDisplayFunc(ogl_glut_pending_display);
                glutHideWindow();
                if (!ogl_glut_window_format(&current))
                {
                    glutDestroyWindow(window);
                    continue;
                }
                if (best == 0 || ogl_glut_better(&current, selected))
                {
                    best = 1;
                    best_mode = mode;
#ifdef OGL_GLUT_STRINGS
                    strcpy(best_request, request);
#endif
                    *selected = current;
                }
                glutDestroyWindow(window);
            }
    if (best == 0) return 0;
    /* Do not promote a hidden one-pixel probe to the application window.
     * Some GLUT implementations discard its initial visibility/resize redraw.
     * Reapply the winning request to a normal, freshly created window. */
    glutInitDisplayMode(best_mode);
#ifdef OGL_GLUT_STRINGS
    glutInitDisplayString(best_request);
#endif
    glutInitWindowSize(width, height);
    glutInitWindowPosition(x, y);
    window = glutCreateWindow(TESTGLOBE_NAME);
    if (window > 0) glutDisplayFunc(ogl_glut_pending_display);
    return window;
}
#else
static int ogl_glut_select_window(Ogl_glut_format *selected, int x, int y,
                                  int width, int height)
{
    int window;
    /* Older GLUT may terminate on an unsupported request. Use the original
     * RGB/double-buffer request once, without probing or requiring newer
     * display-string APIs, and validate the actual returned format. */
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);
    glutInitWindowSize(width, height);
    glutInitWindowPosition(x, y);
    window = glutCreateWindow(TESTGLOBE_NAME);
    if (window <= 0) return 0;
    glutDisplayFunc(ogl_glut_pending_display);
    if (!ogl_glut_window_format(selected))
    {
        glutDestroyWindow(window);
        return 0;
    }
    return window;
}
#endif

int ogl_glut_create_window(Ogl_glut_format *format, int x, int y,
                           int width, int height)
{
    int window;
    window = ogl_glut_select_window(format, x, y, width, height);
    if (window <= 0) return 0;
    glutReshapeWindow(width, height);
    glutPositionWindow(x, y);
    glutSetWindowTitle(TESTGLOBE_NAME);
    return window;
}

void ogl_glut_callbacks(void (*resize)(int, int),
    void (*key)(unsigned char, int, int), void (*mouse)(int, int, int, int),
    void (*motion)(int, int))
{
    glutReshapeFunc(resize);
    glutMouseFunc(mouse);
    glutMotionFunc(motion);
    glutPassiveMotionFunc(motion);
    glutKeyboardFunc(key);
}

void ogl_glut_show(void)
{
    glutShowWindow();
}

int ogl_glut_ready(void)
{
    return glutGetWindow() != 0;
}

void ogl_glut_cleanup(int window)
{
    int tier;
    if (window > 0 && glutGetWindow() != 0)
    {
        for (tier = 0; tier < 3; ++tier)
            if (ogl_glut_glyphs[tier] != 0)
                glDeleteLists(ogl_glut_glyphs[tier], OGL_GLUT_GLYPHS);
        glutDestroyWindow(window);
    }
    for (tier = 0; tier < 3; ++tier)
    {
        ogl_glut_glyphs[tier] = 0;
        ogl_glut_glyph_state[tier] = 0;
    }
}

void ogl_glut_post_redisplay(void)
{
    glutPostRedisplay();
}

void ogl_glut_present(int double_buffer)
{
    if (double_buffer) glutSwapBuffers();
    else glFlush();
}

/* main initializes the globe after drv_init_window returns. Window
 * operations may redraw earlier, so only enable rendering here. */
void ogl_glut_loop(void (*draw)(void))
{
    glutDisplayFunc(draw);
    glutPostRedisplay();
    glutMainLoop();
}

void ogl_glut_install_palette(unsigned int *background, unsigned int *foreground)
{
    unsigned int index, brightness, minimum, maximum;
    minimum = 0xffffffffU; maximum = 0;
    *background = *foreground = 0;
    for (index = 0; index < 256; ++index)
    {
        glutSetColor((int)index, (GLfloat)earth_cmap[index][0] / 255.0f,
                      (GLfloat)earth_cmap[index][1] / 255.0f,
                      (GLfloat)earth_cmap[index][2] / 255.0f);
        brightness = (unsigned int)earth_cmap[index][0] * 77U +
                     (unsigned int)earth_cmap[index][1] * 150U +
                     (unsigned int)earth_cmap[index][2] * 29U;
        if (brightness < minimum) { minimum = brightness; *background = index; }
        if (brightness > maximum) { maximum = brightness; *foreground = index; }
    }
}

static void *ogl_glut_tier_font(int tier)
{
    if (tier == 0) return GLUT_BITMAP_HELVETICA_10;
    if (tier == 2) return GLUT_BITMAP_HELVETICA_18;
    return GLUT_BITMAP_HELVETICA_12;
}

/* Classic GLUT supplies widths but no bitmap-height query. These stock
 * font bounds include descenders; keep two pixels between lines. Prefer
 * a smaller font before wrapping the fixed help text, only on resize. */
void ogl_glut_select_font(int width, int height)
{
    static const int ascent[3] = {11, 12, 18}, descent[3] = {3, 4, 5};
    const unsigned char *text;
    void *font;
    int tier, extent, c;
    tier = width < 480 || height < 320 ? 0 :
           (width >= 1024 && height >= 600 ? 2 : 1);
    for (; tier > 0; --tier)
    {
        font = ogl_glut_tier_font(tier);
        extent = 0;
        for (text = (const unsigned char *)drv_help; *text != 0; ++text)
            extent += glutBitmapWidth(font, *text);
        if (extent <= width - 20) break;
    }
    font = ogl_glut_tier_font(tier);
    if (font == ogl_glut_font) return;
    ogl_glut_font = font;
    ogl_glut_tier = tier;
    ogl_glut_font_ascent = ascent[tier];
    ogl_glut_font_descent = descent[tier];
    ogl_glut_font_step = ogl_glut_font_ascent + ogl_glut_font_descent + 2;
    /* Per-frame wrapping then reads widths without calling GLUT. */
    ogl_glut_widths[0] = 0;
    for (c = 1; c < OGL_GLUT_GLYPHS; ++c)
        ogl_glut_widths[c] = glutBitmapWidth(font, c);
}

int ogl_glut_font_pending(void)
{
    return ogl_glut_glyph_state[ogl_glut_tier] == 0;
}

/* glutBitmapCharacter sets its own pixel-store state and reads the old
 * state back, a round trip per glyph on indirect GLX. A compiled list keeps
 * only the glBitmap with its already unpacked image, so replay needs neither.
 * Reserve all 256 byte values, so every text byte names a list in this
 * block; characters the font lacks compile to empty lists. */
int ogl_glut_build_font(void)
{
    GLuint base;
    int c;
    ogl_glut_glyph_state[ogl_glut_tier] = -1;
    base = glGenLists(OGL_GLUT_GLYPHS);
    if (base == 0) return FALSE;
    ogl_glut_glyphs[ogl_glut_tier] = base;
    for (c = 1; c < OGL_GLUT_GLYPHS; ++c)
    {
        glNewList(base + (GLuint)c, GL_COMPILE);
        glutBitmapCharacter(ogl_glut_font, c);
        glEndList();
    }
    ogl_glut_glyph_state[ogl_glut_tier] = 1;
    return TRUE;
}

void ogl_glut_discard_font(void)
{
    if (ogl_glut_glyphs[ogl_glut_tier] != 0)
        glDeleteLists(ogl_glut_glyphs[ogl_glut_tier], OGL_GLUT_GLYPHS);
    ogl_glut_glyphs[ogl_glut_tier] = 0;
    ogl_glut_glyph_state[ogl_glut_tier] = -1;
}

/* Native bitmap widths preserve proportional spacing. Break after the last
 * space that fits, or inside a word longer than the line. */
static unsigned int ogl_glut_line(const char *text, int width)
{
    unsigned int count, space;
    int next;
    count = space = 0;
    next = 0;
    while (text[count] != 0)
    {
        next += ogl_glut_widths[(unsigned char)text[count]];
        if (next > width) break;
        if (text[count] == ' ') space = count;
        ++count;
    }
    if (text[count] != 0 && space > 0) count = space;
    return count;
}

/* No glyph may extend outside the drawable viewport. */
static void ogl_glut_draw_line(const char *text, unsigned int count, int baseline)
{
    unsigned int i;
    if (baseline - ogl_glut_font_descent < 4 ||
        baseline + ogl_glut_font_ascent > (int)drv_win_height - 4) return;
    glRasterPos2i(10, baseline);
    if (ogl_glut_glyph_state[ogl_glut_tier] > 0)
        glCallLists((GLsizei)count, GL_UNSIGNED_BYTE, (const GLvoid *)text);
    else
        for (i = 0; i < count; ++i)
            glutBitmapCharacter(ogl_glut_font, (unsigned char)text[i]);
}

/* Measure and draw the same word wrapping, downward from baseline. */
static int ogl_glut_text(const char *text, int width, int baseline)
{
    unsigned int count;
    int lines;
    lines = 0;
    while (*text != 0 && width > 0)
    {
        count = ogl_glut_line(text, width);
        if (count == 0) break;
        ogl_glut_draw_line(text, count, baseline);
        ++lines;
        baseline -= ogl_glut_font_step;
        text += count;
        while (*text == ' ') ++text;
    }
    return lines;
}

static void ogl_glut_wrap_help(const char *help, int width)
{
    const char *text;
    unsigned int count;
    if (help == ogl_glut_help_text && ogl_glut_help_tier == ogl_glut_tier &&
        ogl_glut_help_width == width) return;
    ogl_glut_help_text = help;
    ogl_glut_help_tier = ogl_glut_tier;
    ogl_glut_help_width = width;
    ogl_glut_help_lines = 0;
    text = help;
    while (*text != 0 && width > 0 && ogl_glut_help_lines < OGL_GLUT_HELP_LINES)
    {
        count = ogl_glut_line(text, width);
        if (count == 0) break;
        ogl_glut_help_start[ogl_glut_help_lines] = (unsigned int)(text - help);
        ogl_glut_help_count[ogl_glut_help_lines++] = count;
        text += count;
        while (*text == ' ') ++text;
    }
}

/* Status lines run down from the top; help lines end at the bottom and are
 * drawn only when they cannot overlap the status. */
void ogl_glut_draw_osd(const char *status, const char *help)
{
    int width, top, bottom, status_lines;
    unsigned int i;
    width = (int)drv_win_width - 20;
    if (ogl_glut_glyph_state[ogl_glut_tier] > 0)
        glListBase(ogl_glut_glyphs[ogl_glut_tier]);
    top = (int)drv_win_height - 4 - ogl_glut_font_ascent;
    status_lines = ogl_glut_text(status, width, top);
    ogl_glut_wrap_help(help, width);
    if (ogl_glut_help_lines == 0) return;
    bottom = 4 + ogl_glut_font_descent +
             ((int)ogl_glut_help_lines - 1) * ogl_glut_font_step;
    if (bottom + ogl_glut_font_ascent + 2 >=
        top - (status_lines - 1) * ogl_glut_font_step - ogl_glut_font_descent)
        return;
    for (i = 0; i < ogl_glut_help_lines; ++i)
        ogl_glut_draw_line(help + ogl_glut_help_start[i], ogl_glut_help_count[i],
                           bottom - (int)i * ogl_glut_font_step);
}

#endif
