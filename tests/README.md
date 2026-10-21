Record OpenGL/GLUT format selection and the indexed rendering path:

```sh
python3 tests/run.py --ogl-contract
```

These host records check RGBA32, a 32-bit single-buffer visual competing with
24-bit double buffering, RGB24/RGB16 and color-index8 fallbacks, unknown alpha
queries, destruction of every probe context, a fresh final window at its
requested size, initial redisplay, absent depth buffers, exact Earth palette installation, index-byte
facets and native RGB textures. The indexed texture path uses the real shared
software renderer; its transferred bytes are compared with every numeric
framebuffer index, including on hosts with 64-bit `unsigned long`. Pixel-transfer
and unpack state is deliberately nondefault to verify restoration. These
records supplement actual OpenGL GUI smoke tests; they do not emulate a GL
implementation or prove availability of indexed visuals on a particular system.

Record the production IRIS GL driver against manual-derived native call
declarations, and check the real software texture fallback's pixels:

```sh
python3 tests/run.py --igl-contract
```

The native recorder checks all five Earth levels and the default 128 level,
native triangle-mesh winding and provoking-vertex colors independently
located by spherical centroids, per-vertex UVs,
matrix composition and OSD restoration, native object caching/invalidation,
every packed texture word, RGBA32/RGB24/color-index8 format selection,
precision before buffering, unknown alpha queries, exact indexed palette
colors/restoration, missing depth buffers, unknown/absent texture capabilities,
older headers, optional affine-texture subdivision, keyboard controls, tied
mouse movement, focus, redraw sizing, queue overflow, Escape and window quit.
At resolution 512 it verifies 1,278 native meshes and 263,676 submitted
vertices, with 1,026-vertex interior bands and separate nondegenerate polar
caps. Textured rendering runs at all five resolutions in both native and
software capability profiles. A separate indexed strip geometry fixture
checks exact TRI-equivalent tuples, original color bytes, UVs and seams
without native graphics calls. It models IRIS GL's implicit projection-matrix
behavior rather than OpenGL's
matrix calls. The standalone software cases use analytically predicted pixels
for row-vector transforms, six clipping planes, depth/culling, perspective
interpolation, wrapping, bilinear/trilinear filtering, mipmap minification,
indexed conversion and cleanup. All use strict C89, ASan and UBSan.

The recorder headers are **not an SGI SDK or ABI**, and their optional
subdivision constants are explicit fixture tags. Prototypes are checked against
the uploaded SGI guides and 1990 reference; host `unsigned long` image words
are compared numerically. Native IRIX 4.0 compilation, 32-bit ABI, graphics
hardware, fonts, window-manager behavior and pixel transfer require SGI tools
and runtime validation.

Run the OpenGL indexed triangle, XGL RGB strip, and GDI/QuickDraw indexed strip geometry checks with:

```sh
python3 tests/run.py
```

To exercise the System 7 QuickDraw driver, its actual common startup and event
loop, and the separate FPU helper against host Toolbox call records:

```sh
python3 tests/run.py --qd-contract
```

The checks cover all five Earth resolutions, the default low-resolution globe,
resource chunks compared byte-for-byte with the original source data, buffers
larger than 64 KiB, exact palette uploads and strip topology,
independent double-precision projections at rotated poses, fixed-point and FPU
paths, CPU/FPU dispatch guards, near/far/window clipping, native polygon/text
calls, 1-bit luminance dithering, 4/8/16/32-bit color paths, maximum-depth device
selection on a mixed-depth desktop, old Color QuickDraw
without GWorld calls, GWorld allocation/locking failure fallback, port/device
restoration across mixed-depth drawing, short drag-poll sleeps,
input/activation/suspend events, resize and 1×1 viewports, TickCount rollover,
animation, window-close/Command-Q exits, and heap/Toolbox ownership. The
small-screen checks cover 640x480 and 512x342 desktops, larger screens and
explicit per-dimension overrides. OSD checks verify wrapped text, triangle/FPS
statistics and the actual GWorld on/off state after allocation or lock failure.
A synthetic 1,026-vertex strip independently checks all 1,024 facet colors and
winding across native batch boundaries; large globe checks use linear traversal
rather than quadratic polygon matching. The
monochrome-68000 case rejects every Color QuickDraw, Palette Manager, and GWorld
call. Tests model OpenPoly/ClosePoly's automatic pen hiding so outlines cannot
accidentally be drawn while recording filled polygons.

`tests/qd_shim/` contains manual-derived call declarations checked against public
classic interface declarations. It is **not Apple's SDK**, an ABI replacement,
or a renderer. Native 16-bit Point/Rect fields are modeled, while the host's
long and pointer widths, byte order, executable format, code segments, A5 world,
SANE/68881 dispatch, and Toolbox implementations differ. The actual FPU helper
is compiled as ordinary host C for numerical checks; only an MPW build can
verify its instruction selection and separate CODE segment. Native validation
requires MPW, the stated Apple interfaces/libraries, and System 7 hardware or an
appropriate emulator. Host sanitizer checks do not verify physical pixel colors
or QuickDraw hardware acceleration.

