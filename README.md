# Testglobe
Cross-platform program to display a 3d globe on different APIs and hardware. Only OpenGL/GLUT is currently implemented. Provisions are there for color mapping but are incomplete - the texture is stored in an 8-bit (to 24-bit) colormap.

Out of the box, the default mode is texture mapping off, highest resolution sphere, which is likely not going to be the most performant on older systems. Note that it still appears texture mapped, however it is all flat shaded single color quads mapped to the texture of earth by the program itself, rather than texture mapping per se through the graphical API and hardware. Adjust as needed per the Interface section below.

![alt text](https://github.com/trguhq/testglobe/blob/main/testglobe.png?raw=true)
*Running on macOS (OS X)*

## Interface
* keys 1-5 set resolution of globe
* mouse spins globe
* o toggles on-screen display
* t toggles texture mapping
  
## Planned drivers
* IBM PGC hardware
* IRIS GL
* OpenGL
* PHIGS
* Sun XGL
* IBM Adapter Interface? VBE/AF? QuickDraw? others?

## Build notes
### cosf and sinf
If you don't have cosf and sinf (some old Unixes) define NO_SINF as in the Makefile example.
### OpenGL
OpenGL is currently written to support a 24-bit (true color) desktop. It would not be hard to adjust this but it seems unlikely that there is much 8-bit 3D hardware with OpenGL.

GLUT is required. If your system did not include GLUT out of the box you may have to hunt around for it:
* Linux: ```sudo apt-get install freeglut3-dev```
* Solaris:
    * https://web.archive.org/web/20010726192034/http://reality.sgi.com/opengl/glut3/glut-3.7b.sparc_solaris_64.tar.gz
    * https://web.archive.org/web/20010726175006/http://reality.sgi.com/opengl/glut3/glut-3.7b.sparc_solaris.tar.gz
      
If you don't have glGenerateMipmap define NO_GENMIPMAP as in the Makefile example.

## Known issues
* FreeGLUT on Linux (tested on arm64 Raspberry Pi) seems to have an issue with drawing text when texturing is enabled.
