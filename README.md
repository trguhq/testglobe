# Testglobe
Cross-platform program to display a 3d globe on different APIs and hardware. Only OpenGL/GLUT is currently implemented. Provisions are there for color mapping but are incomplete (the colors are in a colormap array).

Out of the box, the default mode is texture mapping off, highest resolution sphere, which is likely not going to be the most performant on older systems. Adjust as needed per the Interface section below.

## Interface
* keys 1-5 set resolution of globe
* mouse spins globe
* t toggles texture mapping
  
## Planned drivers
* IBM PGC hardware
* IRIS GL
* OpenGL
* PHIGS
* Sun XGL
* IBM Adapter Interface? VBE/AF? QuickDraw? others?
