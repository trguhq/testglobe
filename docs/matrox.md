# Matrox LibShell and PGC

## Requirements

MS-DOS on an 8086 or later, Microsoft C 7.0 with its huge-model runtime and NMAKE,
and one of these boards in a 640 × 480, 256-color mode:

- a Matrox PG Series 1 board running the LIB Shell. Download the LIB Shell to the
  board and initialize it as the board's software describes; the driver checks that
  the board answers and does not load anything;
- a board compatible with the IBM Professional Graphics Controller (PGC), such as the
  IBM PGC itself or a Matrox PG-640A, with its communication area at C6000h or, for a
  PG-640A set to its alternate address, C6400h.

No Matrox SDK is needed: `src/mtx_ls.c` implements the LibShell calls the driver
uses directly on the board, and `src/mtx_pgc.c` sends PGC commands, as described
under [Board interface](#board-interface). A Microsoft-compatible DOS mouse driver
enables dragging; the keyboard controls work without one. A
[cross-build](build.md#linux-hosted-cross-builds) with OpenWatcom is also available.

## Board detection

The driver looks for a LIB Shell board first, at segment C600h or at the first
segment listed in the `PG1281` environment variable, as Matrox's own binding does
(for example `set PG1281=0xCE00`). If none answers, it looks for a PGC-compatible
board at C6000h and then C6400h. The LIB Shell test goes first because it is the
less intrusive one: it touches only four words at offsets 318h–31Eh, which lie in the
PGC communication area's unused bytes, and puts them back if no board answers. The
PGC test changes a flag in the communication area and sends commands. If neither
board answers, the driver names both addresses and stops before drawing anything.

A PGC-compatible board is treated as a PG-640A when the board-type byte at offset
3F8h is 2 and the board answers `FLAGRD 30`, a flag only the PG-640A manual
documents. An IBM PGC sends no reply to it, so the test is safe on either board. The
status line shows `LibShell` or `PGC` for the board in use.

## Build

With the compiler and linker on `PATH`, run:

```text
nmake matrox
```

The output is `testglob.exe`, which drives either kind of board. It uses no Matrox
library or headers. Override `MTX_CC`, `MTX_CFLAGS`, `MTX_CPPFLAGS`, `MTX_386FLAGS`,
and `MTX_LDFLAGS` as needed, keeping `-AH -G0 -FPi` on the baseline objects; only the
guarded `m_386.obj` kernel uses `-G3`. `MTX_HOTFLAGS` (`-Gs`) removes stack probes
from the per-vertex, per-polygon and per-word objects, none of which recurse; the
OpenWatcom cross-build uses `-s` for the same files. The DOS rules use backslash
paths, 8.3 generated names, and response files to stay within command-line limits.

## Rendering and color

The default globe is key 1. Only keys 1 and 2 are available, and there is no texture
mapping. Patches are quads, with coincident pole vertices collapsed to triangles. All
256 LUT entries come from `earth_cmap`, and polygons use the original palette indices.
A LIB Shell board and a PG-640A (through `LUTX`) keep all eight bits of each
component. The IBM PGC's `LUT` command takes four bits per component, so it receives
each component's high four bits: 16 levels of each primary.

Both modes use the other drivers' perspective camera: a 45° field of view across the
screen height, with the eye 3,000 units (three globe radii) from the globe's center.

By default, LibShell transforms, projects, clips, and fills world-coordinate polygons
through `v3arwplygn` (`V3ARWPLYGN`, winding-number fill), with the viewing distance
set by `v3gpsdist(3000)` and the angle by `v3gpsproj`; 0 would select an orthogonal
projection. The LibShell *Command Reference* gives no projection formula, but these
are the PGC's `DISTAN` and `PROJCT`, with the same defaults, and the IBM PGC and
Matrox PG-640A manuals define it: the viewing angle spans the window's diagonal, so
the screen scale is (window diagonal ÷ 2) ÷ tan(angle ÷ 2) × (viewport ÷ window), and
the window's size cancels. With a window of the viewport's 639:479 aspect, the
shared camera's 45° vertical field needs a 69.15° angle; LibShell takes whole
degrees, so the driver sends 69, which draws the globe 0.27% (about a pixel) larger
than host transforms do.

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

A PGC-compatible board runs the same two modes with the PGC commands that LibShell's
follow. Native frames set the 3D window each frame, then `MDIDEN`, `MDORG`, `MDROTZ`,
`VWIDEN`, `VWRPT` and `VWROTX`, and fill world-coordinate polygons with `POLY3`, with
`PRMFIL 1` selecting filled primitives, `DISTAN 3000` and `PROJCT 69`. The IBM manual
gives `PROJCT` a one-byte angle and the PG-640A manual a two-byte integer; the driver
sends the two-byte form only to a board it identified as a PG-640A. With `S`,
host-projected polygons go through `POLY` in a pixel window, whose y axis counts up
from the bottom of the screen. The OSD uses the same pixel window, with `MOVE` and
`TEXT` at the PGC's default 8-pixel character spacing. `TEXT` delimits its string with
double quotes, so any in the OSD are sent as single quotes.

## Board interface

The LIB Shell runs on the board's TMS34010 graphics processor. The host reaches it
through a 1 KB window at the board's segment. The window begins with the same
communication area as the IBM PGC; LibShell's registers follow it: the processor's
host-interface address, data and control words at offsets 318h–31Eh, the FIFO data
entry at 320h, and the 8-bit ERROR register at 3B0h. Matrox's library source gives
the command and reply FIFOs 4,096 words each. Their counts, and a status word whose
bit 9 marks an unread error, are read through the host interface. A command is its opcode word and parameter
words, in the formats the *LIB Shell Command Reference* gives; a FIXPT value is sent
as 16.16, fraction word first, and vertex coordinates are converted from their IEEE
bits without floating-point arithmetic. At startup the binding halts the processor
briefly to check that two of its scratch registers keep test patterns, as Matrox's
`lsinit` does; if they do not, the driver looks for a PGC-compatible board instead.
Matrox's `iogeterror()` first waits for the command FIFO to drain, sending a no-op
each time, so a shell stalled on two unread errors can hang it; `src/mtx_ls.c` reads
the error register without waiting.

A PGC-compatible board takes commands through its 1 KB communication area: a
256-byte command FIFO at offset 0, a readback FIFO at 100h and an error FIFO at 200h,
each with a one-byte write and read pointer from 300h, the reset flags at 306h–307h
and the error-reporting flag at 308h. The driver switches the board to hexadecimal
commands with `CX`, sends each command in the form the IBM *Technical Reference*
gives (a PG-640A gets its own form where its manual differs, as for `PROJCT` and
`LUTX`), and keeps track of the board's read pointer, so it waits only when the FIFO
is full. Coordinates are Reals, 16.16 with the integer word first; vertices are
converted from their IEEE bits without floating-point arithmetic.

To find a board, `src/mtx_pgc.c` checks that the error-reporting flag holds two test
values, discards replies and errors left by a previous program, sends `CX` and waits
up to two seconds for the answer to `FLAGRD 4`. A board that fails is left with its
flag restored, and the next address is tried. At startup the driver reads the
displayed screen with `FLAGRD 5`, saves the board's LUT with `LUTSAV`, shows graphics
with `DISPLA 0` and enables error reporting.

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
replies between bounded command packets, so a stalled shell cannot fill its FIFO.
`ioflush()` discards unread replies, so the driver uses it only at startup and after
its last command, never between a command and its reply. One reply confirms a group
of polygon fills, and one the six per-frame model/view commands. The driver resends
`v3mdorg` and `v3vwrpt` each frame, because the manual does not say that `v3mdid`
and `v3vwid` preserve the origin and reference point. A fill color equal to the
previous one is not resent; the driver forgets the color after a clear, the OSD and
any error. Diagnostics name the operation and error bits.

By default the driver assumes a 256-byte command FIFO, the size of the PGC's: a
group is four fills and at most 228 bytes are in flight. In the recorder a key-1
frame then needs 58 replies instead of about 270. Matrox's library source gives the
PG Series 1 boards 4,096-word FIFOs (`FIFO_SIZE` in `IOLIB.H`, and the limit its
`ioputw()` checks against). The *Command Reference* does not state a size, so that
is optional: set `MTXFIFO` to the command FIFO size in bytes, from 256 to 8192,
before starting the program. `set MTXFIFO=8192` uses the whole 4,096-word FIFO, with
145 fills a group, and the same frame needs 8 replies. Other values are rejected. A
larger group means that more fills are drawn before a recoverable error is reported;
the frame is retried either way. `MTXFIFO` does not affect PGC boards, whose
transfer waits for room in their 256-byte FIFO.

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

A PGC reports numbered errors through its error FIFO, which the driver drains
whenever it waits; a board whose error FIFO is full stops taking commands. The driver
confirms a PGC frame with `FLAGRD 4` once after its fills and once after the OSD,
since the board answers only after running every earlier command. Range (1),
overflow (4) and area-fill (10) errors in a native frame retry it with host
transforms, as for LibShell; other errors, errors in host-projected frames, and a
board that takes no command or sends no answer for ten seconds stop the program; an
error message gives the manual's number for the error.

Cleanup restores the palette, its LUT selection, the plane mask read with `LSINQ` 88
(a shell without that index keeps its mask) and the error-reporting policy while
communication still works, and sends no more board commands after communication is
abandoned. On a PGC, cleanup restores the LUT saved at startup (`LUTINT 255`) and
the screen shown then (`DISPLA`), waits up to two seconds for queued commands to run,
and returns the board to ASCII commands (`CA`) with its earlier error-reporting flag.
Ctrl-C and Ctrl-Break set a flag through INT 23h instead of ending the program inside
DOS; the event loop then exits through the same cleanup as Escape, so board emulation
is restored. From DOS 3.1, critical disk errors (INT 24h) fail the
DOS call instead of prompting on an invisible screen.

## Validation

See the [Matrox checks](testing.md#matrox-libshell-and-pgc). Native validation
requires the board and Microsoft C 7 or OpenWatcom. The LibShell binding follows
Matrox's binding source and the Command Reference's formats, but its register
accesses, the presence test and the startup quiet interval still need the board to
confirm, as do the INT 23h exit path and the speed of grouped checkpoints and integer
culling. So does the native perspective: if the globe's size or position changes by
more than a pixel when `S` switches modes, LibShell's projection differs from the PGC
formula above.

The PGC path follows the IBM *Technical Reference* and the PG-640A manual, and is
tested against simulated boards and a command recorder only. Hardware still needs to
confirm the communication-area probe and the switch to hexadecimal commands, the
PG-640A identification and its `LUTX` and two-byte `PROJCT`, the frame
synchronization's speed, and the 3D projection, which `S` should leave unchanged in
size and position. `MTXFIFO` settings above 256 need a LIB Shell board to confirm the
4,096-word FIFO.
