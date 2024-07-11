# Testglobe
Cross-platform program to display a 3d globe on different APIs and hardware. Currently does not use hardware texture mapping and only OpenGL/GLUT is implemented. Provisions are there for color mapping but are incomplete (the colors are in a colormap array).

## Planned drivers
* IBM PGC hardware
* IRIS GL
* OpenGL
* PHIGS
* Sun XGL
* IBM Adapter Interface? VBE/AF? QuickDraw? others?

## Interface
* keys 1-5 set resolution of globe
* mouse spins globe
* t toggles texture mapping
