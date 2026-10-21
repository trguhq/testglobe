# testglobe
* [testglobe website](https://testglobe.dev)
* [testglobe repository](https://github.com/trguhq/testglobe/)

testglobe is a cross-platform demo program to display a 3d globe on different APIs and hardware. Its build targets are OpenGL/GLUT, Sun XGL on Solaris, MS-DOS Matrox LibShell, and native 16-bit Windows 3.1 GDI. The Earth data uses 8-bit palette indices: OpenGL and XGL expand these to RGB, while LibShell and GDI draw indexed-color polygons using the Earth palette.

This was originally conceived as a test for lower spec systems (late 80s and early 90s 3D hardware) and was initially implemented on more modern high spec systems for convenience, but it may envolve into a general 3D demo for medium-high spec.

A feature is the ability to compare detail from the number of quads or triangles used on screen vs. detail from hardware texture mapping. Out of the box, the default mode is texture mapping off, highest resolution sphere, which is likely not going to be the most performant on older systems. Note that it still appears texture mapped, however it is all flat shaded single color quads mapped to the texture of earth by the program itself, rather than texture mapping per se through the graphical API and hardware. Adjust as needed per the Interface section below.

![raw image](https://github.com/trguhq/testglobe/blob/main/testglobe.png?raw=true)
*Running on macOS (OS X)*

External: *[Video of testglobe running on Sun Ultra 60 with Elite3D-m6 graphics](https://www.youtube.com/watch?v=CffPcvwioUA)*

## Interface
* keys 1-5 adjust number of faces of globe for OpenGL and XGL; LibShell and GDI support keys 1-2
* mouse spins globe
* a key toggles auto-rotate / benchmark mode
* o key toggles on-screen display
* t key toggles texture mapping for OpenGL and XGL (off by default)
* esc key exits immediately
  
## Planned drivers
* Apple (classic) Macintosh QuickDraw
* IBM PGC and derivatives
* IRIS GL
* PHIGS
* WebGL
* IBM Adapter Interface? VBE/AF? Amiga? others?

## Binary builds
The following binaries are available in the corresponding releases:
* OpenGL / IRIX / MIPSIV: [v0.1](https://github.com/trguhq/testglobe/releases/tag/v0.1)
* OpenGL / Linux / ARM64: [v0.1](https://github.com/trguhq/testglobe/releases/tag/v0.1)
* OpenGL / macOS / AMD64/ARM64: [v0.1](https://github.com/trguhq/testglobe/releases/tag/v0.1)
* OpenGL / Solaris / SPARCv8: [v0.1](https://github.com/trguhq/testglobe/releases/tag/v0.1)

## Build notes
The Makefiles use plain macro assignments and explicit rules shared by traditional Unix/System V make, Solaris make, and Microsoft NMAKE. GNU make is not required. Run from the repository root or `src`: `make` (or `make opengl`) builds the OpenGL `testglobe`; `make xgl` builds the XGL `testglobe`; `nmake matrox` builds the DOS `testglob.exe`; `nmake gdi` builds the Windows 3.1 `testgdi.exe`. The two segmented targets use Microsoft C 7.0. Objects and binaries are placed in the directory where make is run. OpenGL uses ordinary `.o` names, XGL uses `x_*.o`, DOS uses ordinary `.obj` names, and Windows uses `g_*.obj`. Selecting a Unix backend relinks `testglobe` so that switching targets selects the requested driver even when its objects are older than the other backend's executable.

The Makefile selects `FLAT` with `DRV_OGL` or `DRV_XGL`, or `SEG86` with `DRV_MTX` or `DRV_GDI` and Microsoft C's `-AH`. Driver headers select their drawing primitive: `drv_ogl.h` defines `TRI`, `drv_xgl.h` and `drv_gdi.h` define `TRISTRIP`, and `drv_mtx.h` defines `QUAD`. Do not supply primitive defines on the compiler command line; conflicting modes are rejected. The Matrox and GDI headers define `DRV_COLORMAP`; indexed-color drawing takes palette bytes, while RGB functions retain RGB color parameters. The GDI header additionally defines `GDI` for its Windows-specific integration; neither `WIN32` nor `SEG86WIN` is needed. On Unix, `make clean` removes all targets' generated files; under DOS use `nmake clean RM=del`. `clean-opengl`, `clean-xgl`, `clean-matrox`, and `clean-gdi` clean individual backends using the same `RM` setting. Each removal command names one file for compatibility with older DOS versions.

Run the appropriate clean target after changing the compiler or build flags; traditional make does not track command-line changes.

The root and `src` Makefiles intentionally have the same rules with different source/SDK directory defaults: historical NMAKE and Solaris make do not share an include syntax. Keep the two files in step when changing rules. For an out-of-tree build, use the ordinary `SRC` macro, for example `make -f /path/to/testglobe/src/Makefile SRC=/path/to/testglobe/src`; no VPATH search behavior is required.

### cosf and sinf
The default `SYS_FLAGS` includes `-DNO_SINF` for older Unix math libraries. It selects the standard `sin` and `cos` functions instead of `sinf` and `cosf`.
### Classic Mac (QuickDraw)
The files provided here are in Unix format and can't be used directly on Classic Mac without conversion. The utility "dos2unix" can do this conversion, e.g. with its alias "unix2mac".

Symantec Think C++ was used for testing but a cross-compiler like Retro68 (https://github.com/autc04/Retro68) should work.
### DOS
The Matrox target requires MS-DOS, Microsoft C 7.0 with its huge-model runtime and NMAKE, and the Matrox LibShell C binding library matching that compiler/model. Put the SDK at the repository root:

```text
XGX1/HEADERS/DEF.H
XGX1/HEADERS/LIBFN.H
XGX1/HEADERS/IOLIB.H
libsv5/lscl.lib
```

With the compiler and linker on `PATH`, run `nmake matrox`. An external SDK location can be selected with `nmake matrox XGX_ROOT=C:\MATROX`; `MTX_CC`, `MTX_CFLAGS`, `MTX_CPPFLAGS`, `MTX_LIB`, and `MTX_LDFLAGS` are overridable. Keep `-AH` when overriding compiler flags. `XGX_ROOT` defaults to `.` in the root Makefile and `..` in `src/Makefile`. DOS rules use native backslash paths. Compiler/linker response files avoid the DOS command-line length limit; generated DOS names fit 8.3. No GNU make functions, conditional directives, pattern rules, or shell directory commands are used.

The driver assumes a 640x480, 256-color PG-641-compatible LibShell display. Initialize the board and its LibShell firmware according to the SDK before starting `testglob.exe`. Load a Microsoft-compatible DOS mouse driver for mouse dragging; keyboard controls work without a mouse. The default globe is `EARTH_32` (key 1), and key 2 selects `EARTH_64`. Other globe sizes and texture mapping are excluded from this build, including its help and status text.

LibShell performs the model/view transforms, orthographic projection, viewport clipping, and filled world-coordinate polygon drawing through `v3arwplygn` (`V3ARWPLYGN`). Each ordinary patch is a quad; coincident vertices at the poles are collapsed to a triangle. The host rejects back-facing polygons of the convex globe because the base PG-641 interface does not document a depth buffer or back-face switch. The original palette index colors each polygon; all 256 LUT entries come directly from `earth_cmap`. `SEG86` uses normalized huge pointers, `_halloc`/`_hfree`, long mesh counts, and DOS-compatible timing rather than POSIX calls. The SDK and Microsoft tools are external dependencies and are not redistributed here.

Host regression checks are available with `python3 tests/run.py`. To also check the Matrox driver against the original SDK declarations, run `python3 tests/run.py --sdk /path/to/sdk`; see [tests/README.md](tests/README.md). The tests record DOS/LibShell calls and use sanitizers; they do not emulate segmented code generation or the board. Native validation requires building with Microsoft C 7 and running on the Matrox hardware.
### Windows GDI
The GDI target is a native 16-bit Windows 3.1 program, not Win32 or Win32s. It requires Microsoft C/C++ 7.0 with its Windows emulation runtime (`LLIBCEW.LIB`), and the Windows 3.1 SDK headers, import library (`LIBW.LIB`), 16-bit linker, and resource compiler. Configure the tools' `PATH`, `INCLUDE`, and `LIB` environment variables, then run:

```text
nmake gdi
```

Run the resulting `testgdi.exe` inside Windows 3.1 configured for a 256-color (8-bit paletted) display. GDI windows share the display's color depth; the driver checks the display instead of claiming to create an independent 8-bit visual. It creates a 256-entry logical palette from `earth_cmap`, realizes the palette on activation, and shades polygons with the original palette indices. Windows retains its reserved system palette colors. Rendering uses an 8-bit compatible bitmap and memory DC before copying the finished frame to the window.

The default is `EARTH_32` (key 1), and key 2 selects `EARTH_64`; higher globe resolutions and texture mapping are excluded, including from the OSD and keyboard controls. GDI fills color-grouped triangle polygons with `PolyPolygon`; latitude strips supply the same nondegenerate triangles as the other triangle paths. Model/view rotation, perspective projection, clipping, and back-face rejection are implemented in the driver. Visibility relies on the convex globe's front surface rather than a general-purpose depth buffer for intersecting geometry. Mouse dragging, automatic rotation, resize, palette changes, and window closure use the native Windows message loop.

The driver detects a math coprocessor with `GetWinFlags`; supported 80387-or-later systems use floating-point transforms, while other systems use fixed-point transforms and projection. Mesh generation and common FPS bookkeeping still use floating point. Keep `-FPi`: the Microsoft Windows runtime uses an available coprocessor and emulates these remaining operations otherwise. `-FPi87` would incorrectly require a coprocessor. Windows prologs (`-Gw`) and the huge model (`-AH`) preserve Windows selector handling and cross-segment mesh arithmetic; Windows global-memory allocation supplies blocks larger than 64 KiB. The segmented pointer model is shared with DOS, while Windows allocation, startup, errors, and timing are selected with `GDI`.

`GDI_CC`, `GDI_LINK`, `GDI_RC`, `GDI_CFLAGS`, `GDI_CPPFLAGS`, `GDI_LIBS`, `GDI_LDFLAGS`, and `GDI_RCFLAGS` are overridable. The supplied `.def` file exports the window procedure and specifies Windows segment attributes, an 8 KiB local heap, and a 16 KiB stack. `RC -t testgdi.exe` marks the executable for protected-mode Windows even without resources. The build uses DOS response files and 8.3 output names, independently of the Matrox objects and executable. The huge-model application permits one running instance at a time. Native Microsoft C 7 compilation, segmented code generation, and Windows 3.1 display behavior require validation with those external tools and runtime; host contract tests do not substitute for it.
### OpenGL
OpenGL is currently written to support a 24-bit (true color) desktop. It would not be hard to adjust this but it seems unlikely that there is much 8-bit 3D hardware with OpenGL.

GLUT is required. If your system did not include GLUT out of the box you may have to hunt around for it:
* Linux: ```sudo apt-get install freeglut3-dev```
* Solaris:
    * https://web.archive.org/web/20010726192034/http://reality.sgi.com/opengl/glut3/glut-3.7b.sparc_solaris_64.tar.gz
    * https://web.archive.org/web/20010726175006/http://reality.sgi.com/opengl/glut3/glut-3.7b.sparc_solaris.tar.gz
      
The default `SYS_FLAGS` also includes `-DNO_GENMIPMAP`, which builds mipmaps with GLU for older OpenGL implementations. On newer systems, `SYS_FLAGS=-DGL_GLEXT_PROTOTYPES` enables the `glGenerateMipmap` path.

On Solaris, select the installed C compiler and OpenWindows paths as needed, for example:

```sh
make CC=cc CPPFLAGS=-I/usr/openwin/include \
  LDFLAGS="-L/usr/openwin/lib -lglut -lGL -lGLU -lX11 -lXmu -lm"
```

The default optimization option is the traditional Unix `-O`; override `CFLAGS` and the library locations for the installed compiler and GLUT package.

### Sun XGL
The XGL target is intended for native 32-bit SPARC Solaris 2.6 with the Sun XGL 3.x SDK/runtime and X11/OpenWindows. It uses Xlib directly, without XView or GLUT. The window requires a 24-bit TrueColor visual, matching the OpenGL driver's RGB display assumption.

```sh
make xgl
# For nonstandard Sun SDK locations:
make xgl XGLHOME=/path/to/xgl OPENWINHOME=/path/to/openwin
```

The default locations are `/opt/SUNWits/Graphics-sw/xgl` and `/usr/openwin`. `XGL_CPPFLAGS`, `XGL_CFLAGS`, `XGL_SYS_FLAGS`, and `XGL_LDFLAGS` can be overridden for the installed compiler and libraries. The native Solaris/SPARC compiler's 32-bit default is used. When relocating the runtime, also set the shell's `XGLHOME` as required by the installed XGL pipeline and font files.

The driver submits each interior latitude band to `xgl_triangle_strip`: up to 1,026 vertices and 1,024 facet colors per call at `EARTH_512`. Alternating row vertices reproduce the individual-triangle path's winding; both triangles of each globe patch retain the same flat color. Polar caps use the original nondegenerate three-vertex triangles as short strips, preserving their per-triangle UVs and avoiding undefined normals from duplicate pole vertices. Model/view/projection transforms, clipping, back-face handling, depth, and texture mapping use XGL. The `t` key enables the native texture-map/MipMap path; the XGL runtime provides its software pipeline where hardware cannot accelerate texturing, as described in Sun's programmer's guide. No Mesa replacement is needed.

X11 supplies keyboard, button, drag, expose, resize, and window-close events. The same keys and default `EARTH_512` resolution are retained as in OpenGL. Native SDK compilation and physical XGL rendering require the Sun SDK/runtime; host tests using manual-derived XGL contracts do not replace that validation.
### Xcode
No .xcodeproj is provided at this time (as of v0.1), however testglobe was built for macOS with Xcode. This will be provided at some point in the future.
## Known issues
* Linux or Mesa OpenGL seems to have an issue with drawing text when texturing is enabled (should be fixed in v0.2).

## Version history

### v?.?
* Elevation
* Sunlight, real time
* Clouds, real time if feasible
* Shadows
* City lights
* One-button benchmark
* Add WebGL driver
* Add Classic Mac/QuickDraw driver
* Add XGL driver
* Add IBM PGC and derivatives driver (start with Matrox)
  
### v0.2 (in development)
* Compile for Amiga 68k OpenGL
* Add FPS counter (complete)
* Add auto-rotation feature (complete)
* Change all comments to C style (complete)
* [bugfix] Disable texture mapping during drawing of bitmap text in OpenGL for Mesa quirk (complete)

### [v0.1](https://github.com/trguhq/testglobe/releases/tag/v0.1)
First release, OpenGL/GLUT only. Provide binaries for several platforms.
