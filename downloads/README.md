# Development binaries

These archives are testglobe 0.2.2, cross-compiled on Linux from source revision
[`a93349b`](https://github.com/trguhq/testglobe/tree/a93349b38a3dd65a1eb5eee0cf7cf27264999cad).
Each archive is named `testglobe-<target>-<version>.zip` after its make target and
contains the application, installation notes (`README.txt`), the license and a
`SHA256SUMS` file for its contents. [SHA256SUMS](SHA256SUMS) here covers the archives.
See the [build instructions](../docs/build.md#linux-hosted-cross-builds) to build them
yourself.

| Target | Archive | Contents |
| --- | --- | --- |
| Linux, OpenGL/GLUT: x86-64, x86, ARM64 and ARMv7 | [testglobe-linux-0.2.2.zip](https://github.com/trguhq/testglobe/raw/refs/heads/develop/downloads/testglobe-linux-0.2.2.zip) | One `testglobe` file: a `sh` launcher and a build for each architecture |
| DOS, Matrox LIB Shell and IBM PGC-compatible boards | [testglobe-matrox-0.2.2.zip](https://github.com/trguhq/testglobe/raw/refs/heads/develop/downloads/testglobe-matrox-0.2.2.zip) | `testglob.exe`, 8086 or later |
| DOS, TIGA 2.x boards | [testglobe-tiga-0.2.2.zip](https://github.com/trguhq/testglobe/raw/refs/heads/develop/downloads/testglobe-tiga-0.2.2.zip) | `testglob.exe` with its TI transform module appended |
| Windows 3.0 and 3.1 GDI | [testglobe-gdi-0.2.2.zip](https://github.com/trguhq/testglobe/raw/refs/heads/develop/downloads/testglobe-gdi-0.2.2.zip) | `testglob.exe`, Win16 protected mode |
| Classic Macintosh QuickDraw | [testglobe-quickdraw-0.2.2.zip](https://github.com/trguhq/testglobe/raw/refs/heads/develop/downloads/testglobe-quickdraw-0.2.2.zip) | System 7 / 68000 application as a MacBinary file and an 800 KiB HFS disk image |
| Amiga chipset / RTG | [testglobe-amiga-0.2.2.zip](https://github.com/trguhq/testglobe/raw/refs/heads/develop/downloads/testglobe-amiga-0.2.2.zip) | Application and Workbench icon |
| Amiga PiStorm3D (MiniGL) | [testglobe-amiga-opengl-0.2.2.zip](https://github.com/trguhq/testglobe/raw/refs/heads/develop/downloads/testglobe-amiga-opengl-0.2.2.zip) | Application and Workbench icon |

Toolchains: GCC 13.3 against Ubuntu 20.04 libraries (glibc 2.31, freeglut 2.8.1) for
Linux; OpenWatcom 2.0 beta (Matrox, TIGA, GDI); TI's TMS340 Code Generation
Tools 5.01 under DOSBox for the TIGA module, with `ai.lib` from the TIGA SDK 2.01;
Retro68 with GCC 16.1.0 and the Multiversal interfaces (QuickDraw); VBCC 0.9h patch
3, vasm 2.0f, vlink 0.18a and the NDK 3.2 Release 4 headers (Amiga), plus the MiniGL
SDK at `d950a3a1` (Amiga OpenGL). The Matrox build uses no Matrox library or
headers.

Linux has no multi-architecture executable format, so `testglobe` is a short shell
script followed by x86-64, i686, ARM64 and ARMv7 builds. It picks the build for the
machine (a 32-bit system on a 64-bit kernel gets the 32-bit one), unpacks it to a
temporary directory and runs it there. The builds need glibc 2.29 or later and the
system's OpenGL, GLU and freeglut libraries; the launcher also accepts freeglut 3.4's
`libglut.so.3.12` in place of `libglut.so.3`. Its `README.txt` lists the packages and
options.

All Earth data is embedded in the applications. Decode the Macintosh MacBinary file
with a resource-fork-aware utility, or use the disk image.

Each build compiled and linked, and the Matrox, TIGA, GDI and QuickDraw executables
passed the format and embedded-data checks in `tests/cross_binary.py`, including the
TIGA module's CRC; the Amiga application's embedded maps were read back by the host
data test. The host regression tests cover each driver through call recorders, and
the TIGA driver also compiles against the SDK's own headers. The Linux launcher ran
its x86-64 and i686 builds on Ubuntu 24.04 under Xvfb with Mesa, and the ARM builds
ran under QEMU with Ubuntu 20.04's libraries; each drew the globe. None of these
builds has run on physical retro or ARM hardware here.
