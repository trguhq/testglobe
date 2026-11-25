# TIGA

## Requirements

Real-mode MS-DOS, Microsoft C 7.0 with its huge-model runtime and NMAKE, and a TIGA 2.x
installation for the board. This is a full-screen DOS application. The vendor's
`TIGACD.EXE` communication driver, graphics manager, and standard `extprims.rlm`
library must be installed and configured. The driver needs a 640 × 480 mode with
eight valid pixel bits and a programmable 256-color palette; it queries mode numbers
rather than assuming a particular board. A Microsoft-compatible DOS mouse driver
enables dragging; keyboard input works without one. A
[cross-build](build.md#linux-hosted-cross-builds) with OpenWatcom is also available.

The interface follows TI's
[TIGA Interface User's Guide](https://github.com/tms34010guy/tms34010-sdk/blob/main/Docs/SPVU015C%20TIGA%20Interface%20Users%20Guide%20%281990-09%29.pdf)
(September 1990).

## Build

Install the SDK's `include` and `libs` directories under `TIGA` at the repository
root, or pass its DOS installation path. Put Microsoft C and the TI TMS340
code-generation tools on `PATH`, then run:

```text
nmake tiga TGA_ROOT=C:\TIGA
```

Distribute only `testglob.exe`: it contains both Earth maps and the TI transform
module. The host links `ai.lib`. The TI tools build `tga_gsp.rlm` from
`src/tga_gsp.c` and `src/tga_ext.asm`, and `src/tga_pack.c` then appends it to the
host executable. `tga_gsp.rlm`, `tga_host.exe`, and the packer are build
intermediates. The appended bytes lie outside the DOS load image, so software mode
keeps no extra copy of the module in memory.

The module uses TMS34010 instructions, which the TMS34020 also supports, and needs no
floating-point coprocessor or TI math runtime. `src/tga_cmd.h` defines its bounded
command and embedded-file formats. To build a software-only executable without the TI
tools or the module:

```text
nmake tiga TGA_ROOT=C:\TIGA TGA_MODULE=
```

A software-only build still renders through TIGA; pressing `S` leaves software 3D on.
`nmake tiga-module` builds the module alone, and setting `TGA_MODULE` to a prebuilt
RLM embeds it without rebuilding the TI code.

Override `TGA_CC`, `TGA_CFLAGS`, `TGA_386FLAGS`, `TGA_CPPFLAGS`, `TGA_LIB`, and
`TGA_LDFLAGS` for the host installation, and `TGA_GSP_CC`, `TGA_GSP_LINK`,
`TGA_GSP_CFLAGS`, `TGA_GSP_CPPFLAGS`, and `TGA_GSP_LDFLAGS` for the TI tools. Keep
`-AH` and the separate baseline/386 object settings. The default `TGA_ROOT` is `TIGA`
in the root Makefile and `..\TIGA` in `src/Makefile`. The executable embeds only the
32- and 64-wide Earth maps.

## Module loading

When `S` first selects TI transforms, the driver reads the embedded module from its
own executable and streams it through a bounded buffer into a uniquely named
temporary file, trying `TEMP`, `TMP`, and then the current directory, because TIGA's
`install_rlm()` takes a filename. The file is normally deleted right after loading;
if deletion fails, its path is kept for another attempt on `S` or at exit. The
resident module is reused until it is released or the application exits. Unused
export symbols are discarded at installation to save TI memory. Optional release uses
TIGA's documented `flush_module()`; if that fails, the module identifier is kept for
reuse or another attempt while host transforms continue.

If the executable cannot be reopened, the module is missing or corrupt, temporary
storage is unavailable, or TIGA rejects the module, rendering continues with
`soft3d on` and an OSD notice; press `S` to retry. The application does not look for a
separate `tga_gsp.rlm`. The vendor graphics driver, graphics manager, and
`extprims.rlm` remain part of the installed TIGA environment.

## Rendering and controls

The default globe is key 1. Only keys 1 and 2 are available, and there is no texture
mapping. Both transform modes fill indexed polygons with TIGA's `fill_convex()`, and
use the general polygon fill for results that become nonconvex after rounding. Native
bitmap text draws the OSD. All 256 palette entries come from `earth_cmap`, and the
TIGA runtime adapts them to the board's DAC precision. Quads use the shared globe
traversal, with collapsed pole patches drawn as triangles.

`S` toggles between:

- **`soft3d on` (default):** host rotation, projection, clipping, and culling, using
  the shared software pipeline. An available x87 selects floating-point transforms;
  otherwise the 286-compatible or separately compiled 386 integer path is used.
- **`soft3d off (TI)`:** the downloaded module performs fixed-point rotation, view
  translation, and perspective projection on the TMS340 processor. Matrix
  preparation, clipping, culling, and polygon submission stay on the host, and
  polygons crossing the depth limits use the shared host path. Floating-point host
  transforms and fixed-point TI transforms can differ slightly at rounded edges.

Insufficient command capacity or a rejected transform packet restores software 3D
with an OSD notice; press `S` to retry. Host/GSP packets contain bounded 32-bit values,
not host pointers or compiler-dependent structures.

A mode with two display pages is preferred. Completed frames use synchronized page
flips, and the previous page is not reused until its flip completes. A single-page
mode still works but can flicker, and the OSD reports the buffering in use.

## CPU and platform notes

The shared DOS probe distinguishes 8086-class, 286, 386, and 486-or-later CPUs without
CPUID; the program stays a 16-bit real-mode executable. Baseline objects use `/G0`.
Only the guarded 386 transform object uses `/G3`, and 486-or-later CPUs use that same
path. `/FPi` keeps the Microsoft emulation runtime for startup, mesh generation, and
other floating-point work when there is no coprocessor.

The x87 probe checks the BIOS equipment flag and makes bounded no-wait status reads
that work on 8087, 287, and 387 hardware; it reports a usable x87, not the exact model.
An unreported or unavailable coprocessor selects integer transforms, and `NO87=1`
forces that choice. CPU/FPU and buffering details follow `soft3d` in the top OSD line.
The host accesses no board-specific addresses or registers.

## Validation

See the [TIGA checks](testing.md#tiga), which include executable packing,
corrupt-module rejection, temporary-file failures, and cleanup. The module was
compiled and linked with TI TMS340 tools 5.01 under DOSBox, separately from the host
tests. Microsoft C 7 host compilation, the segmented ABI and communication callbacks,
module loading, rendering, and page-flip behavior still need native validation.
