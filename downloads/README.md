# Development binaries

These archives contain builds from the source revision that last updated each archive.
See the [build instructions](../README.md#linux-hosted-cross-builds) for compilers
and SDK requirements. Each ZIP includes installation notes, a license, and file
checksums; [SHA256SUMS](SHA256SUMS) covers the ZIPs and standalone executables.

| Build | Archive | Application |
| --- | --- | --- |
| Windows 3.1 GDI | [Download ZIP](https://github.com/trguhq/testglobe/raw/refs/heads/develop/downloads/testglobe-gdi-cross.zip) | [testglob.exe](https://github.com/trguhq/testglobe/raw/refs/heads/develop/downloads/testglob.exe), Win16 protected mode |
| Classic Macintosh QuickDraw | [Download](https://github.com/trguhq/testglobe/raw/refs/heads/develop/downloads/testglobe-quickdraw-cross.zip) | System 7 / 68000 application in MacBinary and HFS disk-image formats |
| DOS TIGA | [Download](https://github.com/trguhq/testglobe/raw/refs/heads/develop/downloads/testglobe-tiga-cross.zip) | `testglob.exe`, embedded TI transform module |
| DOS Matrox LibShell | [Download](https://github.com/trguhq/testglobe/raw/refs/heads/develop/downloads/testglobe-matrox-cross.zip) | `testglob.exe`, 286 or later |
| Amiga chipset / RTG palette build | [Download](https://github.com/trguhq/testglobe/raw/refs/heads/develop/downloads/testglobe-amiga-palette-fix.zip) | Application and Workbench icon; predates fullscreen support |
| Earlier Amiga build | [Download](https://github.com/trguhq/testglobe/raw/refs/heads/develop/downloads/testglobe-amiga-50a89c3.zip) | Archived build from `50a89c3` |

All Earth data is embedded in the applications. Decode the Macintosh MacBinary
file with a resource-fork-aware utility, or use the included disk image.

The GDI build closes every Win16 polygon explicitly and fills its boundary through
a monochrome mask, preserving palette dithering across the edges. If mask allocation
fails, solid fills and matching outlines remain available. The ZIP and standalone
executable contain the same application.
Its FPS counter reports subsecond frames promptly instead of waiting for 60 frames.

The four cross-builds passed compilation, executable/resource inspection, and host
regression tests. The Matrox library adapter also passed DOSBox tests with x87 and
forced floating-point emulation. Native Windows/Macintosh execution and physical
Matrox/TIGA graphics remain untested in the build environment.
