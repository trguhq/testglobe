# Windows GDI

## Requirements

Windows 3.1 in a 256-color display mode; Microsoft C/C++ 7.0 with the Windows
emulation runtime (`LLIBCEW.LIB`); and the Windows 3.1 SDK headers, `LIBW.LIB`,
16-bit linker, and resource compiler. The target is a native 16-bit application. A
[cross-build](build.md#linux-hosted-cross-builds) with OpenWatcom is also available,
and a prebuilt copy is in [downloads](../downloads/README.md).

## Build

Configure `PATH`, `INCLUDE`, and `LIB`, then run:

```text
nmake gdi
```

Run `testglob.exe` inside Windows 3.1. Override `GDI_CC`, `GDI_LINK`, `GDI_RC`,
`GDI_CFLAGS`, `GDI_CPPFLAGS`, `GDI_LIBS`, `GDI_LDFLAGS`, `GDI_RCFLAGS`, and `GDI_DATA`
as needed. Keep `-AH -G2 -Gw -Zp1 -FPi` in `GDI_CFLAGS` to preserve the memory model,
Windows ABI, and optional FPU support. `GDI_DATA` defaults to `data` at the root and
`..\data` inside `src`.

The `.def` file exports the window procedure and sets the segment attributes, an
8 KiB local heap, and a 16 KiB stack. `RC -r -fo` compiles `src/gdi.rc` and all 13 map
chunks into `g_earth.res`, and `RC -t g_earth.res testglob.exe` attaches it.
Discardable `RCDATA` chunks avoid large static arrays. As a huge-model application,
only one instance can run at a time.

## Rendering and color

The default globe is key 1, with all five resolutions and no texture mapping. The
window uses the display's 8-bit depth. A 256-entry logical Earth palette is realized
on activation, and Windows keeps its reserved system colors. An 8-bit compatible
bitmap and memory DC normally provide the back buffer; if they cannot be allocated,
the driver paints directly into the window's paint DC.

Colors that the palette realizes exactly use solid brushes. For the others, the
driver uses the same two-color matcher as Amiga, choosing from the colors Windows
actually realized, and draws with a palette-indexed pattern brush. Palette changes
refresh the matches and brush cache. If palette queries or pattern allocation fail,
drawing falls back to a solid palette brush.

Software transforms, perspective projection, clipping, and culling operate on whole
quads. Bounded packets reuse adjoining transformed rows and projected corners, and the
retained polygons are grouped by palette index into `PolyPolygon` calls of at most 64
polygons. Every contour repeats its first vertex, as Win16 `PolyPolygon` requires.
Pole vertices collapse to triangles. Visibility relies on the convex globe rather than
a depth buffer. Native Windows messages handle input, resizing, palette changes, and
closing.

A reusable monochrome mask covers both the polygon fills and one-pixel outlines, and
the selected palette brush is applied through it. This keeps dithering continuous
across shared edges and avoids Win16's incomplete coverage with `NULL_PEN`. The mask
is allocated on demand and adds about 9.4 KiB at 320 × 240 or 37.5 KiB at 640 × 480,
plus GDI overhead. Without it, matching solid fills and outlines still close every
facet; if a pen cannot be allocated, stock black or white is used. Optional-memory
recovery releases the mask, and resizing allows another allocation attempt.

A failed `PolyPolygon` call falls back to individual `Polygon` calls. Brush
allocation can reclaim cached brushes or use a stock color. If the animation timer
cannot be created, manual rotation and keyboard input still work.

## Platform notes

`GetWinFlags` selects floating-point transforms on systems with an 80387 or later;
other systems use the shared integer pipeline. Mesh generation and FPS bookkeeping
still use floating point, so keep `-FPi`, which lets the Windows runtime emulate it
when necessary; `-FPi87` would require a coprocessor.

Windows global memory supplies allocations larger than 64 KiB. `SEG86` shares the
segmented pointer model with DOS, and `GDI` selects Windows allocation, startup,
errors, and timing. Fatal errors stop rendering, animation, capture, and input before
showing a modal dialog, so incomplete state cannot be reentered.

## Validation

See the [GDI checks](testing.md#windows-gdi). Microsoft C 7 and Windows 3.1 must
validate the segmented code, loader format, emulator dispatch, and physical palette
mapping.
