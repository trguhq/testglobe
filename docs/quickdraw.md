# QuickDraw

## Requirements

A System 7 application built with Apple's 68K MPW C compiler and Macintosh
interfaces. Use the System 7-compatible Universal Interfaces 2.2 headers and Rez
templates, the matching MPW C libraries, and the `C`, `Link`, `Rez`, and `Make` tools.
Set MPW's `CIncludes`, `RIncludes`, `Libraries`, and `CLibraries` to the installed
folders. The SDK files and tools are external dependencies and are not
redistributed here. A [Retro68 cross-build](build.md#linux-hosted-cross-builds) is
also available.

## Build

`Makefile.mpw` is a native MPW Make file; MPW's rule syntax, variables, and shell
differ from Solaris make and NMAKE. It is stored in MacRoman with classic CR line
endings, so do not convert it. Transfer the ASCII C, header, and Rez sources as
Macintosh text, converting their Unix line endings to CR in the build copy (for
example, `unix2mac src/*.c src/*.h src/*.r` before transfer).

In MPW Shell, set the current directory to the repository root and run:

```text
Make -f Makefile.mpw quickdraw > QDBuild
Execute QDBuild
```

MPW Make writes commands rather than running them; the second line runs them. From
`src`, use `Make -f ::Makefile.mpw -d QD_SRC=: quickdraw > QDBuild`, then
`Execute QDBuild`. The output is `TestglobeQD`, a Finder-launchable `APPL` with
creator `Tglo`. Keep its resource fork when moving it to another Mac, by using a
resource-aware copy or a MacBinary or BinHex archive. For native cleanup, run
`Make -f Makefile.mpw clean > QDBuild` and then `Execute QDBuild`.

The build needs the C and header files in `src`, `src/qd.r`, `Makefile.mpw`, and all 13
`data/*.bin` files. The binary files hold Earth palette indices in chunks of at most
16 KiB; transfer them as binary, without newline conversion. Rez embeds them as `EART`
resources with IDs from `src/earthres.h`, keeping large initialized arrays out of the
A5-relative globals. Keep `data` beside `src`: the Rez `read` statements use explicit
`::data:` paths, because native MPW Rez does not apply its `-i` search paths to these
binary reads.

The portable `make quickdraw` target can drive the same native file through the
external [MPW tool/shell bridge](https://github.com/ksherlock/mpw-shell) with a
configured MPW installation. `MPW_EXEC` defaults to the bridge's `mpw-make`; the
target passes `QD_DEFINES` (`-d SEG68 -d DRV_QD -d DRV -d NO_SINF`) and the source
directory, and `MPW_EXEC`, `QD_MAKEFILE`, `QD_SOURCE`, and `QD_DEFINES` can be
overridden. Native MPW builds need no bridge or modern scripting language. The
portable `clean-quickdraw` target and `make clean` remove the QuickDraw objects and
application with `RM`, without the MPW tools.

## Rendering and color

The default globe is key 1, with all five resolutions and no texture mapping. Software
quad batches reuse transformed rows and projected vertices, then clip and cull whole
polygons; pole vertices collapse to triangles. QuickDraw fills each retained polygon
with `PaintPoly`, `FillCPoly`, or `FillPoly`. Batching shares geometry work and does
not add a native multiple-polygon call. QuickDraw also draws the OSD text. Resolution
keys show the watch cursor while the mesh is rebuilt.

The driver opens its window on the highest depth currently configured among the active
displays, without changing display modes. After a drag, and on activation or resume,
it follows the window to the deepest display it overlaps (`GetMaxDevice`); drags may
use the whole gray region, across every monitor. It supports monochrome and Color
QuickDraw: 1-bit output uses ordered luminance patterns, 2-, 4- and 8-bit output maps
Earth RGB colors to the device palette, and 16- and 32-bit output draws RGB directly.

Only 8-bit devices take the Earth palette, as a tolerant palette with exact matching
and its 2- and 4-bit entries inhibited. The luminance-sorted palette would otherwise
fill a 4-bit color table with near-blacks, recoloring the desktop, so 2- and 4-bit
devices (and their GWorlds) keep the standard table and dither to it. Color QuickDraw
receives the original RGB values through `MakeRGBPat` and `FillCPoly`; its native
ditherer can combine four palette colors and reconverts patterns when the device
palette or depth changes, so the Amiga two-color matcher is not used here. In an 8-bit
GWorld built from the Earth table, each index is probed once through `Color2Index` and
`Index2Color`: colors that come back exactly are painted solid with `RGBForeColor` and
`PaintPoly`, skipping the pattern build. The inverse table can hide close neighbours
(the 255 distinct Earth colors share only 125 four-bit cells), so the others keep the
dithered pattern. Presentation keeps `ditherCopy`, because the screen's table follows
other applications and is never known to match the GWorld's. Monochrome keeps its
finer 64-step luminance patterns; the 65 distinct 8 × 8 patterns are built on first
use and cached, and black on white is set once per pass.

One checked polygon handle is reused for every fill, avoiding a recording allocation
per polygon. Once per pass, a temporary allocation checks for 8 KiB of contiguous
headroom for pattern drawing, or 4 KiB for basic fills, and is freed immediately. This
preflight can disable optional color dithering or pause drawing when native scratch
space is scarce, and checked allocation failures release optional resources before
one retry. Repeated failure pauses drawing until input requests another attempt.
QuickDraw's own pattern and fill routines can still raise system errors or silently
skip drawing when their internal allocations fail; these Toolbox calls offer no
general recoverable-allocation guarantee, which the memory reserve below covers.

An optional GWorld provides an offscreen back buffer presented through `CopyBits`,
using ordinary GWorld pixel memory and whatever display acceleration QuickDraw selects.
Color QuickDraw needs 32-bit QuickDraw for it; on System 7, monochrome Macintosh Plus,
SE and Classic systems use a 1-bit basic QuickDraw GWorld. Pixels are reached through
`GetGWorldPixMap` (a basic QuickDraw GWorld has no `portPixMap`; Color QuickDraw before
32-bit QuickDraw 1.2 still uses that field), and `CopyBits` receives both ports'
`portBits`. If allocation or pixel locking fails, drawing falls back to the window,
and the OSD reports the path in use. Display changes rebuild the palette and GWorld.

After a complete frame the loop validates the window, so a grow, drag or activation
draws one frame instead of a second one for its update event. An update with no scene
change copies the frame the GWorld already holds; without a GWorld, or after a change,
the whole frame is drawn. Failed and paused frames do not validate the window.

## Memory

At startup the driver makes four blocks of master pointers before any handle exists,
then keeps a 24 KiB reserve handle. A grow-zone procedure, which switches to the
application's A5 world, empties the reserve for Toolbox allocations that would
otherwise end in a system error. The driver's own checked allocations (polygon,
scratch probes, color table, GWorld and mesh storage) never receive it, so their
recovery still runs. Between frames a spent reserve is taken back after optional
resources are released; if it still does not fit, drawing pauses until a control
retries. A fatal alert frees the reserve first.

Optional resources released by recovery (the palette, pattern, and GWorld) come back
at most once per scene revision, after a successful resolution change or a retry, and
only when `MaxBlock` leaves 32 KiB of headroom beyond them. The small palette and
pattern return first; the GWorld needs room of its own. A resize or display change
allows one more attempt. The projection cache is static and is never released.

## Events and menus

The Apple menu offers About and the desk accessories (Apple Menu Items on System 7);
the File menu offers Quit. When Gestalt reports the Apple Event Manager, the driver
installs open-application, open-documents, print-documents, and quit handlers, and
high-level events go through `AEProcessAppleEvent`. Quit, including from Shut Down,
uses the same normal exit as File > Quit; testglobe has no documents, so open and
print requests are declined. Without the Apple Event Manager nothing changes.

`WaitNextEvent` sleeps one tick only while the window is active and animating or
tracking a drag, 30 ticks when idle, and as long as possible in the background, where
the window is not redrawn except for update events.

## Platform notes

The baseline 68000 sources are `main.c`, `drv.c`, `err.c`, `globe.c`, `drv_qd.c`, and
the integer-only `soft3d.c`, built with `SEG68` and `DRV_QD`. MPW's SANE
floating-point code works without a coprocessor. Only `qd_fpu.c` and the shared
`soft3fp.c` are compiled with `-mc68020 -mc68881`; their pointer-and-integer
interface and separate `QDFPU` code segment keep SANE and hardware floating-point
calling conventions apart. The driver checks for both the CPU and the FPU before
calling them. Do not use the FPU flags for baseline objects.

Linking uses MPW's startup and runtime, interface, standard C, `Math.o`, and
`CSANELib.o` libraries. `QD_RUNTIME` defaults to `{Libraries}Runtime.o`; override it
and `QD_LIBRARIES` for MPW releases with different runtime filenames.

`src/qd.r` requests an 8 MiB preferred and 512 KiB minimum partition, sets the
suspend/resume, high-level-event and 24/32-bit addressing flags, and supplies the
error and About alerts. Higher resolutions and larger GWorlds need a larger partition.
The driver creates its own window and menus. Heap allocation and `TickCount` timing
use `SEG68`; classic Macintosh A5-relative globals and segmented `CODE` resources have
their own memory model. The grow-zone procedure lives in the `QuickDraw` segment,
which is loaded at startup and never unloaded.

The OSD wraps text by adding per-character widths rather than remeasuring growing
prefixes, and caches the help text's line breaks for the current font, size, and width.

Before a fatal alert, the driver unlocks pixels, restores the caller's port and
device, restores the arrow cursor, and frees the memory reserve. Readiness guards
prevent reentrant rendering during setup, errors, or teardown. GWorld presentation
temporarily sets black and white destination colors and restores them afterward,
including after a fallback to direct rendering.

## Downloads

The [prebuilt archive](../downloads/README.md) contains `TestglobeQD.bin`, a MacBinary
file that keeps the resource fork (decode it with a MacBinary-aware utility), and
`TestglobeQD.dsk`, an 800 KiB HFS disk image containing the same application.

## Validation

See the [QuickDraw checks](testing.md#quickdraw). Native MPW builds must validate
code-resource linkage and SANE/68881 dispatch. Physical depth and palette behavior,
resource forks, and acceleration require System 7 hardware or a suitable emulator.
