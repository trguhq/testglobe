# Host regression checks

These checks compile production code on a host system and verify geometry,
software-rendered pixels, graphics API arguments, input, and resource cleanup.
Separate [WebGL checks](#webgl) verify the browser frontend's data, geometry,
rendered pixels, and controls without changing the native test requirements.
For application build instructions, see the [project README](../README.md#build-instructions).

## Requirements and quick start

Run commands from the repository root. The native C runner requires:

- Python 3.8 or later.
- A C compiler accepting GCC/Clang-style options, with AddressSanitizer and
  UndefinedBehaviorSanitizer support. `CC` selects the compiler; the default is `cc`.
- A POSIX host and filesystem supporting FIFOs, symbolic links, and hard links.
- `nm` with `-S` output containing GNU-style address, size, type, and symbol fields.
  Other `nm` formats, including platform-specific variants, are not interchangeable.

These are host-test requirements; the runner is not intended for the historical
DOS, Windows, Macintosh, or Unix toolchains used to build the application.

```sh
python3 tests/run.py
```

Additional options may be combined:

| Option | Additional checks | Extra requirements |
| --- | --- | --- |
| [`--ogl-contract`](#opengl) | OpenGL/GLUT calls and software texture pixels | None |
| [`--igl-contract`](#iris-gl) | IRIS GL calls and software texture pixels | None |
| [`--xgl-contract`](#sun-xgl) | XGL calls with real X11 windows and events | X11 development files, Xvfb, and `xvfb-run` |
| [`--sdk /path/to/sdk`](#matrox-libshell) | DOS/Matrox calls against original SDK declarations | LibShell headers under `XGX1/HEADERS` |
| [`--gdi-contract`](#windows-gdi) | Win16 startup, GDI calls, and messages | None |
| [`--qd-contract`](#quickdraw) | System 7 startup, Toolbox calls, and events | None |

With all prerequisites installed, run every native group with:

```sh
python3 tests/run.py --ogl-contract --igl-contract --xgl-contract \
  --gdi-contract --qd-contract --sdk /path/to/sdk
```

Exit status zero means all requested checks passed. `PASS` lines identify completed
checks; `SKIP` identifies an optional group that did not run. Without `--sdk` or
`MATROX_SDK`, the Matrox SDK group is reported as skipped. The baseline needs no
vendor graphics SDK. An intentionally injected application failure can be a passing
test when the runner verifies its expected exit status and diagnostics.

## What the host checks establish

Compilation uses strict C89, warnings, ASan, and UBSan, with undefined behavior
handled as fatal. `NO_SINF` selects the C89 math path; the runner does not replay
every Makefile target's compiler options.

Each requested driver contract also compiles its production source files as separate
translation units. This catches dependencies accidentally supplied by a fixture that
includes a driver `.c` file. These checks use the same recorder declarations as the
behavior tests; they do not substitute for compiling with the native SDK and linker.

Most legacy API declarations come from test fixtures; Matrox uses the supplied SDK
headers. Call recorders check submitted arguments and simulated outcomes. They do
not implement the original graphics libraries, establish native binary ABIs, or
verify hardware rendering. Host integer and pointer widths, byte order, and calling
conventions remain those of the host, even when segmented-driver keywords are
removed for compilation. Platform-specific validation requirements appear below.

OSD meanings are defined under [Controls and status](../README.md#controls-and-status).
The tests check those states against actual submission, fallback, and failure paths,
rather than only checking the user's toggle settings.

## Baseline coverage

### Earth data

The runner builds `src/e_bin.c` and checks deterministic regeneration against the
supplied `src/e_saved.c`. Missing, truncated, and oversized chunks must preserve an
existing output. Input aliases, symbolic links, directories, FIFOs, and occupied
temporary or backup paths must fail promptly without changing files. Changing the
shared chunk size without updating the binary layout must reject generation; stale
generated readers must also fail compilation.

`tests/earth_data.c` compares every embedded byte with the binary maps and checks
invalid reads, 16 KiB chunk seams, and 64 KiB boundaries. It also runs from an empty
directory to verify that runtime map files are unnecessary. Object symbols verify
the 16 KiB per-array limit and Matrox's inclusion of only `EARTH_32` and `EARTH_64`.
`tests/earth_reference.h` reads the binaries for expected colors; the application
does not use that test-only file I/O.

### Geometry and shared interfaces

- OpenGL indexed strips and separate pole triangles, XGL RGB strips, IRIS GL indexed
  strips, and GDI/QuickDraw quad batches are checked at all five resolutions. Checks
  compare primitive tuples, palette bytes, outward winding, pole geometry, shared
  vertices, packet and UV seams, and texture dispatch where supported.
- `tests/quad_batches.c` checks individual and batched traversal for GDI and QuickDraw.
  At key 5 it verifies 2,048 packets and 131,072 quad patches, with at most 130 vertices
  per packet. Driver defaults are checked alongside resolution changes.
- The independent-triangle reference uses `tests/tri_mode.h`. Normal builds select
  `TRI`, `TRISTRIP`, or `QUAD` in driver headers, not through command-line defines.
- `tests/driver_api.c` checks supported declarations, rejects unsupported callbacks,
  and checks XGL's distinct RGB/UV signatures. Plural `globe_draw_*` traversal names
  and singular `drv_draw_*` callbacks are checked as separate interfaces.
- Custom latitude counts sample the full source map. Nonfinite radii must preserve
  an existing valid mesh and return a failure result. Native floating-point drivers
  accept radii above 1,000; bounded drivers reject requests above their own limits
  before replacing a valid mesh. Compiled paths borrow indexed
  map spans; resource-backed builds populate final color storage directly. Generated
  UVs and packet-local RGB expansion must match the original mesh reference.

### Shared utilities and configuration

`tests/driver_helpers.c` exercises flat OpenGL and segmented GDI configurations:
strip splitting with even overlap and odd limits, UV triangle parity, quad packet
bounds and packing, invalid inputs, key handling, FPS toggling, debug metadata,
bounded dragging, zero-sized windows, and large elapsed-time rotations. Shared-service
checks cover allocation bounds, handle release, bounded status messages, empty-scene
OSD, and completed-presentation timing that includes time between native events.
Configuration checks cover platform and architecture defaults, conflicting settings, and explicit
errors for unsupported legacy drivers.

`tests/recovery.c` injects failures into each scene-allocation stage in compiled-map
and resource-backed builds. It checks bounded retries, restoring or reducing the
resolution, empty-scene recovery, no-op revision handling, direct resource loading,
reduced-latitude compaction, exact retained allocation sizes, and aborted traversal.
Console diagnostics describe committed meshes only; GUI builds remain silent.

`tests/tex_take.c` verifies ownership transfer of the software texture base image,
all nine lower-mipmap allocation failures, invalid dimensions, and the retained
copying interface for borrowed input. Every adopted allocation must be released
exactly once, including partial-construction failures.

## WebGL

### Commands and requirements

Palette verification requires Python 3. The data and geometry checks require Node.js
18 or later, with no npm packages, browser, or server:

```sh
python3 web/gen_cmap.py --check
node tests/web.mjs
```

Browser checks require Python 3.10 or later, the Python Playwright package, and
Chromium. Install them in a suitable Python environment if needed:

```sh
python3 -m pip install playwright
python3 -m playwright install chromium
python3 tests/web_browser.py
```

The browser test starts its own HTTP server at the repository root. It selects
`CHROMIUM_BIN`, then `chromium` or `chromium-browser` on `PATH`, then Playwright's
installed Chromium. These checks run separately from `tests/run.py` and add no native
application build dependencies.

### Coverage

- **Canonical data:** the generated palette matches `src/e_cmap.h`; fetched bytes
  match every `data/*.bin` map. Tests reject unsupported widths, HTTP failures,
  incorrect chunk sizes, and partial or cancelled loads.
- **Geometry:** all five resolutions match the native triangle topology, including
  nondegenerate pole triangles, outward winding, strip-restart parity, index bounds,
  and UV coordinates. The frontend retains no global map-data cache.
- **Browser rendering and controls:** the browser checks exercise the production
  page, canvas output, resolution selection, texture/rotation/OSD/FPS controls,
  resetting the view, dragging, resizing, and touch input.
- **Recovery:** browser checks cover unavailable WebGL, failed map requests,
  resolution fallback, competing requests, texture failure and reduction, resize
  retry, retained resources after failed GPU uploads, and context loss/restoration
  when `WEBGL_lose_context` is available.

### Validation limits

The browser checks exercise a real WebGL implementation, which may use a software
renderer in headless environments. They do not establish hardware acceleration,
cross-browser compatibility, mobile performance, or availability of the public demo.
Manual browser checks remain useful for graphics drivers, accessibility, and touch
behavior on physical devices. The frontend requires WebGL 2 and has no WebGL 1 path.

## OpenGL

### Command

```sh
python3 tests/run.py --ogl-contract
```

### Coverage

- **Formats and palette:** RGBA32, RGB24/RGB16, and color-index8; precision before
  buffering; unknown alpha queries; missing depth buffers; exact palette installation
  and index-byte facets. Classic GLUT's `rgba` alpha requirement and FreeGLUT's
  interpretation are modeled separately so both select RGB fallbacks correctly.
- **Geometry and caching:** latitude strips and one polar-cap triangle batch are
  compared with the TRI reference at all resolutions, including provoking colors and
  UVs. Display-list checks cover the enabled default, `d`/`D`, redraws, deletion,
  immediate drawing while disabled, lazy rebuilding, and resolution/texture changes.
- **Compatibility and failures:** OpenGL 1.1 declarations and object imports are
  checked. GLUT headers without display-mode availability queries use one RGB,
  double-buffered window request, including old headers without display strings.
  GLU handles legacy or unsuitable modern-mipmap cases. Allocation,
  compilation, validation, and execution failures must delete bad lists and draw
  complete immediate geometry without retrying failed caches every frame. Texture
  creation, binding, mipmap, and transfer failures must release failed state, draw
  flat colors, and allow off/on retry.
  Native and software allocation-pressure profiles select smaller usable images.
- **Window lifecycle and controls:** early probe and window redraws verify a safe
  display callback before rendering is ready, including Apple's GLUT ordering.
  Checks also cover probe cleanup, final size, FPS toggling, right-button metadata,
  OSD status/help order, and omission of unavailable visibility counts.
- **Software texturing:** transferred bytes match every framebuffer index, including
  with host 64-bit `unsigned long`. Nondefault unpack/transfer state must be restored.
  Software texturing bypasses display lists, and both raster-cache policies are
  exercised. Numerical pixel checks are described under [Shared software pipelines](#shared-software-pipelines).

### Native validation

Actual OpenGL rendering and availability of color-index visuals require GUI checks
with the installed GL implementation. Apple SDK compilation and Cocoa/GLUT event-loop
behavior require macOS; the host recorder checks callback ordering without implementing
that event loop.

## IRIS GL

### Command

```sh
python3 tests/run.py --igl-contract
```

### Coverage

- **Geometry and transforms:** all resolutions and the key-3 default; triangle-mesh
  winding, centroid-derived provoking colors, UVs, matrix composition, and OSD state
  restoration. Key 5 submits 1,278 meshes and 263,676 vertices, with 1,026-vertex interior
  bands and separate pole triangles. The recorder models IRIS GL's implicit projection
  matrix behavior.
- **Formats and textures:** RGBA32/RGB24/color-index8, precision before buffering,
  unknown alpha, palette installation/restoration, absent depth buffers, packed
  texture words, missing capabilities, older headers, and affine-texture subdivision.
  API and software-texture profiles cover all five resolutions and both raster-cache
  policies.
- **Input:** keyboard, tied mouse samples, focus, resize, overflow, Escape, and window
  quit. Missing or reversed mouse samples must neither block nor consume later
  keyboard/window events.
- **Recovery and status:** unavailable geometry objects fall back to complete direct
  drawing; cache allocation failure is latched to avoid repeated failed work.
  Recoverable texture failures invalidate incompatible caches, draw flat colors,
  preserve the requested toggle, and allow a
  retry. Status checks distinguish API submission from application software rendering.

### Native validation

The recorder's declarations are checked against the SGI guides and 1990 reference;
optional subdivision constants are test-local tags. Native IRIX 4.0 builds must
validate the 32-bit ABI, packed pixel transfer, fonts, window manager, and graphics
hardware. IRIS GL texture and pixel-transfer calls return no success value, so checks
of submission and observable failures cannot detect silent library or hardware errors.

## Sun XGL

### Command

```sh
python3 tests/run.py --xgl-contract
```

### Additional requirements

Install X11 development headers/libraries, Xvfb, and `xvfb-run`. Set
`XGL_TEST_CPPFLAGS` and `XGL_TEST_LDFLAGS` for nonstandard X11 locations. The driver
uses real X11 windows and events under Xvfb; XGL calls use a host recorder.

### Coverage

- **Geometry and formats:** the key-3 default, all resolutions, RGB facet/strip
  packets, model/view transforms, clipping, and single/double buffering. Separate
  RGB and UV callbacks preserve flat colors and texture coordinates; textured facet
  metadata is white for XGL's REPLACE operation.
- **Textures:** packed pixels, mipmap settings, and the native three-row/two-column
  orientation matrix, including identity rotation and zero translation. Recoverable
  failures must release partial resources, draw flat colors, preserve the enabled
  request, and allow retry. Nonrecoverable and unrelated errors remain fatal.
- **Input and cleanup:** X11 keyboard, mouse, resize, Escape, and window-manager close.
  Status checks track current-frame use; XGL's internal software rendering is not
  reported as application software texturing.

### Native validation

`tests/xgl_record/xgl/xgl.h` uses manual-derived declarations and test-local symbolic
values. The Sun SDK/runtime on Solaris is required to validate the real ABI, pipeline,
and graphics output; the real X11 activity in these tests does not exercise XGL itself.

## Matrox LibShell

### Command

```sh
python3 tests/run.py --sdk /path/to/sdk
# Or set MATROX_SDK=/path/to/sdk.
```

### Additional requirements

Provide the original LibShell headers under `XGX1/HEADERS` outside the repository.
The runner uses their function prototypes, copying headers into a temporary directory
and removing trailing DOS EOF bytes. DOS and board calls are recorded, and compiler
pointer keywords are removed for host compilation. Vendor headers are not committed.

### Coverage

- **Geometry and palette:** both resolutions, indexed colors, command arguments,
  triangle/quad statistics, and cleanup. Independent normal transforms check every
  expected fill and cap count at several noncardinal poses.
- **Replies and errors:** `LSINQ` word counts and payload ordering; short malformed
  replies are drained before failure, while replies longer than 16 words abandon the
  connection. Whole-reply deadlines apply even when words remain ready. Timeouts and
  oversized replies must prevent further board commands during cleanup.
- **Error policy and recovery:** initially enabled and disabled reporting policies;
  opcode, parameter, board-memory, and overflow diagnostics; multiple errors while
  awaiting completion; palette and policy restoration while communication is usable.
- **Input:** keyboard restrictions and toggles, INT 33h polling, relative motion,
  left-button dragging, and right-button redraws.

### Native validation

Microsoft C 7, `LSCL.LIB`, and a PG-641-compatible board are needed to validate huge
pointers, DOS timing, the LibShell ABI, and graphics output. The recorder assumes
nonblocking error-register reads and that positive `rdfifocnt()` permits a complete
word read. The supplied SDK documentation does not establish FIFO count units or
atomic availability; the installed library and board must validate those assumptions
and asynchronous timing.

## Windows GDI

### Command

```sh
python3 tests/run.py --gdi-contract
```

### Coverage

- **Geometry and resources:** all resolutions, binary resource contents, large global
  heaps, exact palette bytes, quad topology, independent double-precision projection
  references, clipping, and shared-vertex packets. `PolyPolygon` calls are bounded to
  64 polygons; lock/unlock/release ownership is checked.
- **CPU and display:** 80387 selection and fixed-point fallback, including a 286/287
  profile; small-screen defaults, explicit dimensions, foreground/background palettes,
  buffering, and wrapped OSD statistics.
- **Input and lifecycle:** `WinMain`, common startup, keyboard/mouse/resize messages,
  timer rollover, Escape/window close, and heap/GDI-object ownership.
- **Recovery:** missing back buffers use direct paint; failed `PolyPolygon` calls
  fall back to individual polygons; brush allocation retries or uses a stock color.
  Resource failures return typed results, empty scenes still repaint, and timer
  failure leaves manual input available. Former allocation/drawing fatal profiles
  now verify recovery. Oversized windows still follow the fatal guard: modal paint,
  timer, and input messages must not touch failed state, and cleanup releases ownership.

### Native validation

The Win16 fixture models 16-bit `POINT` fields but does not reproduce Microsoft C 7
segmented pointers or callback code generation. Its `MakeProcInstance` casts suppress
only GCC's corresponding function-cast warning. Microsoft C 7, the Windows 3.1 SDK,
and Windows must validate loader format, segmentation, FPU-emulator dispatch, palette
mapping, and rendered pixels.

## QuickDraw

### Command

```sh
python3 tests/run.py --qd-contract
```

### Coverage

- **Geometry and resources:** all resolutions and the key-1 default, binary resources,
  buffers larger than 64 KiB, palette bytes, quad topology, independent projection
  references, clipping, native polygon/text call arguments, and shared-vertex bounds.
  The monochrome-68000 profile rejects Color QuickDraw, Palette Manager, and GWorld
  calls; polygon recording models automatic pen hiding.
- **CPU and display:** fixed-point/FPU paths and dispatch guards; 1-bit dithering;
  4/8/16/32-bit color; selection among currently configured display depths; old Color
  QuickDraw; small-screen and explicit-size cases; 1×1 viewports and the 32,767-coordinate
  limit. Invalid FPU angles must not leave a stale transformation usable.
- **Buffering and state:** GWorld allocation/locking failures, direct-rendering fallback,
  and recovery. `CopyBits` must use black/white destination colors and restore the
  previous colors. The recorder gives a GWorld its own device when `noNewDevice` is
  absent. Display changes rebuild palettes, patterns, and buffers; OSD state follows
  actual GWorld use.
- **Input and lifecycle:** startup, short drag-poll waits, activation/suspend/resume,
  resize, `TickCount` rollover, animation, window close, Command-Q, and ownership.
  Early drawing and late teardown events must respect readiness guards.
- **Recovery:** unavailable optional palettes/patterns use native color drawing;
  native resource reads return typed failures and empty scenes remain interactive.
  Polygon allocation failures unwind native state before one retry, then pause
  without repeated allocations. A subsequent key resumes drawing after the fault
  clears. Every attempt restores ports/devices and releases temporary ownership.

### Native validation

The Toolbox fixture models 16-bit `Point`/`Rect` fields. MPW, the stated Apple
interfaces/libraries, and System 7 hardware or an appropriate emulator must validate
code resources, A5 globals, executable format, and SANE/68881 dispatch. The FPU helper
is compiled as ordinary host C for numerical checks; only MPW can verify its instruction
selection and separate code segment. Physical palette mapping and QuickDraw hardware
acceleration require the target runtime and display.

## Shared software pipelines

### OpenGL and IRIS GL textures

Either `--ogl-contract` or `--igl-contract` runs the real shared `texsoft.c` renderer
against analytically predicted pixels. Cases cover row-vector transforms, six clipping
planes, depth and culling, perspective interpolation, wrapping, bilinear/trilinear
filtering, mipmap minification, indexed conversion, and cleanup. Invalid matrices,
vertices, and UVs must be rejected before integer conversion; very large finite
repeating UVs remain valid. Raster release retains mipmaps and the indexed lookup
table for re-enabling at a resized viewport.

The same options run `tests/texture.c`, comparing every expanded RGB texel with the
Earth palette source at all five resolutions. Checks cover same-size allocation reuse,
transactional allocation failure, invalid sizes, idempotent cleanup, and dimensions
needed after the staging image is released. Native IRIS GL and XGL recorders verify
direct map-row conversion into packed images/rasters. Allocation-pressure profiles
check smaller texture selection and flat-color recovery after exhausting supported
sizes; retained OpenGL GLU staging remains covered by the OpenGL 1.1 checks.

### GDI and QuickDraw geometry

Both driver contracts link the real integer `soft3d.c` module and exercise quad
batching and Earth-resource traversal. Packets contain at most 64 quads and 130 shared
vertices, with four indices and one palette byte per quad. `DRV_QUAD_BATCH` remains
a capability of `QUAD`; individual quad submission is also checked.

Independent projection cases cover exact coordinates, near/far rejection, collapsed
poles, triangle projection, and whole-polygon signed-area culling. A polygon whose
first rounded fan triangle has the opposite orientation must still be classified
correctly. Large accepted fixed-point rotation angles must match their equivalents
modulo 360 degrees.

Boundary cases clip a triangle spanning 268 million coordinate units to exact
viewport corners, reject reversed winding, and check full 32,767-pixel viewports.
Indices are checked against the packet's actual point count; short-array rejection
must occur before any out-of-bounds read or output modification.
