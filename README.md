# testglobe
* [testglobe website](https://testglobe.dev)
* [testglobe repository](https://github.com/trguhq/testglobe/)

testglobe is a cross-platform demo program to display a 3d globe on different APIs and hardware. Its build targets are OpenGL/GLUT and MS-DOS Matrox LibShell. The Earth data uses 8-bit palette indices: OpenGL expands these to RGB, while LibShell installs the palette in the card's LUT and draws indexed-color polygons.

This was originally conceived as a test for lower spec systems (late 80s and early 90s 3D hardware) and was initially implemented on more modern high spec systems for convenience, but it may envolve into a general 3D demo for medium-high spec.

A feature is the ability to compare detail from the number of quads or triangles used on screen vs. detail from hardware texture mapping. Out of the box, the default mode is texture mapping off, highest resolution sphere, which is likely not going to be the most performant on older systems. Note that it still appears texture mapped, however it is all flat shaded single color quads mapped to the texture of earth by the program itself, rather than texture mapping per se through the graphical API and hardware. Adjust as needed per the Interface section below.

![raw image](https://github.com/trguhq/testglobe/blob/main/testglobe.png?raw=true)
*Running on macOS (OS X)*

External: *[Video of testglobe running on Sun Ultra 60 with Elite3D-m6 graphics](https://www.youtube.com/watch?v=CffPcvwioUA)*

## Interface
* keys 1-5 adjust number of faces of globe for OpenGL; LibShell supports keys 1-2
* mouse spins globe
* a key toggles auto-rotate / benchmark mode
* o key toggles on-screen display
* t key toggles texture mapping for OpenGL
* esc key exits immediately
  
## Planned drivers
* Apple (classic) Macintosh QuickDraw
* IBM PGC and derivatives
* IRIS GL
* OpenGL
* PHIGS
* Sun XGL
* WebGL
* IBM Adapter Interface? VBE/AF? Amiga? others?

## Binary builds
The following binaries are available in the corresponding releases:
* OpenGL / IRIX / MIPSIV: [v0.1](https://github.com/trguhq/testglobe/releases/tag/v0.1)
* OpenGL / Linux / ARM64: [v0.1](https://github.com/trguhq/testglobe/releases/tag/v0.1)
* OpenGL / macOS / AMD64/ARM64: [v0.1](https://github.com/trguhq/testglobe/releases/tag/v0.1)
* OpenGL / Solaris / SPARCv8: [v0.1](https://github.com/trguhq/testglobe/releases/tag/v0.1)

## Build notes
GNU make is required. From the repository root or `src`, `make` (or `make opengl`) builds `testglobe`; `make matrox` builds `testglob.exe` with Microsoft C 7.0. The root Makefile forwards builds to `src`, where the binaries are placed. OpenGL and DOS objects are kept separately in `objogl` and `objmtx`; switching targets does not reuse incompatible objects.

The OpenGL target defines `FLAT`, `DRV_OGL`, and `TRI`. The Matrox target defines `SEG86`, `DRV_MTX`, and `QUAD`, and compiles all of its objects with `-AH`. These select the memory model, graphics driver, and primitive independently. `make clean` removes both targets' generated files.

### cosf and sinf
If you don't have cosf and sinf (some old Unixes) define NO_SINF as in the Makefile example.
### Classic Mac (QuickDraw)
The files provided here are in Unix format and can't be used directly on Classic Mac without conversion. The utility "dos2unix" can do this conversion, e.g. with its alias "unix2mac".

Symantec Think C++ was used for testing but a cross-compiler like Retro68 (https://github.com/autc04/Retro68) should work.
### DOS
The Matrox target requires MS-DOS, Microsoft C 7.0 with its huge-model runtime, a DOS port of GNU make, and the Matrox LibShell C binding library matching that compiler/model. Put the SDK at the repository root:

```text
XGX1/HEADERS/DEF.H
XGX1/HEADERS/LIBFN.H
XGX1/HEADERS/IOLIB.H
libsv5/lscl.lib
```

With the compiler and linker on `PATH`, run `make matrox`. From `src`, an external SDK location can be selected with `make matrox XGX_ROOT=C:/MATROX`; `MTX_CC`, `MTX_CFLAGS`, `MTX_CPPFLAGS`, `MTX_LIB`, and `MTX_LDFLAGS` are overridable. Keep `-AH` when overriding compiler flags. `XGX_ROOT` defaults to `..` relative to `src`. Under COMMAND.COM, GNU make detects `COMSPEC` and emits DOS paths and directory commands. Compiler/linker response files avoid the DOS command-line length limit; generated DOS names fit 8.3.

The driver assumes a 640x480, 256-color PG-641-compatible LibShell display. Initialize the board and its LibShell firmware according to the SDK before starting `testglob.exe`. Load a Microsoft-compatible DOS mouse driver for mouse dragging; keyboard controls work without a mouse. The default globe is `EARTH_32` (key 1), and key 2 selects `EARTH_64`. Other globe sizes and texture mapping are excluded from this build, including its help and status text.

LibShell performs the model/view transforms, orthographic projection, viewport clipping, and filled world-coordinate polygon drawing through `v3arwplygn` (`V3ARWPLYGN`). Each ordinary patch is a quad; coincident vertices at the poles are collapsed to a triangle. The host rejects back-facing polygons of the convex globe because the base PG-641 interface does not document a depth buffer or back-face switch. The original palette index colors each polygon; all 256 LUT entries come directly from `earth_cmap`. `SEG86` uses normalized huge pointers, `_halloc`/`_hfree`, long mesh counts, and DOS-compatible timing rather than POSIX calls. The SDK and Microsoft tools are external dependencies and are not redistributed here.

Host regression checks are available with `python3 tests/run.py`. To also check the Matrox driver against the original SDK declarations, run `python3 tests/run.py --sdk /path/to/sdk`; see [tests/README.md](tests/README.md). The tests record DOS/LibShell calls and use sanitizers; they do not emulate segmented code generation or the board. Native validation requires building with Microsoft C 7 and running on the Matrox hardware.
### OpenGL
OpenGL is currently written to support a 24-bit (true color) desktop. It would not be hard to adjust this but it seems unlikely that there is much 8-bit 3D hardware with OpenGL.

GLUT is required. If your system did not include GLUT out of the box you may have to hunt around for it:
* Linux: ```sudo apt-get install freeglut3-dev```
* Solaris:
    * https://web.archive.org/web/20010726192034/http://reality.sgi.com/opengl/glut3/glut-3.7b.sparc_solaris_64.tar.gz
    * https://web.archive.org/web/20010726175006/http://reality.sgi.com/opengl/glut3/glut-3.7b.sparc_solaris.tar.gz
      
If you don't have glGenerateMipmap define NO_GENMIPMAP as in the Makefile example.
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
