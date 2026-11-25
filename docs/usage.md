# Controls and status

[README](../README.md) lists the keys. This page covers the on-screen display (OSD),
frame-rate measurement, window sizing, CPU culling, and recovery from resource
shortages.

## Globe resolution

Keys `1`–`5` select Earth maps 32, 64, 128, 256, and 512 pixels wide (`EARTH_32`
through `EARTH_512`); each map is half as tall as it is wide. Matrox and TIGA include
only the first two. Native backends do not rebuild the mesh when the current
resolution is selected again.

The OSD counts quad patches for Matrox, TIGA, GDI, QuickDraw, and the native Amiga
driver, and triangles for the others. Pole patches count as quads even when their
vertices collapse to a triangle. The quad drivers also report how many polygons
remain after software culling. The other drivers report totals, because native
culling returns no visibility count.

Console builds print the globe resolution, vertex count, and triangle or quad count
after each successful mesh construction. Failed attempts and reselecting the current
resolution print nothing. GDI, QuickDraw, and Amiga report through their OSD instead.

## Frame rate

FPS starts enabled; turning it off leaves rotation timing active. FPS measures the
interval between completed presentations, including event-loop waits. It is an
interactive frame rate, not a GPU throughput benchmark, and native swap and flush
calls give different completion guarantees. Scene and rendering-mode changes restart
the sample. The running average updates after each completed frame once the clock
advances, over about one second or 60 frames. A frame lasting a second or longer
reports its own rate immediately.

Unix uses a monotonic clock where the platform provides one. Older Unix and Amiga
clocks accumulate elapsed time and rebase when the system clock moves backward, so
adjusting the clock does not stall rotation or FPS.

## Status messages

| Status | Meaning |
| --- | --- |
| `texture on` | The frame uses the graphics API's texturing path; this does not guarantee hardware acceleration. |
| `texture on (software)` | Sun XGL is texturing on a Sun ZX, which XGL renders in its own software pipeline with point sampling. |
| `texture soft` | OpenGL or IRIS GL uses the application's software texture rasterizer. |
| `texture off` | Texture mapping is disabled. |
| `texture loading` | WebGL is fetching or uploading a texture; flat colors remain visible. |
| `texture fail` | Texturing is enabled, but a detected failure prevents its use; recoverable failures draw flat colors. |
| `dlist on` | OpenGL uses a display list for the current frame. |
| `dlist off` | Display lists are disabled by `d`, or temporarily while CPU culling is on. |
| `dlist fail` | Lists are enabled but unavailable, failed, or bypassed by software texturing. |
| `cull on` / `cull off` | CPU band culling is enabled / disabled; native back-face culling stays on in both modes. |
| `dither on` / `dither off` | Amiga palette dithering is enabled / disabled; off uses the nearest solid palette color. |
| `dither n/a` | Amiga is drawing RGB directly on a truecolor RTG screen. |
| `fullscreen, double buffered` | The chipset/RTG Amiga driver is presenting complete frames on a private screen. |
| `soft3d on` | Matrox or TIGA uses host transforms while the board or API still fills polygons. |
| `soft3d off (LibShell)` / `soft3d off (TI)` | Matrox uses LibShell transforms / TIGA uses its embedded TI transform module. |
| `soft3d fail (LibShell)` | Host transforms were requested but could not prepare the frame; LibShell transforms remain active. |
| `GWorld on` / `GWorld off` | QuickDraw is drawing through an offscreen GWorld / directly to the window. |

- Toggle `t` off and on to retry after a recoverable texture failure.
- When a smaller texture is active, its width follows the status, for example
  `texture on (128)`, `texture on (software, 128)`, or `texture soft (128)`.
- OpenGL software texturing bypasses display lists, so enabled lists report
  `dlist fail` on that path.
- XGL reports `texture on (software)` only on the Sun ZX, the one device known to
  texture in XGL's software pipeline. Elsewhere that pipeline reports `texture on`.
- IRIS GL cannot detect every silent native texture failure; see
  [IRIS GL](irisgl.md#platform-notes).
- OpenGL places `dlist` before the texture status and its `d` help last. QuickDraw
  appends GWorld status after the other statistics.
- Matrox and TIGA append `soft3d` and CPU/FPU details and place `S` last in their
  help; TIGA also reports buffering. If the TI module cannot be extracted or loaded,
  `soft3d on` remains with a recovery notice. `S` retries an unavailable transform
  path from the working one.
- GDI reports direct drawing, individual polygons, reduced colors, or manual rotation
  when those fallbacks are active.

## Window size and fonts

On displays of 640 × 480 or smaller, GDI and QuickDraw default to a 320 × 240 client
area; larger displays use 640 × 480. Explicit size requests are kept, although
QuickDraw clamps its window to the selected screen. GDI, QuickDraw, and Amiga fit the
projection to the client area, so narrow windows clip the globe rather than
stretching it. Amiga also corrects for the display mode's pixel proportions.

OpenGL, XGL, GDI, and QuickDraw choose small, normal, or large OSD fonts from the
window size. Both Amiga drivers select resident Topaz 8 or 9 and keep the current
font if selection fails. Measured text widths and font bounds control wrapping, keep
text inside the window, and stop status and help from overlapping; very small
windows may omit help. WebGL uses responsive CSS sizes. Matrox keeps its 8 × 16 font,
and IRIS GL and TIGA use their native system fonts. TIGA measures help before placing
it, so a large system font cannot hide the final `S` control, and shortens the
statistics when space is tight to keep counts, FPS, and `soft3d`. No external font
files are required.

## CPU culling

CPU culling (`c`) finds the front-facing longitude range of each latitude band and
submits only those triangle-strip runs. Checks near the range boundaries preserve
the silhouette, winding, and texture coordinates, and the range comes from the
perspective camera position rather than assuming exactly half the globe is visible.
It can reduce geometry submission, but the benefit depends on the platform. Sun XGL
starts with it on because the Sun ZX is limited by submitted triangles; the other
drivers start with it off.

OpenGL display lists and IRIS GL geometry objects are bypassed while culling is on.
OpenGL remembers the `d` setting, including changes made while culling is on, and
reapplies it when culling is turned off. The polygon total still describes the
complete mesh. Nearly edge-on facets can differ by one silhouette pixel, because CPU
and native transforms round differently.

## Resource recovery

When a new mesh cannot be allocated, optional rendering caches are released before
retrying. The application can then restore the previous resolution or choose a
smaller one; startup uses the same fallback. The OSD shows the result and any
recovery notice, including the failure category. If no scene can be built, the window
stays responsive so another resolution can be selected. Bad data and invalid
requests are reported separately from insufficient memory. OpenGL, IRIS GL, and XGL
show a failed-frame diagnostic even with the OSD off, and stop automatic redraws until
input requests another attempt.

A texture-memory shortage or a native size limit can reduce the image from 512 to
256, 128, 64, or 32 pixels wide. Other native errors are not treated as memory
shortages. If no usable texture can be created, the request stays enabled, flat
colors are drawn, and the OSD shows `texture fail`. Geometry caches and large
submission packets are optional: direct drawing or smaller packets draw the same
globe. Fallback resolution, texture size, buffering, and cache use all affect
performance, so compare runs by their reported settings.

These fallbacks keep usable native resources. Choosing another valid resource can
recover from bad map data but does not repair it. A lost device or an opaque native
library error can still require the program to exit.
