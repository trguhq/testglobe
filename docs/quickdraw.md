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
not add a native multiple-polygon call. QuickDraw also draws the OSD text.

The driver chooses the highest depth currently configured among the active displays,
without changing display modes. It supports monochrome and Color QuickDraw: 1-bit
output uses ordered luminance patterns, 4- and 8-bit output maps Earth RGB colors to
the device palette, and 16- and 32-bit output draws RGB directly. Color QuickDraw
receives the original RGB values through `MakeRGBPat` and `FillCPoly`; its native
ditherer can combine four palette colors and reconverts patterns when the device
palette or depth changes, so the Amiga two-color matcher is not used here. Monochrome
keeps its finer 64-step luminance patterns.

One checked polygon handle is reused for every fill, avoiding a recording allocation
per polygon. Once per pass, a temporary allocation checks for 8 KiB of contiguous
headroom for pattern drawing, or 4 KiB for basic fills, and is freed immediately. This
preflight can disable optional color dithering or pause drawing when native scratch
space is scarce, and checked allocation failures release optional resources before
one retry. Repeated failure pauses drawing until input requests another attempt.
QuickDraw's own pattern and fill routines can still raise system errors or silently
skip drawing when their internal allocations fail; these Toolbox calls offer no
general recoverable-allocation guarantee.

With 32-bit QuickDraw, an optional GWorld provides an offscreen back buffer presented
through `CopyBits`, using ordinary GWorld pixel memory and whatever display
acceleration QuickDraw selects. If allocation or pixel locking fails, drawing falls
back to the window, and the OSD reports the path in use. Activation, resume, and
completed window drags recheck the device depth and bounds and rebuild display
resources when they change.

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
suspend/resume and 24/32-bit addressing flags, and supplies the error alerts. Higher
resolutions and larger GWorlds need a larger partition. The driver creates its own
window and menus. Heap allocation and `TickCount` timing use `SEG68`; classic
Macintosh A5-relative globals and segmented `CODE` resources have their own memory
model.

Before a fatal alert, the driver releases pending polygons, unlocks pixels, and
restores the caller's port and device. Readiness guards prevent reentrant rendering
during setup, errors, or teardown. GWorld presentation temporarily sets black and
white destination colors and restores them afterward, including after a fallback to
direct rendering.

## Downloads

The [prebuilt archive](../downloads/README.md) contains `TestglobeQD.bin`, a MacBinary
file that keeps the resource fork (decode it with a MacBinary-aware utility), and
`TestglobeQD.dsk`, an 800 KiB HFS disk image containing the same application.

## Validation

See the [QuickDraw checks](testing.md#quickdraw). Native MPW builds must validate
code-resource linkage and SANE/68881 dispatch. Physical depth and palette behavior,
resource forks, and acceleration require System 7 hardware or a suitable emulator.
