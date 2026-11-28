# Matrox LibShell

## Requirements

MS-DOS, Microsoft C 7.0 with its huge-model runtime and NMAKE, and the matching Matrox
LibShell C binding. The driver assumes a 640 × 480, 256-color PG-641-compatible
display; initialize the board and its firmware as the SDK describes. The supplied
`LSCL.LIB` uses 286 instructions (`ENTER`/`LEAVE`), so both builds stop with an error
on an 8086-class CPU before calling `lsinit`. A Microsoft-compatible DOS mouse driver
enables dragging; the keyboard controls work without one. A
[cross-build](build.md#linux-hosted-cross-builds) with OpenWatcom is also available.

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

Both modes use the other drivers' perspective camera: a 45° field of view across the
screen height, with the eye 3,000 units (three globe radii) from the globe's center.

By default, LibShell transforms, projects, clips, and fills world-coordinate polygons
through `v3arwplygn` (`V3ARWPLYGN`, winding-number fill). The driver sets a 45°
perspective with `v3gpsproj(45)` and the viewing distance with `v3gpsdist(3000)`;
LibShell's default angle is 60°, and 0 selects an orthogonal projection. The
*Command Reference* gives no projection formula. It does not say whether the viewing
angle spans the window's width or its height, or whether the window lies on the view
plane through the reference point. The driver therefore maps a square window, ±1,242.6
units (3,000 × tan 22.5°), to a centered 480 × 480 viewport, which gives the same
image under each of those readings. Because `v2gpsvwp` also resets the clipping
rectangle to the viewport, `krhwsclipr` then restores the full screen for the OSD.

Press `S` to switch to host software transforms. This path uses LibShell's
integer-degree rotations (its `ANGLE` type) and the same camera. The CPU transforms,
projects, clips, and culls bounded packets of up to 64 quads (two latitude rows of at
most 65 vertices) with the shared perspective pipeline that TIGA, GDI, and QuickDraw
use, then sends screen-coordinate polygons through `scarwplygn` (`SCARWPLYGN`).
LibShell still fills them with the same LUT. Each packet's lower row becomes the next
packet's upper row, and a projection cache projects each vertex once, so every mesh
vertex is transformed and projected once per frame (561 points at key 1 rather than
2,048 quad corners). The packets need about 4.8 KB of bounded buffers (2.1 KB from the
shared traversal, 2.7 KB of driver points and projection cache), not a second mesh.
Native LibShell 3D keeps sending each quad's world vertices. Press `S` again to return
to LibShell 3D.

## Platform notes

The *LIB Shell Command Reference Manual* documents no depth buffer or back-face
control, so the host rejects back-facing polygons itself: a polygon is drawn when its
normal points toward the eye's position, seen from one of its vertices. Without an
x87, native mode decides most facings with an integer object-space test instead of
emulated floating point (99.7% at key 1, 98.1% at key 2 in the recorder). A proven
error bound covers the fixed-point rounding and the float test's own rounding; facings
within it, near the silhouette or at thin pole caps, fall back to the float test, so
the submitted polygons are exactly those of the float test. Its speed on real hardware
has not been measured. DOS builds use normalized huge pointers, `_halloc`/`_hfree`,
long mesh counts, and DOS timing.

Matrox and TIGA share `drv_cpu.c` to detect 8086-class, 286, 386, and 486-or-later
CPUs, and one selection in `drv.c` (`drv_soft3d_select()`) picks their host kernel at
startup. A usable x87 selects floating-point host transforms; the presence probe works
with 8087/287/387-compatible instructions but does not identify the exact model.
Without an x87, 386-or-later CPUs use the separate 386 integer kernel and older CPUs
the baseline integer code. `NO87=1` forces integer transforms. `/FPi` keeps runtime
emulation for other floating-point work, including mesh generation.

LibShell reports each error as an 8-bit value in its error register and waits until
that value is read before reporting the next, so a shell with unread errors appears
to hang. The driver therefore services the register while waiting for `LSINQ`
replies between bounded command packets, keeping at most 228 bytes in flight so even
a 256-byte command FIFO cannot fill while the shell is stalled. One reply confirms up
to four polygon fills, and one the six per-frame model/view commands; a key-1 frame
needs about 70 replies instead of about 270. The driver resends `v3mdorg` and
`v3vwrpt` each frame, because the manual does not say that `v3mdid` and `v3vwid`
preserve the origin and reference point. A fill color equal to the previous one is
not resent; the driver forgets the color after a clear, the OSD and any error.
Diagnostics name the operation and error bits.

At startup the driver first discards errors and Read FIFO words left by a previous
program, waiting for a short quiet interval in case a stalled shell resumes, and
prints a warning on stderr if it discarded anything. A board that keeps replying past
a word limit or the deadline is abandoned.

Reply polling reads the board before the clock, so ready words cost no clock call. A
ten-second deadline covers all the waiting of one reply; only forward `clock()` steps
count, so a backward step (DOS midnight) neither ends a wait early nor restarts it.
Short malformed replies are drained, while oversized replies or timeouts abandon
communication.

A recoverable error during a native geometry packet can retry the frame with host
transforms once a valid error reply has been drained; this recovers only if the 2D
color and fill commands work. Communication failures and errors in that shared
drawing path cannot be fixed by changing the transform mode.

Cleanup restores the palette, its LUT selection, the plane mask read with `LSINQ` 88
(a shell without that index keeps its mask) and the error-reporting policy while
communication still works, and sends no more board commands after communication is
abandoned. Ctrl-C and Ctrl-Break set a flag through INT 23h instead of ending the
program inside DOS; the event loop then exits through the same cleanup as Escape, so
board emulation is restored. From DOS 3.1, critical disk errors (INT 24h) fail the
DOS call instead of prompting on an invisible screen.

## Validation

See the [Matrox SDK checks](testing.md#matrox-libshell). Native validation requires
Microsoft C 7, `LSCL.LIB`, and the board. Polling assumes that `iogeterror()` does not
block and that a positive `rdfifocnt()` allows a complete `iogetw()` without waiting.
Neither the supplied headers nor the Command Reference establish the FIFO count's
units or whether a word is available atomically; verify these against the installed
binding and board. The 228-byte packet budget assumes the manual's command formats
and a command FIFO of at least 256 bytes; the startup quiet interval, the INT 23h
exit path and the speed of grouped checkpoints and integer culling also need the
board to confirm. So does the native perspective: if the globe's size or position
changes when `S` switches modes, LibShell reads the viewing angle or window
differently from the assumptions above.
