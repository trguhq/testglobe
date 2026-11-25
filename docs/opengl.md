# OpenGL

OpenGL is testglobe's original backend. One driver, `drv_ogl.c`, serves desktop
OpenGL through GLUT on Unix, Solaris, and modern macOS, and Amiga PiStorm3D through
MiniGL.

## Requirements

The desktop targets need OpenGL 1.1, GLU, and GLUT, with a legacy or compatibility
context. On Debian or Ubuntu, install the development libraries with:

```sh
sudo apt-get install freeglut3-dev
```

Archived Solaris GLUT packages are available for
[32-bit SPARC](https://web.archive.org/web/20010726175006/http://reality.sgi.com/opengl/glut3/glut-3.7b.sparc_solaris.tar.gz)
and
[64-bit SPARC](https://web.archive.org/web/20010726192034/http://reality.sgi.com/opengl/glut3/glut-3.7b.sparc_solaris_64.tar.gz).
Modern macOS uses Apple's frameworks. [Amiga OpenGL](#amiga-opengl) uses MiniGL and
native Amiga windowing instead.

## Build

### Unix (`opengl`)

```sh
make opengl
./testglobe-opengl
```

The default compiler is `cc` with `-O`. Override `CC`, `CPPFLAGS`, `CFLAGS`,
`SYS_FLAGS`, and `LDFLAGS` for the installed compiler and libraries.

### Solaris (`solaris-opengl`)

Use the Solaris C compiler, OpenGL/GLU, GLUT, and OpenWindows libraries:

```sh
make solaris-opengl
./testglobe-opengl
```

`SOLARIS_CC` defaults to `CC` and `SOLARIS_CFLAGS` to `CFLAGS`.
`OPENWINHOME=/usr/openwin` selects the headers, libraries, and runtime search path.
Override `SOLARIS_CPPFLAGS`, `SOLARIS_SYS_FLAGS`, and `SOLARIS_LDFLAGS` as needed;
the compiler's default architecture and ABI are kept. The equivalent settings for the
generic target are:

```sh
make opengl CC=cc CPPFLAGS=-I/usr/openwin/include \
  LDFLAGS="-L/usr/openwin/lib -R/usr/openwin/lib -lglut -lGL -lGLU -lX11 -lXmu -lm"
```

### Modern macOS (`apple-opengl`)

Use the selected Xcode Command Line Tools and their macOS SDK:

```sh
make apple-opengl
./testglobe-opengl
```

This target links `-framework GLUT -framework OpenGL -framework Cocoa`, with GLU
from `OpenGL.framework` and math from the system runtime; Homebrew and XQuartz are not
needed. The toolchain chooses the Intel or Apple silicon architecture and SDK.
`APPLE_CC` defaults to `CC` (normally Apple's Clang) and `APPLE_CFLAGS` to `CFLAGS`.
Override `APPLE_CPPFLAGS`, `APPLE_SYS_FLAGS`, and `APPLE_LDFLAGS` as needed. The
default `APPLE_SYS_FLAGS=-DGLUT_SILENCE_DEPRECATION` suppresses GLUT deprecation
warnings; Apple builds include `<GLUT/glut.h>`.

## Rendering and color

The default globe is key 5. Desktop OpenGL sends each interior latitude band as a
triangle strip, and both polar caps as one batch of nondegenerate triangles. Flat
colors, winding, and texture seams match the independent-triangle path. Display lists
cache the geometry and start enabled; `d` deletes the cache, and enabling it again
rebuilds it on the next frame.

The driver probes RGBA, RGB, and color-index formats with single/double-buffer and
depth-buffer alternatives. It prefers RGB(A) over color index, then greater total
RGB-plus-alpha precision, then double buffering. Requests include 8/8/8/8, 8/8/8,
5/6/5, 5/5/5, unconstrained RGB(A), and color index with at least eight bits. GLUT
probes do not enumerate every native visual, and older implementations may offer
fewer choices; classic GLUT RGB fallbacks do not require an alpha channel. When GLUT
lacks `GLUT_DISPLAY_MODE_POSSIBLE`, the driver makes the original single RGB,
double-buffered window request and reads the resulting window attributes. It cannot
probe alternatives safely there, because an unsupported request may terminate GLUT
instead of returning a failure.

Flat shading keeps each Earth palette byte, converting it to RGB for RGB(A) windows
or submitting the index directly. Classic OpenGL texturing requires RGB(A), so
color-index windows use the shared software texture rasterizer, which bypasses display
lists.

All OpenGL targets use OpenGL 1.1 texture uploads (`glTexImage2D`) with bilinear
minification and magnification, including indexed software texturing. Only the
selected base image is uploaded or kept, with no mipmap chain, because coarse
isotropic mipmaps can mix distant land and ice into ocean pixels at the silhouette and
produce pale speckles. Very small views can instead alias fine texture detail. No post-1.1
entry points, extension lookups, or `NO_GENMIPMAP` flag are needed.

## Platform notes

Display-list allocation, compilation, validation, and execution are checked with
OpenGL 1.1 calls, including `glIsList` and `glGetError`; diagnostics do not use modern
debug extensions. A failed cache is discarded and the geometry drawn immediately.
Texture-memory shortages and native size limits try smaller images, while other
texture failures release the failed resources and draw flat colors. Failed lists are
not rebuilt every frame or on resolution changes; toggling `d` or `t` permits a
retry.

## Amiga OpenGL

### Requirements

The `amiga-opengl` target reuses `drv_ogl.c` with the Amiga adapter `ogl_ami.c`. It
needs AmigaOS 3.x; a PiStorm on Raspberry Pi 4, CM4, or 4B hardware; RTG graphics
with `cybergraphics.library` V40 or later (Picasso96 compatibility included); and
PiStorm3D's shared `minigl.library` V29 with dispatch ABI 5. Code is built for a
68020 with 68881 floating point, both supplied by the PiStorm environment. The
separate [`amiga` target](amiga.md) provides chipset/RTG software rendering.

Use the `MiniGL_Classic` branch of the
[MiniGL SDK repository](https://github.com/SteffenHaeuser/MiniGL_Library_68k/), whose
`libraryroot/include` directory holds the current shared API headers (checked at
commit
[`d950a3a1`](https://github.com/SteffenHaeuser/MiniGL_Library_68k/commit/d950a3a1ed640785a735f7d594298eaf3233b794)).
The root `include` directory on older branches holds ABI 3 headers with different GL
token values and is not interchangeable. The application checks the library's
version, dispatch ABI, and table size before using it. Install the matching
**PiStorm3D** runtime as `LIBS:minigl.library`; the SDK branch name does not select
the graphics hardware.

### Build

Cross-compilation needs VBCC's `aos68k` target, vasm, vlink, the Amiga NDK headers, a
host C compiler for the Earth-data converter, and Python 3.8 or later for the icon:

```sh
git clone --branch MiniGL_Classic --single-branch \
  https://github.com/SteffenHaeuser/MiniGL_Library_68k.git minigl-sdk
export VBCC=/path/to/vbcc
export PATH="$VBCC/bin:$PATH"
make amiga-opengl MINIGL_ROOT=/path/to/minigl-sdk
```

The output is `testglobe-amiga-opengl` and its Workbench icon,
`testglobe-amiga-opengl.info`; copy both for icon launch. The icon requests a
128 KiB stack. From the CLI, set the stack first:

```text
Stack 131072
testglobe-amiga-opengl
```

`AOGL_CC`, `AOGL_CONFIG`, `AOGL_CFLAGS`, `AOGL_CPPFLAGS`, `AOGL_INCLUDES`,
`AOGL_LDFLAGS`, `AOGL_LIBS`, and `AOGL_PYTHON` configure this target. Defaults are
`vc +aos68k`, `-c99 -O1 -cpu=68020 -fpu=68881`, and `-lm881 -lamiga`. C99 mode is for
the external SDK headers; the shared sources stay C89. Add NDK include paths through
`AOGL_CPPFLAGS` if the VBCC configuration does not supply them. `EARTH_CC` selects the
host data converter's compiler. Earth maps are linked from generated `e_data.c`, as in
desktop OpenGL, so no runtime map files or Hunk packing are needed.

### Rendering

The adapter opens the shared library directly through its dispatch interface, so no
GCC import archive, GLUT, X11, or separate GLU library is linked. MiniGL performs
transforms, clipping, depth testing, culling, bilinear texture mapping, and display
lists. Intuition supplies the resizable Workbench window and input, and MiniGL
renders through `mglCreateContextFromBitMap` into an application-owned BGRA32 RTG
bitmap. Presentation waits for the GPU and copies within the allocated bitmap before
native Amiga text draws the OSD. Resizing recreates the bitmap and MiniGL context,
then restores rendering state and the requested texture.

All five resolutions are available, starting at key 5 with textures off and display
lists on, in RGB. PiStorm3D's flat-shading color applies to a whole submission, so
flat strips are split into runs of one color and pole triangles are sent separately.
Textured strips are bounded by the native vertex-buffer limit. These splits keep
facet colors, winding, and texture seams, and leave the shared `TRISTRIP` traversal
unchanged.

PiStorm3D V29 display-list replay does not preserve the flat-color runs, so the
driver bypasses lists for flat-colored frames and reports `dlist fail` while the
toggle stays on. Textured frames can use display lists, because their `GL_REPLACE`
material does not depend on the replayed vertex colors.

## Validation

See the [OpenGL checks](testing.md#opengl). Color-index behavior needs an
implementation that offers such a visual. Apple SDK compilation and rendering require
macOS, and Solaris builds need its installed graphics libraries. The
[Amiga OpenGL checks](testing.md#amiga-opengl) cover MiniGL submission and Amiga
events; VBCC/SDK cross-builds check native compilation. PiStorm3D hardware is still
needed to validate VideoCore rendering, library ABI behavior, and RTG presentation.
