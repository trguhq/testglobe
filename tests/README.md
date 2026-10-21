Run the geometry regression checks with:

```sh
python3 tests/run.py
```

To include the DOS/Matrox contract checks, provide the original LibShell headers
outside the repository, under `XGX1/HEADERS`:

```sh
python3 tests/run.py --sdk /path/to/sdk
# Or set MATROX_SDK=/path/to/sdk.
```

The tests compile the production geometry, shared driver state, and Matrox driver
as strict C89, with AddressSanitizer and UndefinedBehaviorSanitizer. The Matrox
driver is compiled against the supplied SDK's actual function prototypes. DOS
and board calls are recorded by test-only stubs; the tests erase the compiler's
pointer keywords on the host. Vendor headers are copied into a temporary
directory with the trailing DOS EOF byte removed, and are not committed here.

These tests check geometry, palette/index behavior, command arguments, input,
and cleanup. Visibility checks independently transform outward normals through
the recorded model and inverse-view rotations, and compare every expected fill
and cap count at multiple noncardinal poses in both Matrox resolutions.
They do not validate segmented pointer code generation, DOS timing,
LibShell's binary ABI, or graphics output on a physical PG-641. Those require
Microsoft C 7, `LSCL.LIB`, and the board. Without an SDK path, the Matrox checks
are explicitly reported as skipped.
