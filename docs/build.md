# Building

The [WebGL frontend](webgl.md) needs no build. The native Makefiles use plain macro
assignments and explicit rules that work with traditional Unix/System V make,
IRIX and Solaris make, and Microsoft NMAKE; GNU make is not required. Run the
commands below from the repository root or `src`, with the platform's compiler and
libraries installed. Each driver page lists its requirements and build variables.

## Targets

| Target | Command | Output |
| --- | --- | --- |
| [OpenGL](opengl.md) (default) | `make` or `make opengl` | `testglobe-opengl` |
| [Solaris OpenGL](opengl.md#solaris-solaris-opengl) | `make solaris-opengl` | `testglobe-opengl` |
| [Apple OpenGL](opengl.md#modern-macos-apple-opengl) | `make apple-opengl` | `testglobe-opengl` |
| [Amiga OpenGL](opengl.md#amiga-opengl) (VBCC) | `make amiga-opengl` | `testglobe-amiga-opengl` |
| [IRIS GL](irisgl.md) | `make irisgl` | `testglobe-irisgl` |
| [Sun XGL](xgl.md) | `make xgl` | `testglobe-xgl` |
| [Matrox LibShell](matrox.md) | `nmake matrox` | `testglob.exe` |
| [TIGA](tiga.md) | `nmake tiga` | `testglob.exe` |
| [Windows GDI](gdi.md) | `nmake gdi` | `testglob.exe` |
| [QuickDraw](quickdraw.md) (MPW bridge) | `make quickdraw` | `TestglobeQD` |
| [Amiga](amiga.md) (VBCC) | `make amiga` | `testglobe-amiga` |
| [Windows GDI cross-build](#linux-hosted-cross-builds) | `make gdi-cross` | `build/cross/gdi/testglob.exe` |
| [QuickDraw cross-build](#linux-hosted-cross-builds) | `make quickdraw-cross` | `build/cross/quickdraw/TestglobeQD.bin` |
| [TIGA cross-build](#linux-hosted-cross-builds) | `make tiga-cross` | `build/cross/tiga/testglob.exe` |
| [Matrox cross-build](#linux-hosted-cross-builds) | `make matrox-cross` | `build/cross/matrox/testglob.exe` |

Native MPW users build QuickDraw with `Makefile.mpw`, as described in
[QuickDraw](quickdraw.md#build). The portable `quickdraw` target needs a configured
bridge to the MPW tools.

## Common settings

### Output location

Native objects and executables are written to the directory where make runs. For an
out-of-tree build, create a build directory, enter it, and run:

```sh
make -f /path/to/testglobe/src/Makefile SRC=/path/to/testglobe/src
```

Append a target, such as `apple-opengl`. The three desktop OpenGL targets share
`testglobe-opengl`, and Matrox, TIGA, and GDI share `testglob.exe`; selecting a target
relinks its executable. Driver objects have distinct names, so the Unix backends'
objects can coexist.

### Cleaning

Traditional make does not track compiler-option changes. Clean the affected objects
before changing the compiler or build flags:

| Command | Removes |
| --- | --- |
| `make clean` | Native objects, generated build sources, temporary files, and application/helper binaries. |
| `make clean-<target>` | That backend's objects, executable, and intermediates. Shared data sources and helpers generally remain. |
| `make clean-earth-data` | Generated `e_data.c`/`m_data.c`/`t_data.c`, converter binaries, and data-generation temporary files. |
| `make clean-cross` | All four Linux-hosted cross-build directories. |
| `make clean-<target>-cross` | One cross-build directory, for example `make clean-gdi-cross`. |

The per-backend targets are `clean-opengl`, `clean-solaris-opengl`,
`clean-apple-opengl`, `clean-amiga-opengl`, `clean-irisgl`, `clean-xgl`,
`clean-matrox`, `clean-tiga`, `clean-gdi`, `clean-quickdraw`, and `clean-amiga`.
Under DOS, use `nmake` with `RM=del`, for example `nmake clean RM=del`. Cleaning keeps
the supplied `src/e_saved.c`, the canonical binary maps, and recovery `.bak` files.

### Compiler flags and diagnostics

The default Unix `SYS_FLAGS=-DNO_SINF` selects C89 `sin`/`cos` for older libraries;
Apple OpenGL uses its own `APPLE_SYS_FLAGS` and the system math library.

`DEBUG` defaults to `0`. After cleaning, `make opengl CPPFLAGS=-DDEBUG=1` enables
pixel-storage and right-button diagnostics (a System 7 single-button mouse has no
right-button state). Software texture buffers are released when texturing is disabled;
`CPPFLAGS=-DTEXTURE_CACHE_RASTER=1` keeps them.
[Development notes](development.md#shared-rendering-pipelines) describe the cache and
memory policy. Other targets use their own `*_CPPFLAGS` variable, for example
`IGL_CPPFLAGS=-DIGL_ZBUFFER=1` to keep the IRIS GL z-buffer. The Matrox build can
also link Matrox's own LibShell library instead of its built-in binding; see
[Matrox LibShell](matrox.md#build).

## Linux-hosted cross-builds

The `gdi-cross`, `quickdraw-cross`, `tiga-cross`, and `matrox-cross` targets build the
historical executable formats on a modern Linux host. They need Python 3.8 or later
and external cross-compilers. The DOS targets also use a host C compiler (`HOST_CC`,
default `cc`) for Earth-data conversion and executable packing. The original
Microsoft C and MPW targets remain available.

Install [OpenWatcom](https://github.com/open-watcom/open-watcom-v2) for the DOS and
Windows targets and set its installation directory:

```sh
export WATCOM=/path/to/openwatcom
make gdi-cross
make matrox-cross
make tiga-cross CROSS_FLAGS="--tiga-root /path/to/TIGA --tiga-tools /path/to/ti-tools"
```

- **Matrox** needs no SDK. To link Matrox's `LSCL.LIB` instead, pass
  `CROSS_FLAGS="--lscl --xgx-root /path/to/matrox-sdk"`, with `XGX1/HEADERS` and
  `libsv5/lscl.lib` under that root.
- **TIGA** needs `include` and `libs/ai.lib`; the original SDK archive layout is also
  accepted. Install DOSBox on the host and put the TI compiler, assembler, and linker
  together in `--tiga-tools`. The build compiles `tga_gsp.c` and `tga_ext.asm` and
  embeds the module in `testglob.exe`, so no separate RLM is shipped. To reuse an
  already-built matching module, pass `--tiga-module /path/to/tga_gsp.rlm` instead of
  `--tiga-tools`.

SDKs, compilers, and vendor libraries are not included in this repository.

The Watcom DOS builds use huge-model pointers and C stack calling conventions to
match the SDK bindings. Application objects use 8086 instructions, and the 386
transform kernel is compiled separately and guarded by CPU detection. With `--lscl`,
`src/mtx_ow.c` adapts the Microsoft-built LibShell library's runtime calls; that
library needs a 286 or later, which the driver then checks before `lsinit`.
Floating-point emulation stays available without a coprocessor. The per-vertex
objects and the LibShell binding are compiled with `-s`, without stack probes.

Watcom's Windows runtime uses the large memory model. Explicit `TG_HUGE` pointers keep
mesh and resource access normalized across 64 KiB boundaries, with `SEG86` still
enabled. The output is a protected-mode NE application marked for Windows 3.0, with
all Earth resources embedded and loaded on demand.

Install [Retro68](https://github.com/autc04/Retro68) for QuickDraw, including its
Multiversal interfaces, resource tools, and 68000 runtime:

```sh
export RETRO68=/path/to/retro68/toolchain
make quickdraw-cross
```

QuickDraw keeps a 68000 baseline plus separately compiled, runtime-guarded
68020/68881 code. Its MacBinary output carries the application and Earth resources in
one resource-fork-aware file; decode it on the Macintosh before use.

Cross-builds always run in `build/cross/<backend>` under the repository root, whether
make starts in the root or `src`, and each invocation recompiles the selected backend
so stale objects with other options cannot be reused. `CROSS_FLAGS` passes extra
options such as `--build-dir /path/to/builds`, `--host-cc clang`, or
`--dosbox /path/to/dosbox`; `python3 src/cross.py --help` lists them all.
`make clean-cross` removes these directories without touching the
[downloads](../downloads/README.md). Native `make clean` needs neither Python nor the
cross-toolchains.

The packaged cross-builds pass executable-structure and embedded-data checks
(`tests/cross_binary.py`), host API tests, and DOSBox arithmetic checks. Windows and
Macintosh desktop behavior and Matrox/TIGA hardware rendering still need testing on
the target systems.

## Earth map data

Each native application embeds its selected Earth maps, so no map files are needed
at runtime; TIGA also embeds its TI transform module. WebGL fetches the same binary
maps from the site's `data/` directory. Transfer binary files unchanged, and keep
QuickDraw's resource fork when copying its executable.

| File | Role |
| --- | --- |
| `data/*.bin` | Canonical 8-bit Earth palette indices; edit these to change the maps. |
| `src/e_bin.c` | Build-time converter for Unix and DOS. |
| `e_data.c` / `m_data.c` / `t_data.c` | Disposable generated C source for Unix / Matrox DOS / TIGA DOS builds. |
| `src/e_saved.c` | Supplied generated copy for manual builds; normal builds generate their own. |
| `src/e_cmap.h` | Earth color palette. |
| `web/e_cmap.js` | Browser palette, generated from `src/e_cmap.h` by `web/gen_cmap.py`. |
| `src/earthres.h` | Shared chunk size and native resource IDs. |
| `src/gdi.rc` / `src/qd.r` | Embed the binary chunks as Windows `RCDATA` / Macintosh `EART` resources. |
| `src/ami_pack.py` / `src/ami_data.c` | Pack the Amiga maps into the executable and read selected chunks on demand. |

| Map width | Binary inputs | Total bytes |
| --- | --- | --- |
| 32 | `e32.bin` | 512 |
| 64 | `e64.bin` | 2,048 |
| 128 | `e128.bin` | 8,192 |
| 256 | `e256_0.bin`, `e256_1.bin` | 32,768 |
| 512 | `e512_0.bin` through `e512_7.bin` | 131,072 |

OpenGL, IRIS GL, Sun XGL, Matrox, and TIGA compile generated arrays of at most 16 KiB.
Driver `EARTH_*` defines choose the maps in each executable; Matrox and TIGA include
only the 32- and 64-wide maps (2.5 KiB). GDI and QuickDraw embed all five maps
(170.5 KiB) as native resources; Amiga stores them in an executable `HUNK_DEBUG`
record that the OS loader skips. Generated and saved sources are build copies, not
extra runtime allocations. Compiled-data builds read the maps in place; GDI,
QuickDraw, and Amiga load only the selected map's chunks into their final index
buffer, which is 512 bytes at their default `EARTH_32`.

Make regenerates the build source when it is missing or an input changes. `EARTH_DATA`
(Unix) and `MTX_DATA` (both DOS targets) select the input directory, defaulting to
`data` beside `SRC`. `EARTH_CC` and `EARTH_CFLAGS` select the Unix converter's compiler
and options; DOS uses `MTX_CC` for Matrox and `TGA_CC` for TIGA.

| Task | Command |
| --- | --- |
| Refresh the supplied copy after changing the binary maps | `make earth-data` or `nmake earth-data-dos` |
| Remove build copies and converter tools | `make clean-earth-data` or `nmake clean-earth-data RM=del` |

The next application build regenerates removed sources. For a manual build, compile
`src/e_saved.c` instead of the build copy, and do not link both. The supplied copy was
formerly named `src/e_data.c`; update older manual compile commands.

The converter rejects input/output aliases, nonregular destinations, occupied
`.tmp`/`.bak` paths, and Unix symbolic-link outputs. After an interrupted
replacement, keep any `.bak` file until the output is verified or restored. Changing
the chunk size also requires resplitting the binaries and updating the generator and
resource tables; a compile-time guard rejects stale generated readers.
