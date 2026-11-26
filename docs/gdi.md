# Windows GDI

## Requirements

Windows 3.0 in standard or 386 enhanced mode, or Windows 3.1, in a 256-color display
mode; Microsoft C/C++ 7.0 with the Windows emulation runtime (`LLIBCEW.LIB`); and the
Windows 3.1 SDK headers, `LIBW.LIB`, 16-bit linker, and resource compiler. The target
is a native 16-bit protected-mode application; Windows 3.0 real mode is not
supported. A [cross-build](build.md#linux-hosted-cross-builds) with OpenWatcom is also
available, and a prebuilt copy is in [downloads](../downloads/README.md).

The executable is marked for Windows 3.0, and every API it calls (the palette manager,
`CreateDIBPatternBrush`, `GetWinFlags`, `PolyPolygon`, and `DrawText` with
`DT_CALCRECT`) exists there. This marking still needs native validation on both
Windows 3.0 and 3.1.

## Build

Configure `PATH`, `INCLUDE`, and `LIB`, then run:

```text
nmake gdi
```

Run `testglob.exe` inside Windows. Override `GDI_CC`, `GDI_LINK`, `GDI_RC`,
`GDI_CFLAGS`, `GDI_CPPFLAGS`, `GDI_LIBS`, `GDI_LDFLAGS`, `GDI_RCFLAGS`, and `GDI_DATA`
as needed. Keep `-AH -G2 -Gw -Zp1 -FPi` in `GDI_CFLAGS` to preserve the memory model,
Windows ABI, and optional FPU support. `GDI_DATA` defaults to `data` at the root and
`..\data` inside `src`.

The `.def` file declares `EXETYPE WINDOWS 3.0`, exports the window procedure, and sets
the segment attributes, an 8 KiB local heap, and a 16 KiB stack. `RC -r -fo` compiles
`src/gdi.rc` and all 13 map chunks into `g_earth.res`, and `RC -30 -t g_earth.res
testglob.exe` attaches it and marks a protected-mode Windows 3.0 application. The
`-30` is part of the rule, so it is kept when you override `GDI_RCFLAGS` (default
`-t`). Discardable `RCDATA` chunks avoid large static arrays. As a huge-model
application, only one instance can run at a time.

## Rendering and color

The default globe is key 1, with all five resolutions and no texture mapping. The
window uses the display's 8-bit depth. A 256-entry logical Earth palette is realized
and matched when the window is first activated, and Windows keeps its reserved system
colors. While the window is active, another application's palette change keeps the
foreground mapping. An 8-bit compatible bitmap and memory DC normally provide the back
buffer; if they cannot be allocated, the driver paints directly into the window's
paint DC. Each paint realizes the palette once.

Colors that the palette realizes exactly use a solid brush and a pen of the same
palette index. For the others, the driver uses the same two-color matcher as Amiga,
choosing from the colors Windows actually realized, and draws with a palette-indexed
pattern brush. Palette changes refresh the matches and the brush and pen caches. If
palette queries or pattern allocation fail, drawing falls back to a solid palette
brush. Pens are cached for at most 64 palette indices; other solid batches use a
temporary pen, which keeps the driver's share of the 64 KiB GDI heap bounded.

Software transforms, perspective projection, clipping, and culling operate on whole
quads. Bounded packets reuse adjoining transformed rows and projected corners, and the
retained polygons are grouped by palette index into `PolyPolygon` calls of at most 64
polygons, in ascending palette order. Every contour repeats its first vertex, as Win16
`PolyPolygon` requires. Each packet selects one brush and pen per color and restores
the DC's own objects once. Pole vertices collapse to triangles. Visibility relies on
the convex globe rather than a depth buffer. Native Windows messages handle input,
resizing, palette changes, and closing.

Only pattern brushes are applied through a reusable monochrome mask that covers both
the polygon fills and one-pixel outlines. This keeps dithering continuous across
shared edges and avoids Win16's incomplete coverage with `NULL_PEN`; solid colors get
the same coverage from their matching outline. The mask is allocated when the first
dithered color is drawn and adds about 9.4 KiB at 320 × 240 or 37.5 KiB at
640 × 480, plus GDI overhead. Its text and background colors are set once per frame.
If the mask fails, matching solid fills and outlines still close every facet; if a
pen cannot be allocated, stock black or white is used.

A failed `PolyPolygon` call falls back to individual `Polygon` calls. Brush and pen
allocation can reclaim cached objects or use a stock color.

## Animation and updates

While auto-rotation is on and the window is active, the message loop draws a frame
whenever `PeekMessage` finds no message; `PeekMessage` has already yielded to other
applications by then. Frames are therefore not limited to the 18.2 Hz timer. When the
window is inactive, iconic, or not rotating, the loop calls `WaitMessage`. The 30 ms
timer remains as a fallback: it animates only when no idle frame was drawn since its
previous tick, for example in the modal loops of a menu or a window move. Rotation
follows `GetTickCount`, so its smoothness depends on that clock's resolution. If the
timer cannot be created, manual rotation and keyboard input still work.

Timer, idle, and drag frames invalidate only the previous frame's globe bounds and
OSD text rectangles. After drawing, the parts of the new frame outside those areas are
presented from the buffer without drawing again. Keys, resizing, palette changes,
OSD toggles, failed frames, and direct drawing still update the whole client area.
Timer messages are ignored while the window is iconic. A restore to the same client
size keeps the buffer, mask, and brushes and restarts elapsed-time bookkeeping.

## Recovery and platform notes

`GetWinFlags` selects floating-point transforms on systems with an 80387 or later;
other systems use the shared integer pipeline. Both use the shared `soft3d` prepare
helpers. Mesh generation and FPS bookkeeping still use floating point, so keep `-FPi`,
which lets the Windows runtime emulate it when necessary; `-FPi87` would require a
coprocessor.

Windows global memory supplies allocations larger than 64 KiB. `SEG86` shares the
segmented pointer model with DOS, and `GDI` selects Windows allocation, startup,
errors, and timing. When a resolution change runs out of memory, the driver releases
its back buffer, mask, projection cache, font, and cached brushes and pens. It retries
them, along with the pattern and color modes, once per scene revision: after the next
successful resolution change, outside any render. A failed retry waits for the
following revision rather than allocating on every paint. Fatal errors stop rendering,
animation, capture, and input before showing a modal dialog, so incomplete state
cannot be reentered.

## Validation

See the [GDI checks](testing.md#windows-gdi). Microsoft C 7, Windows 3.0, and Windows
3.1 must validate the segmented code, loader format and version marking, emulator
dispatch, physical palette mapping, cooperative scheduling of the idle loop, and the
partial-update presentation.
