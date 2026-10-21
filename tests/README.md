Run the OpenGL triangle and XGL triangle-strip geometry regression checks with:

```sh
python3 tests/run.py
```

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
and cleanup. Visibility checks independently transform outward normals through
the recorded model and inverse-view rotations, and compare every expected fill
and cap count at multiple noncardinal poses in both Matrox resolutions.
They do not validate segmented pointer code generation, DOS timing,
LibShell's binary ABI, native XGL rendering, or graphics output on a physical
PG-641. Native Matrox checks require Microsoft C 7, `LSCL.LIB`, and the board;
native XGL checks require Sun's actual SDK/runtime on Solaris. Without an SDK path, the Matrox checks
are explicitly reported as skipped.
