# Matrox LibShell

## Requirements

MS-DOS, Microsoft C 7.0 with its huge-model runtime and NMAKE, and the matching Matrox
LibShell C binding. The driver assumes a 640 × 480, 256-color PG-641-compatible
display; initialize the board and its firmware as the SDK describes. The supplied
`LSCL.LIB` uses 286 instructions (`ENTER`/`LEAVE`), so both builds stop with an error
on an 8086-class CPU before calling `lsinit`. A
Microsoft-compatible DOS mouse driver enables dragging; the keyboard controls work
without one. A [cross-build](build.md#linux-hosted-cross-builds) with OpenWatcom is
also available.

## Build

Place the SDK at the repository root:

```text
XGX1/HEADERS/DEF.H
XGX1/HEADERS/LIBFN.H
XGX1/HEADERS/IOLIB.H
libsv5/lscl.lib
```

With the compiler and linker on `PATH`, run:

```text
nmake matrox
```

The output is `testglob.exe`. For an SDK elsewhere, set `XGX_ROOT`, for example
`XGX_ROOT=C:\MATROX`; the default is `.` in the root Makefile and `..` in
`src/Makefile`. Override `MTX_CC`, `MTX_CFLAGS`, `MTX_CPPFLAGS`, `MTX_386FLAGS`,
`MTX_LIB`, and `MTX_LDFLAGS` as needed, keeping `-AH -G0 -FPi` on the baseline objects;
only the guarded `m_386.obj` kernel uses `-G3`. When linking a LibShell binding built
for 8086 instructions, add `-DMTX_8086_BINDING` to `MTX_CPPFLAGS` (keeping its
`-I$(XGX_ROOT)`) to skip the 286 check. `MTX_HOTFLAGS` (`-Gs`) removes stack
probes from the per-vertex and per-polygon objects, none of which recurse; the
OpenWatcom cross-build uses `-s` for the same files. The DOS rules use backslash paths,
8.3 generated names, and response files to stay within command-line limits.

## Rendering and color

The default globe is key 1. Only keys 1 and 2 are available, and there is no texture
mapping. Patches are quads, with coincident pole vertices collapsed to triangles. All
256 LUT entries come from `earth_cmap`, and polygons use the original palette indices.

By default, LibShell transforms, projects, clips, and fills world-coordinate polygons
through `v3arwplygn` (`V3ARWPLYGN`, winding-number fill). The driver selects
LibShell's orthographic projection with `v3gpsproj(0)`; LibShell's default is a 60°
perspective view.

Press `S` to switch to host software transforms. This path keeps LibShell's
orthographic world window, integer-degree rotations (LibShell's `ANGLE` type), and
viewport orientation. The CPU transforms, projects, clips, and culls bounded polygon
packets, then sends screen-coordinate polygons through `scarwplygn` (`SCARWPLYGN`).
LibShell still fills them with the same LUT. The driver keeps the quad traversal and
allocates no second mesh. Press `S` again to return to LibShell 3D.

## Platform notes

The *LIB Shell Command Reference Manual* documents no depth buffer or back-face
control, so the host rejects back-facing polygons itself. DOS builds use normalized
huge pointers, `_halloc`/`_hfree`, long mesh counts, and DOS timing.

Matrox and TIGA share `drv_cpu.c` to detect 8086-class, 286, 386, and 486-or-later
CPUs. A usable x87 selects floating-point host transforms; the presence probe works
with 8087/287/387-compatible instructions but does not identify the exact model.
Without an x87, 386-or-later CPUs use the separate 386 integer kernel and older CPUs
the baseline integer code. `NO87=1` forces integer transforms. `/FPi` keeps runtime
emulation for other floating-point work, including mesh generation.

LibShell reports each error as an 8-bit value in its error register and waits until
that value is read before reporting the next, so a shell with unread errors appears
to hang. The driver therefore services the register while waiting for `LSINQ`
replies between bounded command packets. Diagnostics name the operation and error
bits. At startup the driver first discards errors and Read FIFO words left by a
previous program, waiting for a short quiet interval in case a stalled shell
resumes, and prints a warning on stderr if it discarded anything; a board that
keeps replying past a word limit or the deadline is abandoned.

Reply polling reads the board before the clock, so ready words cost no clock call. A
ten-second deadline covers all the waiting of one reply; only forward `clock()` steps
count, so a backward step (DOS midnight) neither ends a wait early nor restarts it.
Short malformed replies are drained, while oversized replies or timeouts abandon
communication. Cleanup restores the palette, its LUT selection, the plane mask read
with `LSINQ` 88 (a shell without that index keeps its mask) and the error-reporting
policy while communication still works, and sends no more board commands after
communication is abandoned. Ctrl-C and Ctrl-Break set a flag through INT 23h instead
of ending the program inside DOS; the event loop then exits through the same cleanup
as Escape, so board emulation is restored. Critical disk errors (INT 24h) fail the
DOS call instead of prompting on an invisible screen. A recoverable error during a native
geometry packet can retry the frame with host transforms once a valid error reply has
been drained; this recovers only if the 2D color and fill commands work.
Communication failures and errors in that shared drawing path cannot be fixed by
changing the transform mode.

## Validation

See the [Matrox SDK checks](testing.md#matrox-libshell). Native validation requires
Microsoft C 7, `LSCL.LIB`, and the board. Polling assumes that `iogeterror()` does not
block and that a positive `rdfifocnt()` allows a complete `iogetw()` without waiting.
Neither the supplied headers nor the Command Reference establish the FIFO count's
units or whether a word is available atomically; verify these against the installed
binding and board.
