# Sun XGL

## Requirements

32-bit SPARC Solaris 2.6 with Sun XGL 3.x, X11/OpenWindows, and a 24-bit TrueColor
visual. The driver uses Xlib directly for windowing and input.

## Build

```sh
make xgl
./testglobe-xgl
```

For nonstandard SDK locations:

```sh
make xgl XGLHOME=/path/to/xgl OPENWINHOME=/path/to/openwin
```

The defaults are `/opt/SUNWits/Graphics-sw/xgl` and `/usr/openwin`. Override
`XGL_CPPFLAGS`, `XGL_CFLAGS`, `XGL_SYS_FLAGS`, and `XGL_LDFLAGS` as needed; the native
compiler's 32-bit default is used. When the runtime is relocated, also set `XGLHOME`
in the shell so XGL finds its pipelines and fonts.

### Argument checking

XGL argument checking (`XGL_SYS_ST_ERROR_DETECTION`) is on by default. Sun notes that
it costs performance, so it can be turned off for timing runs:

```sh
TESTGLOBE_XGL_NOCHECK=1 ./testglobe-xgl
```

With checking off, XGL reports only resource and system errors, so invalid-argument
and configuration failures that texture recovery would otherwise catch go
unreported. An empty value or `0` keeps checking on. The startup device line shows
which is in use.

## Rendering and color

The default globe is key 3. Each interior latitude band is one `xgl_triangle_strip`,
and the polar caps are nondegenerate three-vertex strips. Both triangles of an Earth
patch share its flat RGB color. XGL performs the transforms, clipping, back-face
handling, depth testing, and texturing; texture mapping uses REPLACE mode and keeps
UV seams.

- **Submission:** the globe Context uses XGL's default ASTI deferral, which Sun
  recommends over ASAP. Each frame flushes it synchronously before the OSD and buffer
  switch.
- **Transforms:** the model matrix is composed on the CPU and written once per frame
  as a length-preserving transform.
- **Window:** the window is mapped before the XGL raster is created; backing store is
  refused so that double buffering is granted; and the server is synchronized before
  each raster resize. X11 handles keyboard, mouse, expose, resize, and close events.
- **OSD:** text wraps using native stroke-font measurements. Windows too small for a
  line omit it, and help never overlaps the statistics. The fixed help text is wrapped
  once per window width and font size; only the statistics are measured every frame.

## Platform notes

At startup the driver prints the device that `xgl_inquire()` reports, for example:

```text
Sun XGL device: Sun:LEO, direct graphics access, RGB hardware, Z buffer hardware, shading hardware, maximum buffers 2, argument checking on
```

Missing acceleration is reported, not refused, so remote and software-rendered
displays still work.

XGL falls back to its own software pipeline when hardware cannot texture. The API
reports no texture capability and no per-frame backend, so successful texturing shows
`texture on` on either path, except on the Sun ZX. Recoverable texture failures
release partial resources and draw flat colors. Nonrecoverable errors, and setup,
rendering, or OSD errors unrelated to texturing, remain fatal. Their diagnostics
include the XGL object, operands, error type, and category, because Solaris XGL can
return an empty error ID. Texture rasters are filled from indexed map rows without an
intermediate RGB image. If strip packets cannot be allocated, smaller strips keep the
overlap, winding, colors, and UVs.

### Sun ZX

XGL reports the Sun ZX (Leo) as `Sun:LEO`; any device whose model name begins with
`LEO` is treated the same way. These notes follow measurements that
[XGLQuake](https://github.com/trguhq/XGLQuake) and its `xglbench` made on a
SPARCstation 10 with one 60 MHz SM61 SuperSPARC module, Solaris 2.6, XGL 3.3, and a
Sun ZX.

- **Texturing is software:** xglbench measured 1.84 seconds for one frame of 512
  point-sampled triangles. `t` still works, but uses XGL's cheapest sampling
  (`XGL_TEXTURE_INTERP_POINT` for both minification and magnification), shows
  `texture on (software)`, and adds a notice that frames are slow. Keyboard and window
  events are read only between frames.
- **Triangle-bound:** the ZX is limited by the number of triangles sent, not their
  pixels, so XGL starts with CPU culling (`c`) on. XGL's own back-face culling still
  processes every triangle it receives.
- **Retrace-locked:** a buffer switch waits for vertical retrace (measured at
  13.12 ms, about 76 Hz), so the frame rate moves in steps such as 19.1, 15.2, and
  12.7 FPS.

Set `TESTGLOBE_XGL_NOCHECK=1` to compare frame rates without
[argument checking](#argument-checking).

## Validation

See the [Sun XGL checks](testing.md#sun-xgl). Native SDK compilation, ABI
compatibility, and rendering require the Sun SDK and runtime on Solaris.
