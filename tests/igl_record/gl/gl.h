/* Host call-recording declarations, derived from the IRIS GL manuals.
 * This is not an SGI SDK, ABI, or renderer. Values identify fixture states. */
#ifndef TEST_IGL_GL_H
#define TEST_IGL_GL_H

typedef float Coord;
typedef long Icoord;
typedef short Angle;
typedef short Screencoord;
typedef unsigned short Colorindex;
typedef unsigned short Device;
typedef long Boolean;
typedef float Matrix[4][4];
typedef long Object;

#define FALSE 0
#define TRUE 1
#define MVIEWING 2
#define MPROJECTION 1
#define MSINGLE 0
#define FLAT 0
#define GOURAUD 1
#define GD_BITS_NORM_DBL_RED 9
#define GD_BITS_NORM_DBL_GREEN 10
#define GD_BITS_NORM_DBL_BLUE 11
#define GD_BITS_NORM_SNG_RED 6
#define GD_BITS_NORM_SNG_GREEN 7
#define GD_BITS_NORM_SNG_BLUE 8
#define GD_BITS_NORM_ZBUFFER 16
#ifndef IGL_TEST_OLD_HEADERS
#define GD_BITS_NORM_SNG_ALPHA 21
#define GD_BITS_NORM_DBL_ALPHA 22
#endif
#define GD_BITS_NORM_DBL_CMODE 13
#define GD_BITS_NORM_SNG_CMODE 12
#define GD_ZMIN 4
#define GD_ZMAX 5
#define GD_XPMAX 0
#define GD_YPMAX 1
#define GD_TEXTURE 80
#ifndef IGL_TEST_OLD_HEADERS
#define GD_TEXTURE_PERSP 82
#endif
#define GD_DITHER 28
#define TX_TEXTURE_0 0
#define TX_NULL 0x000
#define TX_MINFILTER 0x100
#define TX_MAGFILTER 0x200
#define TX_WRAP 0x300
#define TX_WRAP_S 0x310
#define TX_WRAP_T 0x320
#define TX_REPEAT 0x301
#define TX_CLAMP 0x302
#define TX_BILINEAR 0x220
#define TX_POINT 0x110
#define TX_MIPMAP_LINEAR 0x122
#define TX_MIPMAP_BILINEAR 0x123
#define TX_MIPMAP_TRILINEAR 0x124
#define TV_ENV0 0
#define TV_NULL 0x000
#define TV_MODULATE 0x101
#define TV_DECAL 0x103
#define TV_BLEND 0x102
/* These optional numeric values are fixture tags, not SGI ABI constants.
 * The 1990 reference documents both token names and scrsubdivide's types. */
#ifdef IGL_TEST_SUBDIVIDE
#define SS_OFF 0
#define SS_DEPTH 1
extern void scrsubdivide(long mode, float params[]);
#endif
#define ZF_LEQUAL 3
#define NORMALDRAW 0x010
#define BLACK 0
#define WHITE 7
extern long getgdesc(long inquiry);
extern long getplanes(void);
extern long getheight(void);
extern long strwidth(char *text);
extern void qreset(void);
extern void lrectwrite(Screencoord left, Screencoord bottom, Screencoord right, Screencoord top, unsigned long *pixels);
extern void prefsize(long width, long height);
extern void prefposition(long left, long right, long bottom, long top);
extern long winopen(char *title);
extern void winclose(long window);
extern void winconstraints(void);
extern void gexit(void);
extern void RGBmode(void);
extern void cmode(void);
extern void singlebuffer(void);
extern void doublebuffer(void);
extern void gconfig(void);
extern void getsize(long *width, long *height);
extern void getorigin(long *x, long *y);
extern void reshapeviewport(void);
extern void viewport(Screencoord left, Screencoord right,
                      Screencoord bottom, Screencoord top);
extern void mmode(short mode);
extern void loadmatrix(Matrix matrix);
extern void getmatrix(Matrix matrix);
extern void pushmatrix(void);
extern void popmatrix(void);
extern void perspective(Angle fov, float aspect, Coord near_plane, Coord far_plane);
extern void ortho2(Coord left, Coord right, Coord bottom, Coord top);
extern void translate(Coord x, Coord y, Coord z);
extern void rot(float angle, char axis);
extern void rotate(Angle angle, char axis);
extern void backface(Boolean enabled);
extern void zbuffer(Boolean enabled);
extern void zclear(void);
extern void czclear(unsigned long background, long depth);
extern void lsetdepth(long near_depth, long far_depth);
extern void zfunction(long function);
extern void clear(void);
extern void shademodel(long model);
extern void subpixel(Boolean enable);
extern void bgnpolygon(void);
extern void endpolygon(void);
extern void v3f(float vertex[3]);
extern void c3f(float rgb[3]);
extern void c3s(short rgb[3]);
extern void cpack(unsigned long packed);
extern void RGBcolor(short red, short green, short blue);
extern void color(Colorindex index);
extern void mapcolor(Colorindex index, short red, short green, short blue);
extern void getmcolor(Colorindex index, short *red, short *green, short *blue);
extern void texdef2d(long index, long components, long width, long height,
                      unsigned long *image, long properties, float *props);
extern void texbind(long target, long index);
extern void tevdef(long index, long properties, float *props);
extern void tevbind(long target, long index);
extern void t2f(float coordinate[2]);
extern void font(short index);
extern void cmov2i(Icoord x, Icoord y);
extern void charstr(char *text);
extern void qdevice(Device device);
extern long qtest(void);
extern long qread(short *value);
extern long getvaluator(Device device);
extern Boolean getbutton(Device device);
extern void tie(Device button, Device first, Device second);
extern void swapbuffers(void);
extern void gflush(void);
extern void frontbuffer(Boolean enabled);
extern void backbuffer(Boolean enabled);
extern long genobj(void);
extern void makeobj(long object);
extern void closeobj(void);
extern void callobj(long object);
extern void delobj(long object);
extern Boolean isobj(long object);
#endif
