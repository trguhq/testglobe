# Testglobe
Cross-platform program to display a 3d globe on different APIs and hardware. Only OpenGL/GLUT is currently implemented. Provisions are there for color mapping but are incomplete - the texture is stored in an 8-bit (to 24-bit) colormap.

Out of the box, the default mode is texture mapping off, highest resolution sphere, which is likely not going to be the most performant on older systems. Adjust as needed per the Interface section below.

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
