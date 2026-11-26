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

`IGL_CPPFLAGS=-DIGL_ZBUFFER=1` restores z-buffering on hardware that has a z-buffer
(see below), for example to compare the globe's limb with and without it.

## Rendering and color

The default globe is key 3. IRIS GL performs the model/view transforms, 45-degree
perspective, clipping, triangle-mesh drawing, back-face rejection, and the bitmap-font
OSD. Interior bands use `bgntmesh`/`endtmesh`, and the polar caps use three-vertex
meshes. In `FLAT` mode a mesh triangle takes the color current at its last vertex, so
the driver sets the color for each mesh's first facet and then only when the Earth
index changes: 38,811 color commands instead of 261,120 at key 5.

Geometry is cached in a GL object and rebuilt for resolution or texture changes. If
the object cannot be allocated or validated, the driver draws the same geometry
directly and keeps the requested settings. A failed creation or immediate validation
stops further cache attempts until texturing is enabled again. If a previously valid
object disappears, direct drawing completes the frame and the cache can be rebuilt.

Runtime queries compare single- and double-buffered RGB(A) formats. Double buffering
is chosen whenever it offers at least 12 RGB bits, so 24-plane machines such as the
Personal IRIS and Indigo XS24 run 4/4/4 double-buffered instead of flickering in
8/8/8 single-buffered mode. Below that threshold (an Indigo Entry offers 1/2/1 double
against 3/3/2 single), the driver prefers total channel precision, then alpha and RGB
precision, then double buffering. If RGB is unavailable, it selects the largest
color-index mode with at least eight bits. Flat colors keep the Earth index and
convert to RGB when needed. Color-index mode installs the Earth palette and restores
the previous one at exit.

The globe is a single closed convex mesh, so back-face rejection alone leaves exactly
one front-facing triangle at each pixel. The driver therefore does not enable the
z-buffer, which saves its clear and the z-tested fills. Only nearly edge-on triangles
at the limb could be classified differently by the hardware's orientation test; a
build with `-DIGL_ZBUFFER=1` keeps the z-buffer where `GD_BITS_NORM_ZBUFFER` reports
one and clears color and depth together with `czclear`.

Native texturing uses `texdef2d`, `t2f`, mipmaps, and bilinear filtering whenever
`GD_TEXTURE` (read once after `gconfig`) is positive, including affine-only hardware
such as the VGX and SDKs without `GD_TEXTURE_PERSP` or the `scrsubdivide` tokens. No
screen subdivision is used: affine interpolation differs from perspective by at most
0.32 texel at key 1, 0.08 at key 2, 0.02 at the default key 3, and 0.001 at key 5.
Without native texturing (or in color-index mode), the shared software rasterizer
draws the textured globe with matrices read from IRIS GL, and one `lrectwrite`
replaces every window pixel, so those frames skip the window clear. Flat rendering
stays native.

The OSD pushes the model-view matrix and sets the projection again after the text
instead of reading either back from the graphics pipe. Font 0 glyph widths are
measured once per font height, and the help text's line breaks are kept until the
window width or font changes.

## Platform notes

`foreground()` is called before `winopen`, so IRIS GL does not fork the program into
the background: the shell waits for it, Ctrl-C and job control reach it, and its exit
status is reported.

SIGINT, SIGTERM, SIGHUP, and SIGQUIT are caught unless they were ignored at startup
(for example under `nohup`). The handler only records the signal, because GL calls
are not safe there; IRIX 4 `signal()` resets the handler on delivery, so it re-arms
itself first. The event loop then stops, restores the color map and window through
the normal cleanup, and re-raises the signal so the parent sees how the program
ended. A signal can arrive while the idle loop is blocked in `qread`. In case IRIS GL
restarts that wait, a `TIMER0` event queued every 30 ticks wakes the loop.

Not yet verified on IRIX: whether `qread` returns when a signal interrupts it, whether
`TIMER0` events are delivered to a window without input focus (and at which rate),
and how SIGHUP arrives when the window manager or terminal closes. A second signal in
the instant between delivery and re-arming still takes its default action.

The event queue supplies keyboard, drag, resize, focus, and close events. Interrupted
tied mouse samples and queue overflow cause the mouse state to be resampled, without
blocking on missing samples or discarding later keyboard and close events.

Texture definition, binding, and pixel-transfer calls return no success value. The
driver detects allocation failures and missing data, but cannot confirm silent
failures inside the library, so `texture on` means the frame was submitted natively.
Recoverable failures discard the failed state and cached geometry before drawing flat
colors.

## Validation

See the [IRIS GL checks](testing.md#iris-gl). Native validation must cover the IRIX
32-bit ABI, graphics hardware, fonts, window manager, and pixel transfer, plus the
limb without a z-buffer, affine texturing at key 1 on a VGX, and the signal paths
above.
