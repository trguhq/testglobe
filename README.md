# testglobe

[Website](https://testglobe.dev) | [Repository](https://github.com/trguhq/testglobe/)

testglobe displays a 3D globe through different graphics APIs and hardware. It was
originally conceived as a test for lower-spec systems from the late 1980s and early
1990s, with the first implementation developed on newer hardware for convenience. It may
evolve into a general 3D demo for medium- and high-spec systems.

The demo compares detail from flat-shaded polygons with detail from texture mapping.
Flat shading assigns each triangle or quad a color from the Earth map; texture mapping
samples the map across each polygon. Texture mapping is off by default. Higher globe
resolutions require more memory and rendering time on older systems.

*This version contains a number of AI-generated driver templates for development - only the OpenGL driver is fully hand coded.*

![OpenGL rendering of the Earth](https://github.com/trguhq/testglobe/blob/main/testglobe.png?raw=true)

*OpenGL driver running on macOS (OS X)*

[Video: testglobe on a Sun Ultra 60 with Elite3D-m6 graphics](https://www.youtube.com/watch?v=CffPcvwioUA)

## Contents

- [Current drivers](#current-drivers)
- [Controls](#controls)
- [Binary releases](#binary-releases)
- [Build instructions](#build-instructions)
- [Testing and validation](#testing-and-validation)
- [Planned work](#planned-work)
- [Version history](#version-history)
- [License](#license)

## Current drivers

The Earth maps store 8-bit palette indices. OpenGL and IRIS GL retain those indices in
the mesh and choose RGB(A) or color-index drawing for the selected window format. XGL
expands the data to RGB. Matrox LibShell, Windows GDI, and QuickDraw draw using the
Earth palette.

| Driver | Platform | Default globe | Texture toggle | Origin
| --- | --- | --- | --- |
| [OpenGL](#opengl) | Unix and modern macOS | Key 5: `EARTH_512` | Yes | human |
| [IRIS GL](#iris-gl) | IRIX 4.0, 32-bit MIPS | Key 3: `EARTH_128` | Yes | AI |
| [Sun XGL](#sun-xgl) | Solaris 2.6, 32-bit SPARC | Key 3: `EARTH_128` | Yes | AI |
| [Matrox LibShell](#matrox-libshell) | MS-DOS, PG-641-compatible hardware | Key 1: `EARTH_32` | No | AI |
| [Windows GDI](#windows-gdi) | Windows 3.1, 16-bit | Key 1: `EARTH_32` | No | AI |
| [QuickDraw](#quickdraw) | Classic Macintosh System 7, 68000 | Key 1: `EARTH_32` | No | AI |

## Controls

| Input | Action |
| --- | --- |
| `1`-`5` | Select globe resolution; Matrox LibShell supports only `1` and `2`. |
| Left mouse drag | Rotate the globe. |
| `a` | Toggle automatic rotation / benchmark mode. |
| `o` | Toggle the on-screen display (OSD). |
| `t` | Toggle texture mapping in OpenGL, IRIS GL, and Sun XGL. |
| `Esc` | Exit. |

Keys `1`-`5` select maps 32, 64, 128, 256, and 512 pixels wide, respectively (`EARTH_32`
through `EARTH_512`). The OSD reports quads for the Matrox quad path and triangles for
the triangle and triangle-strip paths.

Visible counts are shown by Matrox LibShell, GDI, and QuickDraw, which count
polygons retained after software culling. OpenGL, IRIS GL, and XGL show total
counts and FPS; their native culling does not return a visibility count.

On displays of 640x480 or smaller, GDI and QuickDraw default to a 320x240 client area.
Larger displays retain the 640x480 default. Explicit size requests are retained, with
QuickDraw clamping its window to the selected screen.

## Binary releases

The [v0.1 release](https://github.com/trguhq/testglobe/releases/tag/v0.1) provides
OpenGL binaries for these platforms:

| Operating system | Architecture |
| --- | --- |
| IRIX | MIPS IV |
| Linux | ARM64 |
| macOS | x86-64 / ARM64 |
| Solaris | SPARCv8 |

## Build instructions

The portable Makefiles use plain macro assignments and explicit rules shared by
traditional Unix/System V make, IRIX/Solaris make, and Microsoft NMAKE. GNU make is not
required. Run the following commands from the repository root or `src`, with the
appropriate platform's compiler and libraries installed:

| Target | Command | Output |
| --- | --- | --- |
| [OpenGL](#opengl) (default) | `make` or `make opengl` | `testglobe-opengl` |
| [Apple OpenGL](#modern-macos-apple-opengl) | `make apple-opengl` | `testglobe-opengl` |
| [IRIS GL](#iris-gl) | `make irisgl` | `testglobe-irisgl` |
| [Sun XGL](#sun-xgl) | `make xgl` | `testglobe-xgl` |
| [Matrox LibShell](#matrox-libshell) | `nmake matrox` | `testglob.exe` |
| [Windows GDI](#windows-gdi) | `nmake gdi` | `testglob.exe` |
| [QuickDraw](#quickdraw) (MPW bridge) | `make quickdraw` | `TestglobeQD` |

Native MPW users build QuickDraw with `Makefile.mpw` as described in its section. The
portable `quickdraw` target requires a configured bridge to the MPW tools.

### Common build settings

Run the appropriate clean target after changing the compiler or build flags; traditional
make does not track command-line changes. Use `make clean` to remove all generated
targets, or `clean-opengl`, `clean-apple-opengl`, `clean-irisgl`, `clean-xgl`,
`clean-matrox`, `clean-gdi`, or `clean-quickdraw` to clean one backend. Under DOS, use
`nmake clean RM=del` or the corresponding individual target with `RM=del`.

The default Unix `SYS_FLAGS` includes `-DNO_SINF` for older math libraries. It selects
standard `sin` and `cos` instead of `sinf` and `cosf`. Apple OpenGL uses its separate
`APPLE_SYS_FLAGS` and the system's modern math library.

<!--
Objects and binaries are placed in the directory where make is run. OpenGL uses ordinary `.o` names, Apple OpenGL uses separate `a_*.o` names, IRIS GL uses `i_*.o`, XGL uses `x_*.o`, DOS uses ordinary `.obj` names, Windows uses `g_*.obj`, and QuickDraw uses `q_*.o` with a separate `TestglobeQD` application. The three Unix binaries coexist; the generic and Apple OpenGL targets share `testglobe-opengl` and relink for the selected build. Each executable target relinks so command-line linker options take effect with traditional make; Matrox and GDI still share an output name and relink for the selected backend. The OSD reports quads for the Matrox quad path and triangles for the triangle and triangle-strip paths.

The Makefile selects `FLAT` with `DRV_OGL`, `DRV_IGL`, or `DRV_XGL`, `SEG86` with `DRV_MTX` or `DRV_GDI` and Microsoft C's `-AH`, or `SEG68` with `DRV_QD` for MPW. Driver headers select their drawing primitive: `drv_ogl.h` defines `TRI`, `drv_igl.h`, `drv_xgl.h`, `drv_gdi.h`, and `drv_qd.h` define `TRISTRIP`, and `drv_mtx.h` defines `QUAD`. Do not supply primitive defines on the compiler command line; conflicting modes are rejected. The OpenGL, IRIS GL, Matrox, GDI, and QuickDraw headers define `DRV_COLORMAP`; indexed-color drawing takes palette bytes, while RGB functions retain RGB color parameters. The GDI header additionally defines `GDI` for its Windows-specific integration; neither `WIN32` nor `SEG86WIN` is needed. On Unix, `make clean` removes all targets' generated files and the obsolete shared `testglobe` binary; under DOS use `nmake clean RM=del`. `clean-opengl`, `clean-apple-opengl`, `clean-irisgl`, `clean-xgl`, `clean-matrox`, `clean-gdi`, and `clean-quickdraw` clean individual backends using the same `RM` setting. Each removal command names one file for compatibility with older DOS versions.

`DRV_COLORMAP` selects palette-byte geometry inputs; `drv_colormapped` reports the window's runtime color mode. OpenGL and IRIS GL therefore retain indexed mesh data in both RGB(A) and color-index windows.

-->

<!--
GDI and QuickDraw share `src/soft3d.c` and `src/soft3d.h` for integer IEEE-float decoding, fixed-point rotation and vertex transforms, near/far and window clipping, perspective projection, and back-face rejection. Their fixed-point caches remain in the drivers. `src/drv.c` also shares the bounded strip traversal and Earth resource ID/chunk traversal; each driver supplies native polygon drawing and resource loading. GDI retains its floating-point implementation, and QuickDraw retains its isolated 68881 helper. Only the GDI and QuickDraw targets compile the integer geometry object (`g_soft.obj` and `q_soft.o`). OpenGL and IRIS GL share the separate floating-point texture rasterizer `src/texsoft.c` and `src/texsoft.h` for formats without native texture mapping; their ordinary rendering remains native.

The root and `src` Makefiles intentionally have the same rules with different source/SDK/MPW directory defaults: historical NMAKE and Solaris make do not share an include syntax. Keep the two files in step when changing rules. For an out-of-tree build, use the ordinary `SRC` macro, for example `make -f /path/to/testglobe/src/Makefile SRC=/path/to/testglobe/src`; no VPATH search behavior is required.

-->

### OpenGL

#### Requirements

GLUT is required. On Debian or Ubuntu, install it with:

```sh
sudo apt-get install freeglut3-dev
```

Archived Solaris GLUT packages are available for
[32-bit SPARC](https://web.archive.org/web/20010726175006/http://reality.sgi.com/opengl/glut3/glut-3.7b.sparc_solaris.tar.gz)
and
[64-bit SPARC](https://web.archive.org/web/20010726192034/http://reality.sgi.com/opengl/glut3/glut-3.7b.sparc_solaris_64.tar.gz).
Modern macOS uses Apple's framework build below.

#### Unix build (`opengl`)

```sh
make opengl
./testglobe-opengl
```

The output is `testglobe-opengl`. The default optimization option is the traditional
Unix `-O`; override `CC`, `CPPFLAGS`, `CFLAGS`, and `LDFLAGS` for the installed compiler
and GLUT libraries.

The default `SYS_FLAGS` also includes `-DNO_GENMIPMAP`, which builds mipmaps with GLU
for older OpenGL implementations. On newer systems, `SYS_FLAGS=-DGL_GLEXT_PROTOTYPES`
enables the `glGenerateMipmap` path.

For Solaris, select the installed compiler and OpenWindows paths as needed:

```sh
make CC=cc CPPFLAGS=-I/usr/openwin/include \
  LDFLAGS="-L/usr/openwin/lib -lglut -lGL -lGLU -lX11 -lXmu -lm"
```

#### Modern macOS (`apple-opengl`)

Use the selected Xcode Command Line Tools and the macOS SDK they provide. Build from the
repository root or `src`:

```sh
make apple-opengl
./testglobe-opengl
```

This target uses the existing OpenGL driver with `FLAT` and `DRV_OGL`. It links Apple's
native frameworks with `-framework GLUT -framework OpenGL -framework Cocoa`; GLU is
included in `OpenGL.framework`, and standard math functions come from the system
runtime. No Homebrew libraries or XQuartz installation are needed. The compiler's native
architecture and SDK defaults are retained, supporting the host's Intel or Apple silicon
toolchain without hard-coded architecture or deployment flags.

`APPLE_CC` defaults to `CC` (`cc`, Apple's Clang with the standard tools selected);
`APPLE_CPPFLAGS`, `APPLE_CFLAGS`, `APPLE_SYS_FLAGS`, and `APPLE_LDFLAGS` can be
overridden on the make command line. `APPLE_CFLAGS` defaults to `CFLAGS`. The default
`APPLE_SYS_FLAGS=-DNO_GENMIPMAP -DGLUT_SILENCE_DEPRECATION` builds mipmaps through GLU
for the legacy OpenGL context used by Apple's GLUT and the driver's fixed-function
rendering, and suppresses GLUT deprecation warnings. The existing Apple header path
selects `<GLUT/glut.h>`.

Apple objects use `a_*.o`, so they are kept separate from the generic OpenGL objects.
Both targets write `testglobe-opengl` and relink when selected. Use
`make clean-apple-opengl` to remove the Apple objects and that binary before changing
compiler flags; `make clean` also removes them.

#### Rendering and color formats

The OpenGL driver probes RGB(A) requests for at least 8/8/8/8, 8/8/8, 5/6/5, 5/5/5,
unconstrained RGB(A), and color-index windows with at least eight bits, with
single/double-buffered and depth-buffered alternatives. It ranks the actual returned
formats, preferring RGB(A) over color index and greater RGB-plus-alpha precision before
buffering. This selects a supported 32-bit RGBA format ahead of 24-bit RGB. It keeps the
original Earth palette index for each flat-shaded triangle, converting it to RGB for an
RGB(A) window or submitting it directly in a color-index window. Color-index texturing
uses the shared software texture rasterizer because classic OpenGL texture mapping
requires RGB(A) mode.

GLUT's display-mode availability query is required. Where display-string requests are
supported, the driver requests those component sizes explicitly; older GLUT
implementations can expose fewer distinct choices. These probes do not enumerate every
native visual.

#### Validation

Host format/palette contracts are available with `python3 tests/run.py --ogl-contract`;
physical color-index behavior still requires an implementation offering that visual.

Native macOS SDK compilation and rendering must be checked on macOS; the host build-rule
and OpenGL contract checks do not provide that validation.

### IRIS GL

#### Requirements

The `irisgl` target is for a native 32-bit MIPS IRIX 4.0 system with the IRIS GL
development headers and runtime. It uses IRIS GL windowing, input, and rendering without
GLUT, OpenGL, or XGL libraries.

#### Build

```sh
make irisgl
```

The output is `testglobe-irisgl`. `IGL_CC` defaults to `CC`, and `IGL_CPPFLAGS`,
`IGL_CFLAGS`, `IGL_SYS_FLAGS`, and `IGL_LDFLAGS` can be overridden for the installed SGI
compiler and libraries. The default flags select `FLAT`, `DRV_IGL`, and standard
`sin`/`cos` through `NO_SINF`. The default link libraries, `-lc_s -lgl_s -lm`, follow
the supplied IRIS GL Programming Guide's IRIX shared-library example. No later MIPS ISA
or ABI flag is imposed on the IRIX 4.0 compiler.

#### Rendering and color formats

The driver starts at `EARTH_128` (key 3). Keys 1-5 still select `EARTH_32`, `EARTH_64`,
`EARTH_128`, `EARTH_256`, and `EARTH_512`, with texture mapping off until `t` is
pressed. Native IRIS GL handles the model/view transforms, 45-degree perspective,
clipping, filled triangle meshes, back-face rejection, and bitmap-font OSD. Geometry is
cached in a native GL object and rebuilt when the resolution or texture mode changes.
IRIS GL's event queue supplies keyboard, mouse drag, resize, focus, and close events;
`a` toggles automatic rotation, `o` the OSD, and Escape exits.

Each interior latitude band is submitted with `bgntmesh`/`endtmesh`, reusing vertices
between adjoining triangles in the graphics engine. A band contains 258 vertices and 256
triangles at the default resolution, or up to 1,026 vertices and 1,024 triangles at
`EARTH_512`. Polar caps use three-vertex meshes to preserve the original nondegenerate
triangles, winding, and UV coordinates. Both triangles of each patch retain its original
Earth palette byte. RGB(A) and native color-index windows with at least eight bits
remain supported.

Runtime capability queries compare single- and double-buffered RGB(A) channel counts,
preferring the greater total including reported alpha bits, then alpha and RGB
precision, then double buffering. Thus a reported 32-bit RGBA format is preferred to
24-bit RGB. If RGB is unavailable, the driver chooses the largest color-index mode with
at least eight bits. The original Earth index is retained for flat-shaded triangles,
converting to RGB(A) when needed. In color-index mode the Earth palette is installed and
restored at cleanup. A native z-buffer is used when present. The globe's convex front
surface also permits native drawing without a z-buffer, rather than requiring a general
intersecting-geometry renderer.

When native texture mapping is available, `texdef2d`, `t2f`, and the GL texture
environment provide texturing with native mipmaps and bilinear filtering. On systems
without it, each strip is expanded into its original alternating triangles for the
shared `src/texsoft.c` rasterizer, which supplies perspective-correct texture
interpolation, homogeneous clipping, depth testing, repeated UVs, and filtered mipmaps,
then presents the resulting pixel raster through IRIS GL. Native transforms still
provide the matrices, and ordinary flat drawing stays native. The OSD identifies whether
enabled texturing is native or software. OpenGL links this floating-point fallback as
`texsoft.o`, and IRIS GL as `i_soft.o`; the segmented GDI/QuickDraw integer module
remains separate.

#### Validation

Run the host IRIS GL contract and software-pixel checks with
`python3 tests/run.py --igl-contract`; see [tests/README.md](tests/README.md). Native
IRIX compilation and physical graphics validation require the original SGI development
tools and runtime. Host call-contract tests and Makefile graph checks do not replace
them.

### Sun XGL

#### Requirements

The XGL target is intended for native 32-bit SPARC Solaris 2.6 with the Sun XGL 3.x
SDK/runtime and X11/OpenWindows. It uses Xlib directly, without XView or GLUT. The
window requires a 24-bit TrueColor visual.

#### Build

```sh
make xgl
# For nonstandard Sun SDK locations:
make xgl XGLHOME=/path/to/xgl OPENWINHOME=/path/to/openwin
```

The default locations are `/opt/SUNWits/Graphics-sw/xgl` and `/usr/openwin`.
`XGL_CPPFLAGS`, `XGL_CFLAGS`, `XGL_SYS_FLAGS`, and `XGL_LDFLAGS` can be overridden for
the installed compiler and libraries. The native Solaris/SPARC compiler's 32-bit default
is used. When relocating the runtime, also set the shell's `XGLHOME` as required by the
installed XGL pipeline and font files.

#### Rendering and input

The driver submits each interior latitude band to `xgl_triangle_strip`: up to 1,026
vertices and 1,024 facet colors per call at `EARTH_512`. Alternating row vertices
reproduce the individual-triangle path's winding; both triangles of each globe patch
retain the same flat color. Polar caps use the original nondegenerate three-vertex
triangles as short strips, preserving their per-triangle UVs and avoiding undefined
normals from duplicate pole vertices. Model/view/projection transforms, clipping,
back-face handling, depth, and texture mapping use XGL. The `t` key enables the native
texture-map/MipMap path; the XGL runtime provides its software pipeline where hardware
cannot accelerate texturing, as described in Sun's programmer's guide. No Mesa
replacement is needed.

X11 supplies keyboard, button, drag, expose, resize, and window-close events. The driver
starts at `EARTH_128` (key 3), matching IRIS GL; keys 1-5 still select `EARTH_32`,
`EARTH_64`, `EARTH_128`, `EARTH_256`, and `EARTH_512`.

#### Validation

Native SDK compilation and physical XGL rendering require the Sun SDK/runtime; host
tests using manual-derived XGL contracts do not replace that validation.

### Matrox LibShell

#### Requirements

The Matrox LibShell target requires MS-DOS, Microsoft C 7.0 with its huge-model runtime
and NMAKE, and the Matrox LibShell C binding library matching that compiler/model.

#### Build

Put the SDK at the repository root:

```text
XGX1/HEADERS/DEF.H
XGX1/HEADERS/LIBFN.H
XGX1/HEADERS/IOLIB.H
libsv5/lscl.lib
```

```text
nmake matrox
```

With the compiler and linker on `PATH`, run `nmake matrox`. An external SDK location can
be selected with `nmake matrox XGX_ROOT=C:\MATROX`; `MTX_CC`, `MTX_CFLAGS`,
`MTX_CPPFLAGS`, `MTX_LIB`, and `MTX_LDFLAGS` are overridable. Keep `-AH` when overriding
compiler flags. `XGX_ROOT` defaults to `.` in the root Makefile and `..` in
`src/Makefile`. DOS rules use native backslash paths. Compiler/linker response files
avoid the DOS command-line length limit; generated DOS names fit 8.3. No GNU make
functions, conditional directives, pattern rules, or shell directory commands are used.

#### Rendering and input

The driver assumes a 640x480, 256-color PG-641-compatible LibShell display. Initialize
the board and its LibShell firmware according to the SDK before starting `testglob.exe`.
Load a Microsoft-compatible DOS mouse driver for mouse dragging; keyboard controls work
without a mouse. The default globe is `EARTH_32` (key 1), and key 2 selects `EARTH_64`.
Other globe sizes and texture mapping are excluded from this build, including its help
and status text.

LibShell performs the model/view transforms, orthographic projection, viewport clipping,
and filled world-coordinate polygon drawing through `v3arwplygn` (`V3ARWPLYGN`). Each
ordinary patch is a quad; coincident vertices at the poles are collapsed to a triangle.
The host rejects back-facing polygons of the convex globe because the base PG-641
interface does not document a depth buffer or back-face switch. The original palette
index colors each polygon; all 256 LUT entries come directly from `earth_cmap`. `SEG86`
uses normalized huge pointers, `_halloc`/`_hfree`, long mesh counts, and DOS-compatible
timing rather than POSIX calls. The SDK and Microsoft tools are external dependencies
and are not redistributed here.

#### Validation

Host regression checks are available with `python3 tests/run.py`. To also check the
Matrox driver against the original SDK declarations, run
`python3 tests/run.py --sdk /path/to/sdk`; see [tests/README.md](tests/README.md). The
tests record DOS/LibShell calls and use sanitizers; they do not emulate segmented code
generation or the board. Native validation requires building with Microsoft C 7 and
running on the Matrox hardware.

### Windows GDI

#### Requirements

The GDI target is a native 16-bit Windows 3.1 program, not Win32 or Win32s. It requires
Microsoft C/C++ 7.0 with its Windows emulation runtime (`LLIBCEW.LIB`), and the Windows
3.1 SDK headers, import library (`LIBW.LIB`), 16-bit linker, and resource compiler.

#### Build

Configure the tools' `PATH`, `INCLUDE`, and `LIB` environment variables, then run:

```text
nmake gdi
```

`GDI_CC`, `GDI_LINK`, `GDI_RC`, `GDI_CFLAGS`, `GDI_CPPFLAGS`, `GDI_LIBS`, `GDI_LDFLAGS`,
`GDI_RCFLAGS`, and `GDI_DATA` are overridable. The supplied `.def` file exports the
window procedure and specifies Windows segment attributes, an 8 KiB local heap, and a 16
KiB stack. The build compiles `src/gdi.rc` and all 13 `data/*.bin` files into
`g_earth.res` with `RC -r -fo`, then attaches it with `RC -t g_earth.res testglob.exe`.
These discardable `RCDATA` resources hold the Earth palette indices in chunks of at most
16 KiB, keeping large arrays out of Win16 static data segments. The binary data files
must retain their original bytes. `GDI_DATA` defaults to `data` in the root Makefile and
`..\data` in `src/Makefile`. The build uses DOS response files and separate objects from
Matrox; both targets write `testglob.exe`, relinking it for the selected driver. The
huge-model application permits one running instance at a time.

#### Rendering and input

Run the resulting `testglob.exe` inside Windows 3.1 configured for a 256-color (8-bit
paletted) display. GDI windows share the display's color depth; the driver checks the
display instead of claiming to create an independent 8-bit visual. It creates a
256-entry logical palette from `earth_cmap`, realizes the palette on activation, and
shades polygons with the original palette indices. Windows retains its reserved system
palette colors. Rendering uses an 8-bit compatible bitmap and memory DC before copying
the finished frame to the window.

The default is `EARTH_32` (key 1). Keys 1-5 select `EARTH_32`, `EARTH_64`, `EARTH_128`,
`EARTH_256`, and `EARTH_512`; texture mapping remains excluded, including from the OSD
and keyboard controls. GDI fills color-grouped triangle polygons with `PolyPolygon`;
latitude strips supply the same nondegenerate triangles as the other triangle paths.
Model/view rotation, perspective projection, clipping, and back-face rejection use the
shared integer pipeline or the driver's floating-point path. Visibility relies on the
convex globe's front surface rather than a general-purpose depth buffer for intersecting
geometry. Mouse dragging, automatic rotation, resize, palette changes, and window
closure use the native Windows message loop.

The driver detects a math coprocessor with `GetWinFlags`; supported 80387-or-later
systems use floating-point transforms, while other systems use fixed-point transforms
and projection. Mesh generation and common FPS bookkeeping still use floating point.
Keep `-FPi`: the Microsoft Windows runtime uses an available coprocessor and emulates
these remaining operations otherwise. `-FPi87` would incorrectly require a coprocessor.
Windows prologs (`-Gw`) and the huge model (`-AH`) preserve Windows selector handling
and cross-segment mesh arithmetic; Windows global-memory allocation supplies blocks
larger than 64 KiB. The segmented pointer model is shared with DOS, while Windows
allocation, startup, errors, and timing are selected with `GDI`.

#### Validation

Native Microsoft C 7 compilation, segmented code generation, and Windows 3.1 display
behavior require validation with those external tools and runtime; host contract tests
do not substitute for it.

### QuickDraw

#### Requirements

The QuickDraw driver targets a System 7 application built with Apple's 68K MPW C
compiler and Macintosh interfaces. Use the System 7-compatible Universal Interfaces 2.2
headers and Rez templates, the matching MPW C libraries, and the `C`, `Link`, `Rez`, and
`Make` tools. Set MPW's `CIncludes`, `RIncludes`, `Libraries`, and `CLibraries` to the
installed interface/library folders. These SDK files and tools are external dependencies
and are not redistributed here.

#### Build and transfer

`Makefile.mpw` is a separate, native MPW Make file: MPW's rule syntax, variables, and
shell differ from Solaris make and Microsoft NMAKE. It is stored in MacRoman with
classic CR line endings; preserve those bytes. Transfer the ASCII C/header/Rez sources
as Macintosh text, converting their Unix line endings to CR in the build copy (for
example, `unix2mac src/*.c src/*.h src/*.r` before transfer). Do not convert
`Makefile.mpw` from UTF-8; it is already in its native encoding.

In MPW Shell, set the current directory to the repository root and run:

```text
Make -f Makefile.mpw quickdraw > QDBuild
Execute QDBuild
```

MPW Make emits commands rather than executing them; the second line runs the generated
commands. From `src`, use `Make -f ::Makefile.mpw -d QD_SRC=: quickdraw > QDBuild`, then
`Execute QDBuild`. Build output is `TestglobeQD`, a Finder-launchable `APPL` with
creator `Tglo`. Preserve its resource fork when transferring it to another Mac, using a
resource-aware copy or a MacBinary/BinHex archive.

The build needs the C/header files in `src`, `src/qd.r`, `Makefile.mpw`, and all 13
`data/*.bin` files. The binary files contain the original Earth palette indices in
chunks of at most 16 KiB; transfer them as binary without newline conversion. Rez embeds
them as `EART` resources with IDs from `src/earthres.h`, keeping large initialized
arrays out of the A5-relative globals. Keep `data` beside `src`: the Rez `read`
statements use explicit source-relative `::data:` paths, since native MPW Rez does not
use its `-i` header search paths for these binary reads.

The portable `make quickdraw` target can use the external
[MPW tool/shell bridge](https://github.com/ksherlock/mpw-shell) with a configured MPW
installation: `MPW_EXEC` defaults to its `mpw-make` executable. It invokes the same
native file and passes `QD_DEFINES` (`-d SEG68 -d DRV_QD -d DRV -d NO_SINF`) and the
source directory; `MPW_EXEC`, `QD_MAKEFILE`, `QD_SOURCE`, and `QD_DEFINES` are
overridable. Native MPW builds need no bridge or modern scripting language. Use
`Make -f Makefile.mpw clean > QDBuild` and `Execute QDBuild` for native cleanup. The
portable `clean-quickdraw` target and ordinary `make clean` remove the seven QuickDraw
objects and application using `RM`, without requiring MPW tools.

#### Runtime and resources

The native build compiles `main.c`, `drv.c`, `err.c`, `globe.c`, `drv_qd.c`, and the
integer-only `soft3d.c` for the baseline 68000 with `SEG68` and `DRV_QD`. MPW's default
SANE floating-point code remains safe without a coprocessor. Only `qd_fpu.c` receives
`-mc68020 -mc68881`; its pointer-only API and separate `QDFPU` code segment avoid mixing
SANE and hardware floating-point calling conventions. The driver calls it only after
detecting both a suitable processor and FPU. `qd_math.h` supplies that internal
interface. Do not enable direct FPU flags for the baseline objects. The link includes
the MPW C startup/runtime, interface, standard C, `Math.o`, and `CSANELib.o` libraries.
`QD_RUNTIME` defaults to `{Libraries}Runtime.o`; override it and `QD_LIBRARIES` if the
installed MPW release uses different runtime filenames.

`src/qd.r` supplies the `SIZE` resource (8 MiB preferred and 512 KiB minimum application
partitions, suspend/resume handling, and clean 24/32-bit addressing) and the error
alert's `ALRT`/`DITL` resources. Higher globe resolutions and a large GWorld need a
larger application partition; the minimum permits the default low-resolution globe on
smaller systems. Windows and menus are created by the driver. The classic A5-relative
globals and segmented `CODE` model are distinct from `SEG86`; heap mesh allocation and
TickCount timing are selected under `SEG68`. The default globe is `EARTH_32` (key 1).
Keys 1-5 select `EARTH_32`, `EARTH_64`, `EARTH_128`, `EARTH_256`, and `EARTH_512`;
texture mapping remains excluded.

#### Rendering and color formats

The driver uses native QuickDraw polygon/text drawing. It detects monochrome versus
Color QuickDraw and uses the selected display's current maximum drawing depth without
changing the desktop's mode. At 1 bit it approximates Earth colors with ordered
luminance patterns; at 4/8 bits QuickDraw maps RGB colors to the device palette, and
16/32-bit displays use their native RGB drawing. On systems with 32-bit QuickDraw, an
optional GWorld provides an offscreen back buffer and `CopyBits` presents it. This is a
useful application of GWorld pixel memory, rather than a separate kind of hardware RAM.
QuickDraw chooses available display acceleration; the program does not access
undocumented video memory. If an offscreen world cannot be allocated or its pixels
cannot be locked, drawing falls back to the window. The OSD appends `GWorld on` or
`GWorld off` after its other statistics, reflecting the actual rendering path.

#### Validation

Host tests check call contracts, geometry, and simulated devices; native MPW
compilation, code-resource linkage, FPU dispatch, physical depth/palette behavior, and
System 7 rendering still require the actual tools and Macintosh runtime.

## Testing and validation

See the [host regression guide](tests/README.md) for prerequisites, driver-specific
commands, test coverage, and native validation limits. The basic geometry checks run
with:

```sh
python3 tests/run.py
```

These checks exercise production code with host call recorders and sanitizers. They do
not replace native compilation or rendering checks with the original SDKs, operating
systems, and hardware.

## Planned work

- Elevation.
- Real-time sunlight.
- Real-time clouds, if feasible.
- Shadows.
- City lights.
- A one-button benchmark.
- A WebGL driver, requiring a different language.
- An IBM PGC or compatible driver; performance may be limiting.
- Other possible backends: IBM Adapter Interface, VBE/AF, and the Amiga chipset.

## Version history

### v0.2 (in development)

- Added AI-generated driver code for Matrox LibShell, Windows GDI, QuickDraw, Sun XGL, and IRIS GL.
- Added native macOS framework build rules for OpenGL.
- Added an FPS counter and automatic rotation.
- Changed comments to C style.
- Fixed the Mesa bitmap-text issue by disabling texture mapping while drawing the OpenGL OSD.

### [v0.1](https://github.com/trguhq/testglobe/releases/tag/v0.1)

First release, with OpenGL/GLUT and binaries for several platforms.

## License

GNU General Public License, version 3 or later. See [LICENSE](LICENSE).
