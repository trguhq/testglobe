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
strict C89, warnings, ASan, and UBSan, with undefined behavior treated as fatal.
Legacy graphics API declarations come from test fixtures, except for the original
Matrox SDK headers described below. XGL contracts also require real X11
development headers and libraries.

```sh
python3 tests/run.py
```

The baseline checks cover OpenGL indexed strips with separate pole triangles, XGL
RGB strips, IRIS GL indexed strips, and GDI/QuickDraw indexed quad batches. They
verify all five globe resolutions, exact primitive tuples and palette bytes,
outward winding, pole geometry, packet seams and shared vertices, UV seams, and
texture-toggle dispatch where supported. The shared `tests/quad_batches.c` fixture
is built separately for GDI and QuickDraw; at key 5 it checks 2,048 packets and
131,072 quad patches, with no packet larger than 130 vertices. OpenGL defaults to
`EARTH_512` (key 5); XGL and IRIS GL default to `EARTH_128` (key 3). The retained
independent-triangle path has a separate reference check using the test-only
forced-include header `tests/tri_mode.h`. Normal builds select primitive modes in
driver headers; do not pass `TRI`, `TRISTRIP`, or `QUAD` on the compiler command
line.

Traversal checks use `globe_draw_tris()`, `globe_draw_tristrips()`,
`globe_draw_quads()`, and `globe_draw_quadbatches()` as separate entry points.
Driver callbacks retain singular primitive names; their color/RGB/UV declarations
are grouped and selected by the driver headers' implemented capabilities.

`tests/driver_api.c` checks declarations for all six drivers and the retained TRI
configuration, rejects unsupported callbacks, and checks the separate three-argument
XGL RGB/UV signatures. `tests/driver_helpers.c` exercises the shared `drv.c` utilities
in flat OpenGL and segmented GDI configurations: 1,026-vertex strip splitting with
even overlap, odd limits and tails, exact UV triangle parity, invalid-input guards,
standard and unsupported keys, FPS enable/disable behavior, debug metadata,
bounded multi-turn and zero-window drag handling, very large elapsed-time rotation,
indexed quad validation, and individual-quad packing. The quad geometry fixture verifies
individual and batched globe traversal separately at all five resolutions.

Core geometry checks also cover custom latitude counts by sampling the complete
source map at each band's center, and reject nonfinite radii while preserving a valid
existing mesh. Resource-backed builds must release the temporary Earth byte buffer
after constructing indexed colors.
Configuration checks exercise repaired platform/architecture defaults and explicit
errors for unsupported legacy driver selections.

With `--ogl-contract` or `--igl-contract`, `tests/texture.c` compares every expanded
RGB texel with its Earth palette source at all five resolutions. It checks repeated
generation with same-size allocation reuse, changed-size replacement-allocation
failure, invalid sizes, idempotent cleanup, and the retained dimensions needed by
XGL after releasing the staging image.

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
reported as skipped. The baseline geometry checks require no XGL SDK.

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

The native geometry checks cover full interior latitude strips, a single
`GL_TRIANGLES` batch for both polar caps, original winding and provoking-vertex
colors, per-vertex UV seams, and display-list caching and invalidation. Strip and
cap topology is checked against the retained independent-triangle reference at
all five Earth resolutions.

Display-list controls check the enabled default, both `d` and `D`, redraw requests,
cache deletion on disable, complete uncached geometry without list calls, and lazy
rebuilding after re-enabling. Resolution and texture changes are checked while lists
are disabled. OSD checks verify `dlist on` only after successful list execution,
`dlist off` when disabled, and `dlist fail` when enabled lists fail or are bypassed.
The status follows FPS and precedes texture status, with the display-list key last
in the help text. Indexed software texturing continues to bypass native lists in
either setting and reports `dlist fail` while they are enabled.

The GLU mipmap path is checked with a 256-pixel texture-size limit and a larger
source image, as well as a failed mipmap build. Native-rendering statistics must
omit visibility counts that the graphics API does not report.

Failure injection checks zero display-list allocation, rejected list creation,
partial compilation failure, invalid list validation, and execution errors, with
failed-list deletion and complete immediate-mode fallback. Validation and error
checks use OpenGL 1.1-compatible `glIsList` and `glGetError` calls. Native texture
allocation, image upload, and mipmap errors must leave textures unloaded and disabled
and release their resources. FPS toggling, right-button metadata, and both released
and retained software-raster cache policies are recorded independently of geometry caching.

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

Software-texture profiles verify both the default raster release on disable and
the optional `TEXTURE_CACHE_RASTER=1` retained cache.

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
and cleanup. Raster-release checks retain mipmaps and the indexed-color lookup
table for re-enabling at a resized viewport. Invalid matrices, vertices, and UVs
must be rejected before integer conversion; very large finite repeating UVs
remain valid.

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

