/*
 * testglobe - Amiga MiniGL platform support for drv_ogl.
 * Copyright (C) 2026 trguhq
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef ogl_ami_h
#define ogl_ami_h

/* The external V29 SDK uses GCC's spelling of inline and integer types.
 * Keep these aliases local to its headers; application code remains C89. */
#ifdef __VBCC__
#ifndef __inline__
#define __inline__ inline
#define OGL_AMI_UNDEF_INLINE
#endif
#ifndef __UINT32_TYPE__
#define __UINT32_TYPE__ unsigned int
#define OGL_AMI_UNDEF_UINT32
#endif
#ifndef __UINTPTR_TYPE__
#define __UINTPTR_TYPE__ unsigned int
#define OGL_AMI_UNDEF_UINTPTR
#endif
#endif
#include <proto/minigl.h>
#ifdef OGL_AMI_UNDEF_INLINE
#undef __inline__
#undef OGL_AMI_UNDEF_INLINE
#endif
#ifdef OGL_AMI_UNDEF_UINT32
#undef __UINT32_TYPE__
#undef OGL_AMI_UNDEF_UINT32
#endif
#ifdef OGL_AMI_UNDEF_UINTPTR
#undef __UINTPTR_TYPE__
#undef OGL_AMI_UNDEF_UINTPTR
#endif

#if MINIGL_DISPATCH_ABI_VERSION != 5
#error amiga-opengl requires the MiniGL V29 SDK (dispatch ABI 5)
#endif

#define OGL_AMI_LEFT_BUTTON 0
#define OGL_AMI_RIGHT_BUTTON 2
#define OGL_AMI_DOWN 0
#define OGL_AMI_UP 1
#define OGL_AMI_STACK_BYTES 131072UL
#define OGL_AMI_MAX_DIMENSION 4096
#define OGL_AMI_VERTEX_LIMIT 256U

extern void ogl_ami_init(int *argc, char **argv);
extern int ogl_ami_create_window(int x, int y, int width, int height);
extern void ogl_ami_cleanup(void);
extern int ogl_ami_ready(void);
extern void ogl_ami_present(void);
extern void ogl_ami_post_redisplay(void);
extern void ogl_ami_get_size(int *width, int *height);
/* 0: no context, 1: existing context retained, 2: a new context was made. */
extern int ogl_ami_resize_context(int width, int height);
extern void ogl_ami_loop(void (*draw)(void), void (*resize)(int, int),
    void (*key)(unsigned char, int, int), void (*mouse)(int, int, int, int),
    void (*motion)(int, int));
extern void ogl_ami_draw_string(const char *text, int x, int y);
extern double ogl_ami_time_now(void);
extern void ogl_ami_error(char *message);

#endif
