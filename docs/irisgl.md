# IRIS GL

## Requirements

Native 32-bit MIPS IRIX 4.0 with the IRIS GL development headers and runtime.
Windowing, input, and rendering all use IRIS GL directly.

## Build

```sh
make irisgl
./testglobe-irisgl
```

`IGL_CC` defaults to `CC`; override `IGL_CPPFLAGS`, `IGL_CFLAGS`, `IGL_SYS_FLAGS`, and
`IGL_LDFLAGS` for the installed tools. The defaults select `FLAT`, `DRV_IGL`, C89
math, ANSI C mode (`-xansi`, added to `CFLAGS`), and the shared libraries
`-lgl_s -lm -lc_s`. These static shared libraries resolve in one pass, so the shared
C library comes last, after the libraries that call it. No later MIPS ISA or ABI is
imposed.

## Rendering and color

The default globe is key 3. IRIS GL performs the model/view transforms, 45-degree
perspective, clipping, triangle-mesh drawing, back-face rejection, and the bitmap-font
OSD. Interior bands use `bgntmesh`/`endtmesh`, and the polar caps use three-vertex
meshes.

Geometry is cached in a GL object and rebuilt for resolution or texture changes. If
the object cannot be allocated or validated, the driver draws the same geometry
directly and keeps the requested settings. A failed creation or immediate validation
stops further cache attempts until texturing is enabled again. If a previously valid
object disappears, direct drawing completes the frame and the cache can be rebuilt.

Runtime queries compare single- and double-buffered RGB(A) formats, preferring total
channel precision, then alpha and RGB precision, then double buffering. If RGB is
unavailable, the driver selects the largest color-index mode with at least eight
bits. Flat colors keep the Earth index and convert to RGB when needed. Color-index
mode installs the Earth palette and restores the previous one at exit. A z-buffer is
used when available; the convex globe can also be drawn with back-face rejection
alone.

Native texturing uses `texdef2d`, `t2f`, mipmaps, and bilinear filtering. When it is
unavailable, the shared software rasterizer draws the textured globe with matrices
read from IRIS GL and presents the image through IRIS GL. Flat rendering stays
native.

## Platform notes

The event queue supplies keyboard, drag, resize, focus, and close events. Interrupted
tied mouse samples and queue overflow cause the mouse state to be resampled, without
blocking on missing samples or discarding later keyboard and close events.

Texture definition, binding, and pixel-transfer calls return no success value. The
driver detects allocation failures, missing data, and lost texture capability, but
cannot confirm silent failures inside the library, so `texture on` means the frame
was submitted natively. Recoverable failures discard the failed state and cached
geometry before drawing flat colors.

## Validation

See the [IRIS GL checks](testing.md#iris-gl). Native validation must cover the IRIX
32-bit ABI, graphics hardware, fonts, window manager, and pixel transfer.
