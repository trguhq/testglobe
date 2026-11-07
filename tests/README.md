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
| [`--amiga-ogl-contract`](#amiga-opengl) | MiniGL submissions and Amiga window/context lifecycle | None |
| [`--igl-contract`](#iris-gl) | IRIS GL calls and software texture pixels | None |
| [`--xgl-contract`](#sun-xgl) | XGL calls with real X11 windows and events | X11 development files, Xvfb, and `xvfb-run` |
| [`--sdk /path/to/sdk`](#matrox-libshell) | DOS/Matrox calls against original SDK declarations | LibShell headers under `XGX1/HEADERS` |
| [`--tiga-contract`](#tiga) | TIGA calls, embedded-module loading, arithmetic, and recovery | None |
| [`--tiga-sdk /path/to/sdk`](#tiga) | TIGA checks plus compilation against original SDK declarations | TIGA host headers |
| [`--dos-cpu`](#tiga) | Shared Matrox/TIGA CPU-probe instruction and stack checks (`--tiga-cpu` remains an alias) | Python `unicorn`, GNU `as` and `objcopy` |
| [`--gdi-contract`](#windows-gdi) | Win16 startup, GDI calls, and messages | None |
| [`--qd-contract`](#quickdraw) | System 7 startup, Toolbox calls, and events | None |
| [`--ami-contract`](#amiga) | Amiga window/fullscreen lifecycle, native/RTG graphics calls, events, and executable resources | None |

With all prerequisites installed, run every native group with:

```sh
python3 tests/run.py --ogl-contract --igl-contract --xgl-contract \
  --gdi-contract --qd-contract --ami-contract --amiga-ogl-contract \
  --tiga-contract --dos-cpu --tiga-sdk /path/to/tiga --sdk /path/to/matrox
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

Most legacy API declarations come from test fixtures; Matrox and the optional TIGA
SDK check use supplied vendor headers. Call recorders check submitted arguments and
simulated outcomes. They do not implement the original graphics libraries, establish
native binary ABIs, or
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
the 16 KiB per-array limit and inclusion of only `EARTH_32` and `EARTH_64` in Matrox
and TIGA builds.
`tests/earth_reference.h` reads the binaries for expected colors; the application
does not use that test-only file I/O.

`tests/ami_pack.py` checks the Amiga packer without an SDK. It compares all embedded
chunks and resource IDs with the canonical data, preserves loadable hunks and unrelated
debug records, and verifies that repacking does not duplicate maps. Malformed hunks,
incorrect map lengths, and input/output aliases must fail without replacing a valid
output. These checks run in the baseline suite and can also be run directly:

```sh
python3 tests/ami_pack.py
```

The generated classic Workbench tool icon is checked for its file structure and
32 KiB stack request.

### Geometry and shared interfaces

Shared palette helpers are checked in both the flat and segmented helper builds.
Tests compare small-palette mixtures with an exhaustive reference, require larger
palettes to improve on or equal the previous nearest-two method, and verify exact
colors, nearest solid entries, invalid inputs, and all ordered-pattern levels.

- OpenGL indexed strips and separate pole triangles, XGL RGB strips, IRIS GL indexed
  strips, and GDI/QuickDraw quad batches are checked at all five resolutions. Checks
  compare primitive tuples, palette bytes, outward winding, pole geometry, shared
  vertices, packet and UV seams, and texture dispatch where supported.
- `tests/quad_batches.c` checks individual and batched traversal for GDI and QuickDraw.
  At key 5 it verifies 2,048 packets and 131,072 quad patches, with at most 130 vertices
  per packet. Scoped sequence/row metadata must identify adjoining bands, disappear
  after the callback, and reset on a new tile or interrupted traversal. EARTH_32
  permits 561 new transforms instead of 1,056. Driver defaults are checked alongside
  resolution changes.
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
Elapsed-clock checks include backward adjustments without a long freeze. Quad stream
helpers validate consecutive successful packets and explicit resets; strip-range
checks preserve winding independently of native color/UV packing.
Configuration checks cover platform and architecture defaults, conflicting settings,
Amiga's FLAT/no-texture configuration, and explicit errors for unsupported legacy drivers.

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
  A failed base-level texture upload must be checked before mipmap generation so an
  out-of-memory error still selects a smaller image without changing globe detail.

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
  Base-image uploads and bilinear minification require no later GL entry points.
  Allocation,
  compilation, validation, and execution failures must delete bad lists and draw
  complete immediate geometry without retrying failed caches every frame. Texture
  creation, binding, upload, and transfer failures must release failed state, draw
  flat colors, and allow off/on retry.
  Native and software allocation-pressure profiles select smaller usable images;
  non-memory upload errors stop retries. Invalid or undersized texture limits fail
  without uploading. Both minification and magnification stay bilinear across toggles.
  Failed traversal with OSD disabled and autorotation enabled must show diagnostics
  without self-scheduling another failed frame; explicit input can retry.
- **Window lifecycle and controls:** early probe and window redraws verify a safe
  display callback before rendering is ready, including Apple's GLUT ordering.
  Checks also cover probe cleanup, final size, FPS toggling, right-button metadata,
  OSD status/help order, and omission of unavailable visibility counts.
- **Software texturing:** OpenGL adopts only the base image and samples it bilinearly,
  including strongly minified triangles; IRIS GL retains trilinear mipmaps.
  OpenGL presents the rasterizer's index bytes directly;
  transferred bytes match every framebuffer index, including on 64-bit hosts.
  Nondefault unpack/transfer state must be restored.
  Software texturing bypasses display lists, and both raster-cache policies are
  exercised. Numerical pixel checks are described under [Shared software pipelines](#shared-software-pipelines).

### Native validation

Actual OpenGL rendering and availability of color-index visuals require GUI checks
with the installed GL implementation. Apple SDK compilation and Cocoa/GLUT event-loop
behavior require macOS; the host recorder checks callback ordering without implementing
that event loop.

## Amiga OpenGL

### Command

```sh
python3 tests/run.py --amiga-ogl-contract
```

The host recorders need neither VBCC nor the MiniGL SDK. Native cross-compilation
uses the [Amiga OpenGL build instructions](../README.md#amiga-opengl).

### Coverage

- **MiniGL geometry:** the recorder models one flat color per native submission.
  Tests compare facet colors, winding, and UVs across all five globe levels, the
  longest latitude strips, odd-start color runs, and pole triangles. Submissions
  must remain within the configured native vertex-buffer limit.
- **Material and caching:** texture creation and failure status, context recreation
  and reupload, textured display lists, and flat-colored list bypass. Native OSD
  text must follow presentation so it remains visible.
- **Amiga integration:** runtime library/dispatch validation, window creation,
  keyboard and mouse events, resize/recovery, and context/bitmap/window/library
  ownership. Presentation bounds follow the allocated RTG bitmap even if the
  window dimensions change before resize handling.

### Native validation

The recorder validates application logic and API arguments, including selected
PiStorm3D limitations; it does not implement MiniGL or a VideoCore GPU. Compile and
link with the matched V29/ABI 5 SDK using VBCC separately. Actual PiStorm3D execution
must verify shared-library calling conventions, rendering, RTG presentation,
window resize, and GPU resource recovery.

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
  drawing; both identifier-allocation and newly compiled-object validation failures
  are latched to avoid repeated failed work. Previously valid objects that disappear
  can be rebuilt. Failed traversal forces diagnostics with OSD disabled and suspends
  automatic retries until input requests another attempt.
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
- **OSD layout:** native extent queries with proportional widths and negative bearings,
  wrapped status/help at small sizes, long unbroken words, and tiny-window guards.

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
- **Software transforms:** LibShell is the default; `S`/`s` selects host transforms
  while keeping indexed LibShell polygon filling. An independent orthographic
  reference checks orientation, integer-degree rotation, screen coordinates and
  viewport bounds. CPU profiles exercise baseline, 386 and floating-point dispatch,
  including 8086/8087 and 286/287 combinations. The OSD reports actual selection
  and failed requests; the help keeps `S` visible.
- **Replies and errors:** `LSINQ` word counts and payload ordering; short malformed
  replies are drained before failure, while replies longer than 16 words abandon the
  connection. Whole-reply deadlines apply even when words remain ready. Timeouts and
  oversized replies must prevent further board commands during cleanup.
- **Error policy and recovery:** initially enabled and disabled reporting policies;
  opcode, parameter, board-memory, and overflow diagnostics; multiple errors while
  awaiting completion; palette and policy restoration while communication is usable.
  Recoverable native 3D-packet failures retry through host transforms. Shared 2D
  drawing errors and communication failures retain their failure handling.
- **Input:** keyboard restrictions and toggles, INT 33h polling, relative motion,
  left-button dragging, and right-button redraws.

### Native validation

Microsoft C 7, `LSCL.LIB`, and a PG-641-compatible board are needed to validate huge
pointers, DOS timing, the LibShell ABI, and graphics output. The recorder assumes
nonblocking error-register reads and that positive `rdfifocnt()` permits a complete
word read. The supplied SDK documentation does not establish FIFO count units or
atomic availability; the installed library and board must validate those assumptions
and asynchronous timing.

## TIGA

### Commands

```sh
python3 tests/run.py --tiga-contract
# Or run only the executable packer checks:
python3 tests/tiga_pack.py
```

The driver recorder, executable packer, and module arithmetic tests require no
vendor SDK. To also compile production units against the original TIGA headers, use:

```sh
python3 tests/run.py --tiga-sdk /path/to/TIGA
# Or set TIGA_SDK=/path/to/TIGA with --tiga-contract.
```

The runner accepts headers directly or under `include`, `INCLUDE`, or `TIGA/INCLUDE`.
It normalizes filename case and DOS EOF bytes in a temporary copy and removes
segmented pointer keywords for host compilation. Vendor headers are not committed.
This checks declarations, not the native calling convention.

The separate CPU instruction check needs the optional Python `unicorn` package and
GNU `as` and `objcopy`:

```sh
python3 tests/run.py --dos-cpu
# Or run only the instruction check:
python3 tests/dos_cpu.py
```

### Coverage

- **Mode and palette:** query-based selection of 640 × 480, eight-bit programmable
  256-color modes, preference for two display pages, exact Earth palette uploads,
  and restoration of available prior state. Missing drivers, old TIGA versions,
  invalid modes, and failed primitive-library initialization must clean up safely.
- **Geometry:** both Earth resolutions, bounded shared-vertex packets, clockwise
  polygon submission, rounded-polygon fallback, and visibility counts. Host integer
  and TI-module paths must submit identical coordinates and colors. Small command
  buffers force bounded packet splitting rather than oversized transfers.
- **Transforms and controls:** software 3D is the default; `S`/`s` switches the
  actual transform path and OSD status. CPU/FPU profiles check dispatch, including
  8086/8087, 286/287 and 386/387 combinations, with mouse
  absence and DOS mouse/keyboard input covered independently. Missing embedded
  payloads, incorrect protocol versions, rejected packets, and invalid returned
  coordinates must restore usable host rendering.
- **Presentation and failure:** single-page fallback, synchronization before page
  flipping, safe page reuse, page-wait deadlines, communication errors, and cleanup.
  The recorder rejects drawing into a page that is still displayed in double-buffered
  profiles.
- **Executable packing:** `tests/tiga_pack.py` checks unchanged DOS headers and
  image bytes, exact module bytes and CRC, 64 KiB boundaries through the 1 MiB
  payload limit, software-only copying, and repeated builds. Invalid inputs, file
  aliases, and occupied `.tmp`/`.bak` paths must preserve existing output.
- **Embedded loading:** independent Python MZ/trailer fixtures feed real driver file
  I/O. Only the executable remains in the distribution directory, which may be
  read-only; changing the working directory after startup must still load it. The
  TIGA recorder verifies exact extracted bytes and deletion after loading or failure.
  Missing, truncated, or corrupt payloads, invalid bounds, unavailable temporary
  storage, disk-full and close failures must retain software rendering. Short writes,
  exclusive-name collisions, and deferred-deletion retries are also covered.
  Loader arguments discard unused exports while preserving the plain extraction path.
  Checked `flush_module()` failure retains module ownership for reuse or retry; the
  recorder distinguishes it from the reserved internal deletion API and rejects
  further board calls after a communication failure.
- **TI arithmetic:** `tests/tiga_module.c` calls the production module entry and
  compares its fixed-point camera coordinates and projections with the host pipeline
  over varied poses, one- and 130-vertex packets, and viewport boundaries. Malformed
  packet fields, out-of-range matrices, and invalid coordinate bits must be rejected.

`tests/dos_cpu.py` assembles the shared `drv_cpu.c` inline probe instructions and executes 24
FLAGS/stack cases. Hooks supply the older processors' FLAGS restrictions, which
Unicorn does not model directly. Checks cover CPU classification, guarded 386
instructions, aligned stack accesses, and restoration of registers and flags. They
do not test the BIOS or x87 probe.

### Native validation

The downloadable module was separately compiled and linked with TI TMS340 tools 5.01
under DOSBox. This checks TI compilation and linking of the TMS34010 module, not
execution on a board. Host recorders execute the module as ordinary C;
they do not implement the TIGA TSR, GSP transport, polygon rasterizer, or display.
Microsoft C 7 host compilation, huge pointers, callback conventions, real CPU/x87
behavior, module loading, palette output, and page-flip synchronization still require
native validation. See the [TIGA build instructions](../README.md#tiga).

## Windows GDI

### Command

```sh
python3 tests/run.py --gdi-contract
```

### Coverage

- **Geometry and resources:** all resolutions, binary resource contents, large global
  heaps, exact palette bytes, quad topology, independent double-precision projection
  references, clipping, and shared-vertex packets. `PolyPolygon` calls are bounded to
  64 polygons; lock/unlock/release ownership is checked. Individual and streamed
  submissions must produce the same polygon/color multiset across poses and resized
  viewports, including narrow windows that require clipping.
- **CPU and display:** 80387 selection and fixed-point fallback, including a 286/287
  profile; small-screen defaults, explicit dimensions, foreground/background palettes,
  buffering, and wrapped OSD statistics.
- **Palette dithering:** realized palette RGBs, exact solid brushes, two-color DIB
  patterns, cache refreshes after palette changes, and solid fallback when queries
  or pattern creation fail. Packed DIB layout, logical palette indices, pattern
  coverage, and GDI/global-memory ownership are checked.
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
  Individual and streamed submissions must preserve polygon coordinates/colors at
  multiple poses and window sizes, including clipped silhouettes and collapsed poles.
  The monochrome-68000 profile rejects Color QuickDraw, Palette Manager, and GWorld
  calls. The recorder validates the reusable native polygon record, its bounds,
  closure, and point coordinates rather than simulating `OpenPoly()` recording.
- **CPU and display:** fixed-point/FPU paths and dispatch guards; 1-bit dithering;
  4/8/16/32-bit color; selection among currently configured display depths; old Color
  QuickDraw; small-screen and explicit-size cases; 1×1 viewports and the 32,767-coordinate
  limit. Invalid FPU angles must not leave a stale transformation usable.
- **Native color selection:** all 256 original Earth RGB values reach Color
  QuickDraw unchanged. Indexed fills retain native pattern conversion after palette
  changes and palette-allocation failures; monochrome retains 64 luminance steps.
  The recorder checks API inputs, not the Toolbox's internal dithered pixels.
- **Buffering and state:** GWorld allocation/locking failures, direct-rendering fallback,
  and recovery. `CopyBits` must use black/white destination colors and restore the
  previous colors. The recorder gives a GWorld its own device when `noNewDevice` is
  absent. Display changes rebuild palettes, patterns, and buffers; OSD state follows
  actual GWorld use.
- **Input and lifecycle:** startup, short drag-poll waits, activation/suspend/resume,
  resize, `TickCount` rollover, animation, window close, Command-Q, and ownership.
  Early drawing and late teardown events must respect readiness guards.
- **Recovery:** checked polygon allocation and bounded native-scratch preflight
  failures release optional resources before one retry, then pause without repeated
  allocations. Reduced pattern scratch permits nearest-color fallback. A subsequent
  key resumes drawing after the fault clears. Native resource reads return typed
  failures and empty scenes remain interactive; attempts restore ports/devices and
  temporary ownership.

### Native validation

The Toolbox fixture models 16-bit `Point`/`Rect` fields. MPW, the stated Apple
interfaces/libraries, and System 7 hardware or an appropriate emulator must validate
code resources, A5 globals, executable format, and SANE/68881 dispatch. The FPU helper
is compiled as ordinary host C for numerical checks; only MPW can verify its instruction
selection and separate code segment. Physical palette mapping and QuickDraw hardware
acceleration require the target runtime and display.
Native QuickDraw allocation wrappers can invoke system errors, and polygon filling
can silently skip work after internal allocation failure. The fixture tests checked
Memory Manager operations and preflight policy; it cannot establish recovery from
every opaque Toolbox allocation.

## Amiga

### Command

```sh
python3 tests/run.py --ami-contract
```

The host checks use test-local Amiga declarations and need no SDK or VBCC. A native
cross-build uses the [Amiga build instructions](../README.md#amiga) separately.

### Coverage

- **Workbench and color:** OS library versions 34/36/39 with OCS/ECS/AGA profiles,
  window creation, actual client dimensions, native text, and palette-preserving
  drawing at all 1- through 8-bit indexed depths. Palette/display queries are version-guarded
  and HAM is rejected. Native area vertices must remain inside the drawable bitmap;
  patterns and pen indices must stay within their bounds. Small-palette mixtures
  are checked against an exhaustive RGB-error oracle; larger-palette matching must
  be at least as accurate as the previous nearest-two-pen method. Exact colors,
  duplicate pens, short palettes, complementary-color mixtures, and all 17 Bayer
  coverage levels are checked through the production palette and drawing paths.
  `D`/`d` switches between mixtures and independently verified nearest solid pens;
  the setting survives palette refreshes, resizing, and resolution changes. Tests
  check OSD/help, unchanged resource ownership, and unaffected truecolor RTG output.
- **RTG:** optional CyberGraphX/Picasso96 detection, indexed and 15/16/24/32-bit
  screens, opaque bitmap attributes, RGB span bounds and colors, and native fallback
  when RTG libraries are absent. The native-only `AMI_RTG=0` build is checked separately.
  Display-compatible bitmap allocation, direct-drawing fallback, resizing, and cleanup
  must not depend on native chipset limits or planar bitmap fields.
- **Fullscreen:** V39 screen-buffer allocation and presentation, delayed safe/display
  notifications, failed swaps, allocation rollback, and cleanup with pending work.
  Drawing must target an inactive buffer and finish before a swap; fullscreen must
  not copy into the displayed bitmap or discard its buffers during cache recovery.
  `F`/`f` switches presentation and `P`/`p` toggles FPS. Tests cover retained scene
  settings, Workbench window restoration, private palettes, older-OS fallback and
  incompatible RTG buffers.
- **CPU and geometry:** 68000 fixed-point, enhanced integer, and FPU dispatch,
  including a reported FPU without a supporting CPU. Cached and uncached drawing
  must retain polygon coordinates and colors, including projection-cache allocation
  failure; the default streamed globe requires 561 transforms. Resizing and pixel-aspect
  changes rebuild projection state; all five resolution keys remain available.
- **Lifecycle and recovery:** native event input, activation/refresh, close,
  Chip RAM allocation/release, the 32 KiB stack guard, back-buffer failure selecting
  direct drawing, scratch-allocation pause/retry, and bounded recovery from area-fill
  errors. Refresh drawing is deferred outside Intuition's damage-clipping scope;
  invalid display changes can recover when valid dimensions return.
- **Executable data:** the production reader opens a real packed Hunk file through
  CLI, Workbench, program-directory, and old CLI search-path cases. Tests compare
  selected chunks with canonical bytes, enforce bounded reads, reject malformed
  footers/directories, recover from short reads, and verify handle/library ownership.
  The separate baseline packer checks are described under [Earth data](#earth-data).

### Native validation

The recorders check production control flow and arguments, not AmigaOS graphics
implementation, RTG drivers, or the custom chips. Enhanced integer/FPU objects compile
as ordinary host C during these tests, so host results do not prove native opcode selection,
library-vector ABI, or 68000/FPU dispatch. VBCC/NDK cross-builds provide a separate
compiler/linker check. Actual Workbench startup, planar fills, palette patterns,
RTG pixel conversion and acceleration, fullscreen scanout synchronization,
screen-depth changes, pointer colors, blitter completion, Chip RAM pressure, and
performance still need hardware or a suitable emulator on OCS/ECS/AGA and
CyberGraphX/Picasso96 configurations.

## Shared software pipelines

### OpenGL and IRIS GL textures

Either `--ogl-contract` or `--igl-contract` runs the real shared `texsoft.c` renderer
against analytically predicted pixels. Cases cover row-vector transforms, six clipping
planes, depth and culling, perspective interpolation, wrapping, bilinear/trilinear
filtering, mipmap minification, indexed conversion, and cleanup. Invalid matrices,
vertices, and UVs must be rejected before integer conversion; very large finite
repeating UVs remain valid. Raster release retains mipmaps and the indexed lookup
table for re-enabling at a resized viewport.
Byte-index and native-word outputs must agree pixel for pixel. Tests cover ownership
and allocation when dimensions or output formats change, including same-size reuse.

The same options run `tests/texture.c`, comparing every expanded RGB texel with the
Earth palette source at all five resolutions. Checks cover same-size allocation reuse,
transactional allocation failure, invalid sizes, idempotent cleanup, and dimensions
needed after the staging image is released. Native IRIS GL and XGL recorders verify
direct map-row conversion into packed images/rasters. Allocation-pressure profiles
check smaller texture selection and flat-color recovery after exhausting supported
sizes; retained OpenGL GLU staging remains covered by the OpenGL 1.1 checks.

### TIGA, GDI, QuickDraw, and Amiga geometry

The driver contracts link the real integer `soft3d.c` and floating `soft3fp.c` modules
and exercise quad batching. TIGA uses compiled Earth maps; the other three drivers
also exercise Earth-resource traversal. Packets contain at most
64 quads and 130 shared vertices, with four indices and one palette byte per quad.
`DRV_QUAD_BATCH` remains a capability of `QUAD`; individual quad submission is also
checked.

Independent projection cases cover exact coordinates, near/far rejection, collapsed
poles, triangle projection, and whole-polygon signed-area culling. A polygon whose
first rounded fan triangle has the opposite orientation must still be classified
correctly. Large accepted fixed-point rotation angles must match their equivalents
modulo 360 degrees.

Projection-cache checks compare cached and uncached results, validate adjoining-row
reuse, and invalidate coordinates after viewport changes. Depth and viewport fast
paths must retain the existing crossing-polygon fallback and rounded-area culling.
The optimization does not replace complete polygon orientation with the first fan
triangle's sign. Invalid indices and coordinates must still be rejected when caching
is enabled.

Boundary cases clip a triangle spanning 268 million coordinate units to exact
viewport corners, reject reversed winding, and check full 32,767-pixel viewports.
Indices are checked against the packet's actual point count; short-array rejection
must occur before any out-of-bounds read or output modification.
