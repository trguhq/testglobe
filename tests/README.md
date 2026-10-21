# Host regression checks

These checks compile production code on a host system and verify geometry,
software-rendered pixels, native API call arguments, input handling, and resource
cleanup. They supplement native builds and runtime checks; the call recorders do
not emulate the original graphics implementations or prove their binary ABIs.

For native build instructions, see the [project README](../README.md#build-instructions).

## Requirements and quick start

Run commands from the repository root. The baseline checks require Python 3 and
a C compiler with AddressSanitizer and UndefinedBehaviorSanitizer support. The
runner uses `cc` by default; set `CC` to select another compiler. Compilation uses
strict C89, warnings, ASan, and UBSan. Legacy graphics API declarations come from
test fixtures, except for the original Matrox SDK headers described below. XGL
contracts also require real X11 development headers and libraries.

```sh
python3 tests/run.py
```

The baseline checks cover OpenGL indexed triangles, XGL RGB strips, and IRIS GL,
GDI, and QuickDraw indexed strips. They verify all five globe resolutions,
TRI-equivalent winding and colors, nondegenerate polar caps, UV seams, and
texture-toggle dispatch where supported. XGL and IRIS GL default to `EARTH_128`
(key 3). Primitive modes come from the driver headers; do not pass `TRI`,
`TRISTRIP`, or `QUAD` on the compiler command line.

Additional options may be combined in one invocation:

| Option | Additional checks | Extra requirements |
| --- | --- | --- |
| [`--ogl-contract`](#opengl) | OpenGL/GLUT calls and software texture pixels | None |
| [`--igl-contract`](#iris-gl) | IRIS GL calls and software texture pixels | None |
| [`--xgl-contract`](#sun-xgl) | XGL calls with real X11 windows and events | X11 development files, Xvfb, and `xvfb-run` |
| [`--sdk /path/to/sdk`](#matrox-libshell) | DOS/Matrox calls against original SDK declarations | LibShell headers under `XGX1/HEADERS` |
| [`--gdi-contract`](#windows-gdi) | Win16 startup, GDI calls, and messages | None |
| [`--qd-contract`](#quickdraw) | System 7 startup, Toolbox calls, and events | None |

Without `--sdk` or `MATROX_SDK`, the Matrox SDK contract checks are explicitly
reported as skipped. The baseline strip checks require no XGL SDK.

## OpenGL

Record OpenGL/GLUT format selection and the indexed rendering path:

```sh
python3 tests/run.py --ogl-contract
```

The records check RGBA32, a 32-bit single-buffer visual competing with 24-bit
double buffering, RGB24/RGB16 and color-index8 fallbacks, unknown alpha queries,
destruction of every probe context, a fresh final window at its requested size,
initial redisplay, absent depth buffers, exact Earth palette installation,
index-byte facets, and native RGB textures.

Each window has its own display callback. Probe hiding and final-window
resizing/showing dispatch an early initial redraw to check that callbacks exist
before native window operations and safely do no rendering before globe
initialization. The final window keeps its temporary callback until globe data
is initialized; entering the driver loop must register the globe renderer and
request a fresh redraw before the GLUT loop. This checks the callback ordering
required by Apple's GLUT without modeling its Cocoa event loop.

The indexed texture path uses the real shared software renderer. Its transferred
bytes are compared with every numeric framebuffer index, including on hosts with
64-bit `unsigned long`. Pixel-transfer and unpack state is deliberately
nondefault to verify restoration. These records supplement actual OpenGL GUI
smoke tests; they do not emulate a GL implementation or prove availability of
indexed visuals on a particular system.

## IRIS GL

Record the production IRIS GL driver against manual-derived native call
declarations, and check the real software texture fallback's pixels:

```sh
python3 tests/run.py --igl-contract
```

The native recorder checks all five Earth levels and the default 128 level,
native triangle-mesh winding and provoking-vertex colors independently located
by spherical centroids, per-vertex UVs, matrix composition and OSD restoration,
native object caching/invalidation, every packed texture word,
RGBA32/RGB24/color-index8 format selection, precision before buffering, unknown
alpha queries, exact indexed palette colors/restoration, missing depth buffers,
unknown/absent texture capabilities, older headers, optional affine-texture
subdivision, keyboard controls, tied mouse movement, focus, redraw sizing, queue
overflow, Escape, and window quit.

At resolution 512 it verifies 1,278 native meshes and 263,676 submitted vertices,
with 1,026-vertex interior bands and separate nondegenerate polar caps. Textured
rendering runs at all five resolutions in both native and software capability
profiles. A separate indexed strip geometry fixture checks exact TRI-equivalent
tuples, original color bytes, UVs, and seams without native graphics calls. The
recorder models IRIS GL's implicit projection-matrix behavior rather than
OpenGL's matrix calls.

The standalone software cases use analytically predicted pixels for row-vector
transforms, six clipping planes, depth/culling, perspective interpolation,
wrapping, bilinear/trilinear filtering, mipmap minification, indexed conversion,
and cleanup.

The recorder headers are **not an SGI SDK or ABI**, and their optional subdivision
constants are explicit fixture tags. Prototypes are checked against the supplied
SGI guides and 1990 reference; host `unsigned long` image words are compared
numerically. Native IRIX 4.0 compilation, 32-bit ABI, graphics hardware, fonts,
window-manager behavior, and pixel transfer require SGI tools and runtime
validation.

## Sun XGL

Exercise the actual Sun XGL driver with real X11 windows and events under Xvfb,
using a recording replacement for XGL calls:

```sh
python3 tests/run.py --xgl-contract
```

This option requires X11 development headers/libraries and `xvfb-run`. Set
`XGL_TEST_CPPFLAGS` and `XGL_TEST_LDFLAGS` if X11 is installed outside the
compiler's default paths. The checks cover the `EARTH_128` default, all five
resolution keys, native-call arguments, flat facet/strip packets, packed texture
pixels and mipmap settings, clipping and model/view setup, single/double
buffering, X11 input/resize events, and both Escape and window-manager close
cleanup.

`tests/xgl_record/xgl/xgl.h` is a small, manual-derived recording contract with
test-local symbolic values. It is **not Sun's SDK header**, an ABI-compatible
replacement, or a renderer. Native XGL rendering requires Sun's SDK/runtime on
Solaris; passing this check does not establish that a Solaris XGL implementation
rendered an image.

## Matrox LibShell

Provide the original LibShell headers outside the repository, under
`XGX1/HEADERS`, to include the DOS/Matrox contract checks:

```sh
python3 tests/run.py --sdk /path/to/sdk
# Or set MATROX_SDK=/path/to/sdk.
```

The tests compile the production geometry, shared driver state, and Matrox
driver against the supplied SDK's actual function prototypes. DOS and board
calls are recorded by test-only stubs; the tests erase the compiler's pointer
keywords on the host. Vendor headers are copied into a temporary directory with
the trailing DOS EOF byte removed, and are not committed here.

The checks cover geometry, palette/index behavior, command arguments, input, and
cleanup, including primitive-correct triangle and quad OSD statistics. Visibility
checks independently transform outward normals through the recorded model and
inverse-view rotations, and compare every expected fill and cap count at
multiple noncardinal poses in both Matrox resolutions.

These checks do not validate segmented pointer code generation, DOS timing,
LibShell's binary ABI, or graphics output on a physical PG-641. Native Matrox
validation requires Microsoft C 7, `LSCL.LIB`, and the board.

## Windows GDI

Exercise the native Win16 driver's actual `WinMain`, common startup, message
loop, and rendering calls against a host call recorder:

```sh
python3 tests/run.py --gdi-contract
```

The checks cover all five Earth resolutions, resource chunks compared with the
original data, large global-heap buffers, exact palette bytes and strip topology,
independent double-precision projection references at rotated poses, 80387
selection and the integer fallback (including a 286/287 machine), near/far and
screen clipping, bounded `PolyPolygon` packets, foreground/background palette
handling, buffering, mouse/keyboard/resize messages, timer rollover,
Escape/window-close cleanup, and global-heap/object ownership.

Small-screen defaults, explicit size overrides, and wrapped triangle/FPS/math
statistics are recorded. Synthetic full-width and odd-tail strips verify exact
facet colors and winding with every native `PolyPolygon` packet bounded to 128
facets. Resource mocks check native lock/unlock/release ownership. The Win16
`MakeProcInstance` callback casts suppress only GCC's `-Wcast-function-type`
diagnostic.

`tests/win16_shim/windows.h` contains manual-derived call declarations,
cross-checked against public Win16 API declarations. It is **not Microsoft's
SDK**, an ABI-compatible replacement, or a GDI renderer. Its `POINT` coordinates
are 16-bit, but the host's C `int`, `long`, calling conventions, and pointers do
not reproduce Microsoft C 7 code generation. Passing the checks does not verify
the executable's Windows loader format, segmented pointer arithmetic,
floating-point emulator dispatch, physical display palette mapping, or rendered
pixels. Those require Microsoft C 7, the Windows 3.1 SDK, and Windows itself.

## QuickDraw

Exercise the System 7 driver's actual common startup and event loop, and its
separate FPU helper, against host Toolbox call records:

```sh
python3 tests/run.py --qd-contract
```

The checks cover all five Earth resolutions, the default low-resolution globe,
resource chunks compared byte-for-byte with the original source data, buffers
larger than 64 KiB, exact palette uploads and strip topology, independent
double-precision projections at rotated poses, fixed-point and FPU paths,
CPU/FPU dispatch guards, near/far/window clipping, native polygon/text calls,
1-bit luminance dithering, 4/8/16/32-bit color paths, maximum-depth device
selection on a mixed-depth desktop, old Color QuickDraw without GWorld calls,
GWorld allocation/locking failure fallback, port/device restoration across
mixed-depth drawing, short drag-poll sleeps, input/activation/suspend events,
resize and 1×1 viewports, `TickCount` rollover, animation,
window-close/Command-Q exits, and heap/Toolbox ownership.

The small-screen checks cover 640×480 and 512×342 desktops, larger screens, and
explicit per-dimension overrides. OSD checks verify wrapped text, triangle/FPS
statistics, and the actual GWorld on/off state after allocation or lock failure.
A synthetic 1,026-vertex strip independently checks all 1,024 facet colors and
winding across native batch boundaries; large globe checks use linear traversal
rather than quadratic polygon matching. The monochrome-68000 case rejects every
Color QuickDraw, Palette Manager, and GWorld call. Tests model
`OpenPoly`/`ClosePoly`'s automatic pen hiding so outlines cannot accidentally be
drawn while recording filled polygons.

`tests/qd_shim/` contains manual-derived call declarations checked against public
classic interface declarations. It is **not Apple's SDK**, an ABI replacement,
or a renderer. Native 16-bit `Point`/`Rect` fields are modeled, while the host's
long and pointer widths, byte order, executable format, code segments, A5 world,
SANE/68881 dispatch, and Toolbox implementations differ. The actual FPU helper
is compiled as ordinary host C for numerical checks; only an MPW build can
verify its instruction selection and separate `CODE` segment. Native validation
requires MPW, the stated Apple interfaces/libraries, and System 7 hardware or an
appropriate emulator. Host sanitizer checks do not verify physical pixel colors
or QuickDraw hardware acceleration.

## Shared GDI and QuickDraw pipeline

Both driver contracts link the actual shared `soft3d.c` module and exercise the
common strip batching and Earth-resource traversal in `drv.c`. In addition to
the independent projection references, a shared public-pipeline case clips a
triangle spanning 268 million coordinate units to exact viewport corners, then
reverses its winding to check rejection. This exercises clipping products that
would overflow a 32-bit intermediate without depending on private helpers.
Large positive and negative accepted rotation angles must produce the same
fixed matrix as their equivalent angles modulo 360 degrees.