To exercise the native Win16 GDI driver's actual `WinMain`, common startup,
message loop, and rendering calls against a host call recorder:

```sh
python3 tests/run.py --gdi-contract
```

These checks cover all five Earth resolutions, resource chunks compared with
the original data, large global-heap buffers, exact palette bytes and strip
topology, independent double-precision projection references at rotated poses,
80387 selection and the integer fallback (including a 286/287 machine),
near/far and screen clipping, bounded PolyPolygon packets, foreground/background
palette handling, buffering, mouse/keyboard/resize messages, timer rollover,
Escape/window-close cleanup, and global-heap/object ownership.
Small-screen defaults, explicit size overrides and wrapped triangle/FPS/math
statistics are recorded. Synthetic full-width and odd-tail strips verify exact
facet colors and winding with every native PolyPolygon packet bounded to 128
facets. Resource mocks check native lock/unlock/release ownership. Tests use strict
C89, AddressSanitizer, and UndefinedBehaviorSanitizer. The unavoidable
Win16 `MakeProcInstance` callback casts suppress only GCC's
`-Wcast-function-type` diagnostic.

Both driver contracts link the actual shared `soft3d.c` module and exercise
the common strip batching and Earth-resource traversal in `drv.c`. In addition
to the independent projection references, a shared public-pipeline case clips
a triangle spanning 268 million coordinate units to exact viewport corners,
then reverses its winding to check rejection. This exercises clipping products
that would overflow a 32-bit intermediate without depending on private helpers.
Large positive and negative accepted rotation angles must produce the same
fixed matrix as their equivalent angles modulo 360 degrees.

`tests/win16_shim/windows.h` contains manual-derived call declarations,
cross-checked against public Win16 API declarations. It is **not Microsoft's
SDK**, an ABI-compatible replacement, or a GDI renderer. Its `POINT` coordinates
are 16-bit, but the host's C `int`, `long`, calling conventions, and pointers do
not reproduce Microsoft C 7 code generation. Passing the checks does not verify
the executable's Windows loader format, segmented pointer arithmetic,
floating-point emulator dispatch, physical display palette mapping, or rendered
pixels. Those require Microsoft C 7, the Windows 3.1 SDK, and Windows itself.

To also exercise the actual Sun XGL driver with real X11 windows/events under
Xvfb and a recording replacement for XGL calls:

```sh
python3 tests/run.py --xgl-contract
```

This optional check requires X11 development headers/libraries and `xvfb-run`.
Set `XGL_TEST_CPPFLAGS` and `XGL_TEST_LDFLAGS` if X11 is installed outside the
compiler's default paths. It checks native-call arguments, flat facet/strip
packets, packed texture pixels and mipmap settings, clipping and model/view
setup, single/double buffering, X11 input/resize events, and both Escape and
window-manager close cleanup. `tests/xgl_record/xgl/xgl.h` is a small,
manual-derived recording contract with test-local symbolic values: it is **not
Sun's SDK header**, an ABI-compatible replacement, or a renderer. Passing this
check does not establish that a Solaris XGL implementation rendered an image.

To include the DOS/Matrox contract checks, provide the original LibShell headers
outside the repository, under `XGX1/HEADERS`:

```sh
python3 tests/run.py --sdk /path/to/sdk
# Or set MATROX_SDK=/path/to/sdk.
```

The tests compile the production geometry, shared driver state, and Matrox driver
as strict C89, with AddressSanitizer and UndefinedBehaviorSanitizer. Primitive
modes come from each driver header, with no TRI, TRISTRIP, or QUAD command-line
defines. The XGL geometry checks need no XGL SDK: they verify all five globe
resolutions, complete latitude strips, exact individual-triangle winding,
per-facet colors, nondegenerate polar caps, UV seams, and texture-toggle arguments.
The Matrox
driver is compiled against the supplied SDK's actual function prototypes. DOS
and board calls are recorded by test-only stubs; the tests erase the compiler's
pointer keywords on the host. Vendor headers are copied into a temporary
directory with the trailing DOS EOF byte removed, and are not committed here.

These tests check geometry, palette/index behavior, command arguments, input,
and cleanup, including primitive-correct triangle and quad OSD statistics.
Visibility checks independently transform outward normals through
the recorded model and inverse-view rotations, and compare every expected fill
and cap count at multiple noncardinal poses in both Matrox resolutions.
They do not validate segmented pointer code generation, DOS timing,
LibShell's binary ABI, native XGL rendering, or graphics output on a physical
PG-641. Native Matrox checks require Microsoft C 7, `LSCL.LIB`, and the board;
native XGL checks require Sun's actual SDK/runtime on Solaris. Without an SDK path, the Matrox checks
are explicitly reported as skipped.