Separate `drv_draw_tristrip_flat_rgb()` and `drv_draw_tristrip_flat_uv()` callbacks
verify that flat facets retain their RGB values and textured strips retain their
UVs. The textured native facet metadata is white because XGL's REPLACE operation
uses the texture color directly.

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

Inquiry records include the generic `LSINQ` reply's leading word count and
verify that its payload is consumed before subsequent LUT reads. Empty and
unexpected-length replies must fail without leaving unread payload words.

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
original data, large global-heap buffers, exact palette bytes and indexed quad
topology, independent double-precision projection references at rotated poses, 80387
selection and the integer fallback (including a 286/287 machine), near/far and
screen clipping, bounded `PolyPolygon` packets, foreground/background palette
handling, buffering, mouse/keyboard/resize messages, timer rollover,
Escape/window-close cleanup, and global-heap/object ownership.

Small-screen defaults, explicit size overrides, and wrapped quad/FPS/math
statistics are recorded. Synthetic indexed quad batches verify shared-vertex
projection, exact colors, winding, clipping, pole compaction, and packet boundaries,
with every native `PolyPolygon` call bounded to 64 polygons. Resource mocks check
native lock/unlock/release ownership. The Win16 `MakeProcInstance` callback casts
suppress only GCC's `-Wcast-function-type` diagnostic.

Fatal-error profiles inject polygon drawing failure, vertex-allocation failure during
a resolution change, and an oversized initial window. The modal error dialog dispatches
paint, timer, and input messages: rendering must already be disabled, partial mesh state
must stay untouched, and cleanup must release the timer, window, heap, and GDI objects.

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
larger than 64 KiB, exact palette uploads and indexed quad topology, independent
double-precision projections at rotated poses, fixed-point and FPU paths,
CPU/FPU dispatch guards, near/far/window clipping, native polygon/text calls,
1-bit luminance dithering, 4/8/16/32-bit color paths, maximum-depth device
selection on a mixed-depth desktop, old Color QuickDraw without GWorld calls,
GWorld allocation/locking failure fallback, port/device restoration across
mixed-depth drawing, short drag-poll sleeps, input/activation/suspend events,
resize and 1×1 viewports, `TickCount` rollover, animation,
window-close/Command-Q exits, and heap/Toolbox ownership.

The small-screen checks cover 640×480 and 512×342 desktops, larger screens, and
explicit per-dimension overrides. OSD checks verify wrapped text, quad/FPS
statistics, and the actual GWorld on/off state after allocation or lock failure.
Synthetic indexed quad packets independently check shared vertices, colors, winding,
clipping, pole compaction, and packet boundaries; large globe checks use linear
traversal rather than quadratic polygon matching. The monochrome-68000 case rejects
every Color QuickDraw, Palette Manager, and GWorld call. Tests model
`OpenPoly`/`ClosePoly`'s automatic pen hiding so outlines cannot accidentally be
drawn while recording filled polygons.

Readiness checks cover early drawing and late events during teardown. OSD calculations
exercise the supported 32,767-coordinate boundary with widened arithmetic. Activation,
resume, and completed drags re-sample display properties, including depth changes that
require palette, pattern, and GWorld reconstruction.

Polygon and color-pattern allocation failures check that pending polygons are released,
GWorld pixels unlocked, and the caller's port/device restored before a modal alert.
Reentrant drawing during the alert is blocked, and repeated teardown must be safe.

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

Both driver contracts link the actual shared `soft3d.c` module and exercise quad
batching in `globe_draw_quadbatches()` and Earth-resource traversal in `drv.c`.
The quad packets contain at most 64 quads and 130 shared vertices, with four indices
and one palette byte per quad. The `DRV_QUAD_BATCH` capability retains `QUAD` as
the primitive; `globe_draw_quads()` retains individual submission, including Matrox.
In addition to the independent projection references, a shared public-pipeline case clips a
triangle spanning 268 million coordinate units to exact viewport corners, then
reverses its winding to check rejection. This exercises clipping products that
would overflow a 32-bit intermediate without depending on private helpers.
Large positive and negative accepted rotation angles must produce the same
fixed matrix as their equivalent angles modulo 360 degrees.

Quad projection cases check exact screen coordinates, near/far rejection, both
collapsed poles, and the retained triangle projection entry point. Polygon culling
uses the whole signed area: a rounded silhouette whose first fan triangle has the
opposite orientation must still be classified correctly. A full 32,767-pixel
viewport checks the area calculation at the supported integer coordinate limit.
