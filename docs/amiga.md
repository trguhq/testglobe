# Amiga

This page covers the chipset/RTG software driver (`make amiga`). The PiStorm3D
hardware-accelerated target is described under [Amiga OpenGL](opengl.md#amiga-opengl).

## Requirements

The driver opens a resizable Workbench window on a 68000 or later CPU.

- **Native chipset:** OCS, ECS, or AGA on AmigaOS 1.3 or later, with planar screens of
  1–8 bitplanes.
- **RTG (optional):** CyberGraphX or Picasso96 on AmigaOS 3.0 or later, with 8-bit
  indexed or 15/16/24/32-bit truecolor desktops. Storage size comes from each
  library's bytes-per-pixel attribute.
- **Fullscreen (optional):** AmigaOS 3.0 or later and a display mode that supports
  synchronized screen buffers.

Windowed operation uses the current desktop mode without replacing its palette.

Cross-compilation needs VBCC's m68k-amigaos compiler, vasm, vlink, the `kick13`
target runtime, the Amiga NDK headers, and Python 3.8 or later on the build host.
The SDK and compiler are external dependencies; other targets need neither VBCC nor
the Amiga packer.

## Build

Set `VBCC` to the toolchain installation and put its host tools on `PATH`. Install the
`kick13` target and make the NDK headers available through the target configuration
or `AMI_CPPFLAGS`. For example, from the repository root:

```sh
export VBCC=/path/to/vbcc
export PATH="$VBCC/bin:$PATH"
make amiga AMI_CPPFLAGS=-I/path/to/NDK/Include/include_h
```

The build produces `testglobe-amiga` and its classic Workbench tool icon,
`testglobe-amiga.info`; copy both to the Amiga for icon launch. The icon requests a
32 KiB stack. From the CLI, set the stack first:

```text
Stack 32768
testglobe-amiga
```

The driver checks the available stack before opening its window. When the task's
stack bounds contain the running program, it measures the space left below its own
frame. The 1.3 CLI runs commands on a separate stack without updating those bounds, so
there the CLI's configured `Stack` size is checked instead. Override `AMI_CC`,
`AMI_CONFIG`, `AMI_CFLAGS`, `AMI_020FLAGS`, `AMI_FPUFLAGS`, `AMI_CPPFLAGS`,
`AMI_LDFLAGS`, `AMI_LIBS`, or `AMI_PYTHON` for another toolchain. Keep the baseline
objects at `-cpu=68000 -fpu=0`; `-rmcfg-amiga-softfloat` and `-lmsoft` select IEEE
software math without an FPU or a newer OS math library.

RTG support is built in by default (`AMI_RTG=1`). The RTG libraries are opened only
when present: `cybergraphics.library` V40 or later is preferred, then
`Picasso96API.library` V2 or later. No RTG headers or link libraries are needed, and
the same executable keeps the native chipset path. Build with `make amiga AMI_RTG=0`
to omit RTG support; changing this setting recompiles the driver.

The build packs all 13 canonical `data/*.bin` chunks into an executable `HUNK_DEBUG`
record, so no map files are needed on the Amiga. Keep the packed executable intact:
stripping its debug records removes the maps. Copy it as binary and keep its
executable protection bit. The packer accepts only records that Kickstart 1.3's
`LoadSeg` understands; it rejects `HUNK_RELOC32SHORT` (1020) and `HUNK_DREL32`
(1015), which need Kickstart 2.0, so link with ordinary 32-bit relocations. Whether
`LoadSeg` reads or seeks past the 170.5 KiB map record when loading from floppy has
not been measured.

The program reads its own executable through the Workbench launch lock or the CLI
program path, loading only the selected map in reads of at most 16 KiB. Later
resolution changes read the executable again, so it must stay accessible while the
program runs. This avoids holding all 170.5 KiB of map data in RAM on small systems.

## Rendering and color

The default globe is key 1, with all five resolutions and no texture mapping. Shared
software transforms, projection, clipping, and back-face rejection prepare indexed
quad packets. On indexed screens, `graphics.library`'s `AreaMove`, `AreaDraw`, and
`AreaEnd` fill each retained polygon. Truecolor RTG drawing converts the polygons to
horizontal spans and submits RGB fills through CyberGraphX's `FillPixelArray` or
Picasso96's `p96RectFill`, which the RTG driver can accelerate. Collapsed pole patches
become triangles. Native `Text` draws the wrapped OSD, including the chipset or RTG
backend, CPU and math, color depth, and buffering.

### Palette and dithering

On indexed Workbench screens, the driver reads the desktop palette and maps Earth
indices to available pens at every supported depth, from 2 to 256 colors. Ordered
two-pen patterns approximate missing colors by comparing their mixed RGB values.
Palettes of up to 16 entries use the closest pair and a 1/16 mixture; larger palettes
use two bounded searches to improve the mixture without a costly full pair search.
Exact matches stay solid. Mappings are cached until the desktop palette changes, and
precomputed patterns avoid per-polygon work. Workbench and fullscreen keep separate
mappings, each rebuilt only when the colors read back from its screen change, so
switching with `F` does not repeat the search. A pen, draw mode, or pattern is set only
when it differs from the previous polygon's, and `AreaEnd` reinitializes the vector
table for the next polygon.

`D` toggles dithering, which starts enabled; with it off, each polygon uses its
nearest solid palette color. Both mappings stay cached, so toggling neither rebuilds
the globe nor allocates memory. The OSD shows `dither on`, `dither off`, or, on
truecolor RTG screens, `dither n/a`.

The driver does not change desktop or pointer colors. OCS and ECS use RGB12 palette
precision, and AGA-capable OS versions expose RGB24 entries. Display depth is
independent of CPU/FPU capability. HAM, EHB, and dual-playfield modes are rejected,
because they lack ordinary indexed-pixel semantics. Truecolor RTG uses the Earth
palette's RGB values directly, and the RTG library converts the pixel format. OSD
text uses available desktop pens.

### Window buffering and limits

In windowed mode, an optional offscreen bitmap is presented through
`BltBitMapRastPort`. Native screens use planar Chip RAM; RTG screens use a
display-compatible bitmap from `AllocBitMap`. Indexed area fills also need a Chip RAM
scratch raster, which truecolor RTG fills do not; geometry and other working data use
ordinary memory. If the back buffer cannot be allocated, the driver draws directly; if
the indexed scratch raster is missing, filled rendering pauses while controls stay
available to retry. When memory recovery releases the back buffer and projection
cache, they are reacquired at the next key press, resize, or frame after the scene
changes, with at most one attempt per scene revision. Buffering hides each polygon's
construction but does not guarantee tear-free presentation. The window copy does not
wait for the blitter: `graphics.library` and the RTG libraries order later blits
behind it, and the driver waits only before freeing a bitmap or swapping fullscreen
buffers. Native input handles dragging, keys, close, activation, refresh, and
resizing. The projection follows the client area and corrects the display mode's pixel
proportions; RTG uses square pixels.

OCS, and hardware that older OS versions cannot identify, use conservative window
limits of 992 × 1024 pixels and need a Workbench bitmap stride of at most 128 bytes
per plane row; oversized transfers are rejected before drawing. Confirmed ECS Agnus or
AGA Alice hardware allows larger windows within the coordinate limits. These chipset
limits do not apply to RTG bitmaps, but every path keeps signed-word coordinate
bounds.

## Fullscreen

`F` switches between the Workbench window and a private fullscreen screen; in this
driver `P` toggles FPS instead. `Esc` exits from either mode. Switching keeps the
globe resolution, orientation, animation, dithering, OSD, and FPS settings, and
returning to Workbench restores the window's position and size.

Fullscreen draws the globe and OSD into an inactive display bitmap, waits for the
blitter, then presents it with `ChangeScreenBuffer`. Safe-to-write and
display-complete messages control buffer reuse, so no bitplanes are copied into the
displayed frame. A slow render or blocked swap leaves the previous complete frame
visible. The OS keeps running, including Intuition input and reads of Earth data from
the executable.

Native fullscreen uses low resolution on the Workbench screen's monitor, so a 31 kHz
monitor driver from `DEVS:Monitors` keeps a visible mode; on PAL or NTSC this is 320 ×
256 or 320 × 200. If that monitor has no available low-resolution mode, `BestModeIDA`
picks the nearest nominal 320 × 200/256 mode on it without HAM, extra-half-brite, or
dual playfield, and then the default monitor's low resolution is tried. The screen
takes its size from the mode's nominal `DimensionInfo` and uses 32 colors on OCS/ECS
or 256 on AGA, within the mode's maximum depth. AGA retries with 32 colors if the
larger buffers cannot be allocated. Its private palette is based on the Earth colors,
with pointer and OSD colors accounted for, and Workbench's palette is preserved. RTG
fullscreen stays on the current graphics backend and needs compatible screen buffers.
Older OS versions and unsupported modes stay windowed. If allocation fails, windowed
operation is kept or restored; fullscreen never falls back to drawing directly into
the displayed bitmap.

Avoiding the presentation copy saves bandwidth, but polygon filling and software
transforms still set the rendering speed. At 320 × 256, two planar display buffers plus
the area-fill scratch raster need about 110 KiB for 32 colors or 170 KiB for 256
colors, besides Workbench, application data, and OS structures. These buffers are
required while fullscreen; memory recovery may release optional transform caches but
never a displayed or pending frame.

## Platform notes

The build defines `FLAT`, `DRV_AMI`, `DRV`, `NO_SINF`, and `AMI_RTG`. Amiga uses
ordinary data pointers; the project's `SEG68` option is specific to the Macintosh
memory model. Baseline code uses 68000 instructions and IEEE software floating point,
and Exec's CPU/FPU flags select separately compiled enhanced math routines:

| Available hardware | Transform and projection | Clipping and back-face rejection |
| --- | --- | --- |
| 68000/68010 | Shared fixed point | Shared fixed point |
| 68020 or later without FPU | 68020 integer object | 68020 integer object |
| 68020 or later with a usable 68881/68882 or integrated FPU | Shared hardware floating point | 68020 integer object |

The enhanced integer object compiles the same fixed-point transform, projection,
clipping, and back-face code from `soft3d.c` with `-cpu=68020`, so results match the
baseline exactly; the FPU object uses `-cpu=68020 -fpu=68881`, and all other objects
keep the baseline options. The fixed-point projection cache applies the display's
pixel aspect once per cached vertex; the FPU path scales each projected corner, with
the aspect's overflow bounds precomputed when the display mode changes.
Pointer-and-integer interfaces preserve the baseline calling boundary. The FPU helper
uses only basic arithmetic and bounded IEEE conversion, avoiding transcendental and
integer-conversion opcodes that later FPUs lack. Mesh construction and shared
bookkeeping still use the baseline software floating-point runtime.

Animation and FPS timing read `timer.device`: the free-running E-clock through
`ReadEClock` on AmigaOS 2.0 and later, or `TR_GETSYSTIME` through `DoIO` on 1.3.
Intuition's `CurrentTime`, which advances only with input and vertical-blank updates,
remains the fallback if the device cannot be opened. The device is opened at startup
and closed at exit; the [PiStorm3D target](opengl.md#amiga-opengl) shares the same
clock helper, `src/ami_time.c`.

## Validation

See the [Amiga checks](testing.md#amiga). A cross-build was checked with VBCC 0.9h
(m68k generator 1.15), vasm 1.9d, vlink 0.17a, NDK 47.5 headers, and the `kick13`
runtime. Host call recorders and cross-builds do not establish graphics output,
Workbench behavior, or performance on real machines. Native testing is still needed
for 68000 and accelerated CPUs, OCS/ECS/AGA screens, CyberGraphX/Picasso96 desktops,
Chip RAM pressure, palette patterns, and blitter/window synchronization. Fullscreen
also needs native checks of buffer-swap timing, screen switching, and pointer colors;
host tests cannot establish tear-free scanout on an RTG driver.
