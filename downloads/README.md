# Development binaries

These archives are cross-compiled on Linux. The Matrox archive is testglobe 0.2.3,
from source revision
[`6b78013`](https://github.com/trguhq/testglobe/tree/6b78013eba4ccb3dc8ed763fde913b9444961f10);
the others are 0.2.2, from
[`a93349b`](https://github.com/trguhq/testglobe/tree/a93349b38a3dd65a1eb5eee0cf7cf27264999cad).
Each archive is named `testglobe-<target>-<version>.zip` after its make target and
contains the application, installation notes (`README.txt`), the license and a
`SHA256SUMS` file for its contents. [SHA256SUMS](SHA256SUMS) here covers the archives.
See the [build instructions](../docs/build.md#linux-hosted-cross-builds) to build them
yourself.

| Target | Archive | Contents |
| --- | --- | --- |
| DOS, Matrox LIB Shell and IBM PGC-compatible boards | [testglobe-matrox-0.2.3.zip](https://github.com/trguhq/testglobe/raw/refs/heads/develop/downloads/testglobe-matrox-0.2.3.zip) | `testglob.exe`, 8086 or later |
| DOS, TIGA 2.x boards | [testglobe-tiga-0.2.2.zip](https://github.com/trguhq/testglobe/raw/refs/heads/develop/downloads/testglobe-tiga-0.2.2.zip) | `testglob.exe` with its TI transform module appended |
| Windows 3.0 and 3.1 GDI | [testglobe-gdi-0.2.2.zip](https://github.com/trguhq/testglobe/raw/refs/heads/develop/downloads/testglobe-gdi-0.2.2.zip) | `testglob.exe`, Win16 protected mode |
| Classic Macintosh QuickDraw | [testglobe-quickdraw-0.2.2.zip](https://github.com/trguhq/testglobe/raw/refs/heads/develop/downloads/testglobe-quickdraw-0.2.2.zip) | System 7 / 68000 application as a MacBinary file and an 800 KiB HFS disk image |
| Amiga chipset / RTG | [testglobe-amiga-0.2.2.zip](https://github.com/trguhq/testglobe/raw/refs/heads/develop/downloads/testglobe-amiga-0.2.2.zip) | Application and Workbench icon |
| Amiga PiStorm3D (MiniGL) | [testglobe-amiga-opengl-0.2.2.zip](https://github.com/trguhq/testglobe/raw/refs/heads/develop/downloads/testglobe-amiga-opengl-0.2.2.zip) | Application and Workbench icon |

Toolchains: OpenWatcom 2.0 beta (Matrox, TIGA, GDI); TI's TMS340 Code Generation
Tools 5.01 under DOSBox for the TIGA module, with `ai.lib` from the TIGA SDK 2.01;
Retro68 with GCC 16.1.0 and the Multiversal interfaces (QuickDraw); VBCC 0.9h patch
3, vasm 2.0f, vlink 0.18a and the NDK 3.2 Release 4 headers (Amiga), plus the MiniGL
SDK at `d950a3a1` (Amiga OpenGL). The Matrox build uses no Matrox library or
headers.

All Earth data is embedded in the applications. Decode the Macintosh MacBinary file
with a resource-fork-aware utility, or use the disk image.

Each build compiled and linked, and the Matrox, TIGA, GDI and QuickDraw executables
passed the format and embedded-data checks in `tests/cross_binary.py`, including the
TIGA module's CRC; the Amiga application's embedded maps were read back by the host
data test. The host regression tests cover each driver through call recorders, and
the TIGA driver also compiles against the SDK's own headers. None of these builds has
been run on its target hardware here.
