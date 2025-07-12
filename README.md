# Testglobe
Cross-platform demo program to display a 3d globe on different APIs and hardware. Only OpenGL/GLUT is currently implemented. Provisions are there for color mapping but are incomplete - the texture is stored in an 8-bit (to 24-bit) colormap.

Out of the box, the default mode is texture mapping off, highest resolution sphere, which is likely not going to be the most performant on older systems. Note that it still appears texture mapped, however it is all flat shaded single color quads mapped to the texture of earth by the program itself, rather than texture mapping per se through the graphical API and hardware. Adjust as needed per the Interface section below.

![alt text](https://github.com/trguhq/testglobe/blob/main/testglobe.png?raw=true)
*Running on macOS (OS X)*

## Interface
* keys 1-5 adjust number of faces of globe
* mouse spins globe
* a key toggles auto-rotate / benchmark mode
* o key toggles on-screen display
* t key toggles texture mapping
* esc key exits immediately
  
## Planned drivers
* Apple (classic) Macintosh QuickDraw
* IBM PGC hardware
* IRIS GL
* OpenGL
* PHIGS
* Sun XGL
* IBM Adapter Interface? VBE/AF? Amiga? others?

## Binary builds
The following binaries are available in the corresponding releases:
* OpenGL / IRIX / MIPSIV: [v0.1](https://github.com/trguhq/testglobe/releases/tag/v0.1)
* OpenGL / Linuz / ARM64: [v0.1](https://github.com/trguhq/testglobe/releases/tag/v0.1)
* OpenGL / macOS / AMD64/ARM64: [v0.1](https://github.com/trguhq/testglobe/releases/tag/v0.1)
* OpenGL / Solaris / SPARCv8: [v0.1](https://github.com/trguhq/testglobe/releases/tag/v0.1)

## Build notes
### cosf and sinf
If you don't have cosf and sinf (some old Unixes) define NO_SINF as in the Makefile example.
### Classic Mac (QuickDraw)
The files provided here are in Unix format and can't be used directly on Classic Mac without conversion. The utility "dos2unix" can do this conversion, e.g. with its alias "unix2mac".

Building assumes some coding knowledge and experience with compilers on Classic Mac, for example Symantec Think C++, or with a cross-compiler like Retro68 (https://github.com/autc04/Retro68).
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

### v0.2 (in development)
* Add XGL driver (in progress)
* Add Classic Mac/QuickDraw driver (in progress)
* Add one-button benchmark (in progress)
* Add FPS counter (complete)
* Add auto-rotation feature (complete)
* Change all comments to C style (complete)
* [bugfix] Disable texture mapping during drawing of bitmap text in OpenGL for Mesa quirk (complete)

### [v0.1](https://github.com/trguhq/testglobe/releases/tag/v0.1)
First release, OpenGL/GLUT only. Provide binaries for several platforms.
