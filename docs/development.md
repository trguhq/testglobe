# Development notes

## Configuration

Native Makefiles choose the memory model and driver; driver headers choose the
primitive and capabilities. Keep those responsibilities separate.

| Driver | Build defines | Primitive | Additional geometry capabilities |
| --- | --- | --- | --- |
| OpenGL | `FLAT`, `DRV_OGL` | `TRISTRIP` | `DRV_TRI_CAPS`, color/RGB/UV triangle callbacks |
| Amiga OpenGL | `FLAT`, `DRV_OGL`, `OGL_AMIGA` | `TRISTRIP` | Same callbacks with MiniGL submission limits |
| IRIS GL | `FLAT`, `DRV_IGL` | `TRISTRIP` | Color/UV strip callbacks |
| Sun XGL | `FLAT`, `DRV_XGL` | `TRISTRIP` | RGB/UV strip callbacks |
| Matrox | `SEG86`, `DRV_MTX` | `QUAD` | `DRV_QUAD_BATCH` |
| TIGA | `SEG86`, `DRV_TGA` | `QUAD` | `DRV_QUAD_BATCH` |
| GDI | `SEG86`, `DRV_GDI` | `QUAD` | `DRV_QUAD_BATCH` |
| QuickDraw | `SEG68`, `DRV_QD` | `QUAD` | `DRV_QUAD_BATCH` |
| Amiga | `FLAT`, `DRV_AMI` | `QUAD` | `DRV_QUAD_BATCH`; optional `DRV_QUAD_TRIM` |

- All native application targets also define `DRV`; Microsoft C builds use `-AH`. The
  GDI header supplies `GDI` for Windows-specific integration.
- Primitive and callback-capability defines belong in headers, not compiler flags.
  `TRI` remains a tested reference path. `DRV_TRI_COLOR`, `DRV_TRI_RGB`, and
  `DRV_TRI_UV` expose only implemented callbacks.
- `DRV_CULL` enables the CPU band-culling control in OpenGL, IRIS GL, and XGL. A driver
  header may set `DRV_CULL_DEFAULT` to `TRUE` (XGL does) to start with it on.
- `DRV_COLORMAP` selects palette-byte geometry inputs, and `drv_colormapped` describes
  the window's runtime mode. Every native driver except XGL defines `DRV_COLORMAP`,
  even when it can draw to RGB windows.
- GDI and Amiga select `DRV_PALETTE_DITHER` for the shared color matcher and ordered
  patterns; QuickDraw leaves that work to its native color machinery.

## Versions

Versions have three fields, major.minor.patch, defined in `src/testglob.h`
(`TESTGLOBE_VERSION_MAJOR`, `_MINOR` and `_PATCH`). Applications print the version at
startup; QuickDraw shows it in its About box. Every push to GitHub that changes code
increments the patch field; a push that changes only documentation does not. Each
field runs from 0 to 99 and then rolls over into the next: 0.2.99 is followed by
0.3.0, and 0.99.99 by 1.0.0. `testglob.h` stops the build if a field exceeds 99. The
README's version history has one section per minor series, such as v0.2.x.

## Source layout

| Location | Responsibility |
| --- | --- |
| `src/globe.c` | Scene ownership, recoverable mesh construction, and the plural traversals `globe_draw_tris()`, `globe_draw_tristrips()`, `globe_draw_quads()`, `globe_draw_quadbatches()`, and OpenGL pole-cap traversal. |
| `src/drv_*.c` | Singular native callbacks such as `drv_draw_tri_*`, `drv_draw_tristrip_*`, `drv_draw_quad_*`, and `drv_draw_quadbatch_*`, with color/RGB/UV variants grouped by primitive. |
| `src/drv.c` | Shared allocation and clock services, input, rotation, FPS/OSD, palette matching, geometry-packet helpers, and Earth-resource traversal. |
| Driver-private helpers | Backend-prefixed windowing, events, timing, transforms and caches, and drawing state. `src/ogl_glut.c`/`.h` adapt the OpenGL driver to GLUT; `src/ogl_ami.c`/`.h` integrate it with MiniGL and the Amiga; `src/ami_time.c`/`.h` supply both Amiga drivers' `timer.device` clock. |
| `src/drv_cpu.c` / `src/tga_gsp.c` | Shared Matrox/TIGA DOS CPU/FPU detection / TI-processor transforms; `tga_cmd.h` and `tga_ext.asm` define the module interface. |
| `src/tga_pack.c` | Build-time attachment of the TI module to the DOS executable; `drv_tga.c` extracts it for the native loader, and both use the format and MZ/CRC-32 checks in `tga_rlm.h`. |
| `src/cross*.py` | Linux-hosted build orchestration for the DOS, Win16, and QuickDraw drivers. |
| `src/mtx_ls.c`/`.h` | The LibShell calls the Matrox driver uses, implemented directly on the PG Series 1 host interface, so no Matrox library or headers are needed. |
| `src/mtx_pgc.c`/`.h` | IBM PGC hexadecimal commands through the communication area, for PGC-compatible boards such as the Matrox PG-640A; the Matrox driver uses them when no LIB Shell board answers. |
| `src/buildio.h` | C89 file validation and safe output publication for the Earth converter and TIGA packer; not linked into applications. |
| `src/e_ext.h` | Shared palette and bounded map-reader declarations; the generated arrays stay private. |
| `web/` | Browser frontend, WebGL rendering, and the generated palette module; reads the canonical `data/` maps. |

## Driver interface

Shared geometry helpers include `drv_validate_quadbatch()`,
`drv_submit_quad_as_batch()`, `drv_submit_quadbatch()`, `drv_quadbatch_context()`,
`drv_expand_tristrip_uv()`, `drv_tristrip_batch_facets()`, and
`drv_split_tristrip_batches()`. The common strip-range calculation preserves even
winding for indexed, RGB, and UV packets. Shared input uses `drv_handle_key()`,
`drv_rotate_drag()`, and `drv_rotate_elapsed()`; the DOS Matrox/TIGA mouse adapters
use `drv_dos_mouse_init()` and `drv_dos_mouse_poll()`. Matrox and TIGA also call
`drv_dos_break_init()`, which turns Ctrl-C and Ctrl-Break into a flag that the event
loop reads with `drv_dos_break_pending()` and, from DOS 3.1, fails critical disk
errors instead of prompting. Their `drv_soft3d_select()` runs the CPU and x87 probes
once and sets `drv_soft3d_kernel` (x87, 386 integer, or baseline integer), which the
OSD reports. Each driver calls the selected kernel itself, keeping the kernel calls
visible to its recorder tests, and holds its matrix and points in the shared
`Soft3d_matrix` and `Soft3d_points` unions. `drv_match_palette_colors()`
matches a whole palette for GDI and Amiga, searching each distinct realized color
once. Backend resize and
texture-loading helpers stay private; `drv_render()` and `drv_close()` are the common
driver entry points.

Drawing callbacks borrow their inputs for the call and never rebuild the scene inside
a native begin/end or paint scope. Scene revisions invalidate geometry caches, and
texture intent is tracked separately from the active draw pass. `Drv_result`
distinguishes allocation, data, input, unavailable-feature, and native failures.
Optional caches are evicted outside rendering scopes through `drv_release_optional()`.
`drv_status_required()` and `drv_animation_allowed()` implement the common
failed-frame policy. Texture drivers share the size search `drv_texture_fit()`, which
evicts optional caches once and halves the width after each memory shortage, and use
the typed, recoverable `texture_build()`;
`texture_generate()` remains a fatal compatibility wrapper, and
`texture_try_generate()` its nonterminating boolean counterpart. Selecting the
unsupported PGC or PHIGS drivers reports a configuration error; their headers are
reference stubs. `types.h` provides wide arithmetic aliases for segmented builds.

## Makefiles

The root and `src` Makefiles intentionally duplicate their rules with different
directory defaults, because historical NMAKE and Solaris make share no include
syntax; keep both files synchronized. OpenGL uses ordinary `.o` names; Solaris,
Apple, IRIS GL, XGL, and Amiga OpenGL use the `s_`, `a_`, `i_`, `x_`, and `ao_`
prefixes. Matrox uses `.obj`, TIGA `t_*.obj`, GDI `g_*.obj`, QuickDraw `q_*.o`, and
Amiga `am_*.o`; TIGA module objects stay separate from the x86 host objects. MPW uses
its own Makefile grammar.

DOS link rules write their object lists to response files in several `echo` lines,
each within COMMAND.COM's 127-character command line once NMAKE adds `/c`. Keep new
objects within that budget. `MTX_HOTFLAGS` and `TGA_HOTFLAGS` (`-Gs`) remove stack
probes from the per-vertex and per-polygon objects only; those objects must not
recurse.

## Geometry submission

The scene is one opaque convex globe, with the camera, clipping limits, radius, and
starting pose defined in `globe.h`. Paths without a depth buffer rely on back-face
rejection for visibility, so they are not general renderers for intersecting or
transparent objects.

The default radius is 1,000. OpenGL, IRIS GL, and XGL accept any positive finite
radius, but their camera and clipping values must still suit the scene. TIGA, GDI,
QuickDraw, and Amiga keep a maximum radius of 1,000 for their shared fixed-point
pipeline, and Matrox keeps the validated 1,000-unit range of LibShell's fixed-point
transforms. These are driver limits, not a restriction on every globe.

With CPU culling off, triangle strips follow complete interior latitude bands. At key
5, an interior strip has 1,026 vertices and 1,024 triangles. Desktop OpenGL sends 254
such strips plus one cap batch: 263,676 vertices for 261,120 triangles, compared with
783,360 vertices as independent triangles. Amiga OpenGL splits bands for MiniGL's
color and buffer constraints, and IRIS GL and XGL send the caps as separate
three-vertex meshes or strips. Flat colors and UVs keep the independent-triangle
topology. Under allocation pressure, bounded packets may split a band without
changing its facets. UVs are generated from grid coordinates at submission rather
than stored as a full two-dimensional array, and XGL expands palette indices only for
its current RGB packet. XGL has separate `drv_draw_tristrip_flat_rgb()` and
`drv_draw_tristrip_flat_uv()` callbacks.

`DRV_QUAD_BATCH` is a capability of `QUAD`. Matrox, TIGA, GDI, QuickDraw, and Amiga
use at most 64 quads and 130 shared vertices per packet, with four indices and one
palette byte per quad. Traversal follows latitude bands within bounded longitude
tiles. In the host software paths, packet sequence and row-continuity metadata let the
previous bottom row become the next top row without repeating its transforms or cached
projections. Direct quad calls, new tiles, interrupted streams, and new frames disable
this reuse, and no cache treats a submission buffer's address as a persistent vertex
identity. The shared `drv_quadstream_*()` helpers check eligibility, commit successful
packet state, and reset interrupted streams; each driver keeps its own transform
dispatch and cache allocation.

At `EARTH_32`, row reuse cuts host transforms from 1,056 to 561 per frame, and an
optional packet cache also avoids projecting the same corner for neighboring quads.
Larger maps repeat tile-boundary vertices to keep storage bounded; there is no cache
of the whole transformed globe. GDI groups visible polygons by color for
`PolyPolygon`, while QuickDraw, Amiga, TIGA, and Matrox fill them individually.
Native LibShell mode sends each quad's world vertices. The TI transform module
receives only each band's new row after the first, but its transfer costs can still
make it slower than host transforms. Individual quad submission remains available.

## Shared rendering pipelines

Matrox, TIGA, GDI, QuickDraw, and Amiga share `src/soft3d.c`/`.h` for integer
IEEE-float decoding, fixed-point transforms, projection, clipping, and back-face
rejection; `src/soft3fp.c` provides the floating-point equivalents. All five use
perspective projection over row-reusing packets with bounded projection caches;
Matrox prepares its matrix from LibShell's integer-degree yaw and pitch
(`soft3d_*_prepare_degrees()`). Indexed projection
validates indices against the initialized packet. Whole polygons inside the depth and
viewport bounds skip clipping, while polygons crossing them take the general path.
Back-face rejection uses the complete rounded polygon, preserving silhouette and
collapsed-pole behavior, and clearly rejected polygons skip later processing.

The GDI and Amiga projection caches are optional and bounded to one packet; without
them, the uncached path is used. After `drv_release_optional()`, GDI, QuickDraw, and
Amiga reacquire their optional resources at most once per scene revision, outside
rendering. Amiga's cache also applies the display's pixel aspect once per vertex
(`soft3d_project_cache_begin_aspect()`); the plain `soft3d_project_cache_begin()`
records a 1/1 ratio for the other drivers. QuickDraw, TIGA, and Matrox use bounded
static packet storage. Frame, pose, viewport, and interrupted-stream changes
invalidate reuse. QuickDraw's `qd_fpu.c`/`qd_math.h` boundary and Amiga's runtime
CPU/FPU dispatch keep hardware floating-point code apart from baseline 68000
execution; Amiga's separate 68020 object also carries the integer projection,
clipping, and back-face code. Resource-backed indices load directly into their final
storage without an extra full-map allocation. TIGA's TI module uses the same
fixed-point rounding as the host integer kernels and validates each command's
version and bounds.

OpenGL and IRIS GL share `src/texsoft.c`/`.h` for software texturing: homogeneous
clipping, depth testing and culling, perspective-correct UVs, repeat sampling, and
filtered mipmaps. Triangles inside every clip plane skip the clipper, each triangle
and pixel takes one reciprocal instead of separate divides, power-of-two wrapping uses
masks, and a reused raster clears only the previous frame's drawn rectangle. They link
it as `texsoft.o` and `i_soft.o`; the OpenGL platform variants use their own object
prefixes. `src/texture.h` sets `TEXTURE_MAX_SIZE=512`, `TEXTURE_PALETTE_SIZE=256`, and
`TEXTURE_CACHE_RASTER=0` by default.

- Disabling software texturing releases the frame and depth buffers but keeps the
  texture images.
- Indexed OpenGL renders directly into palette bytes for `glDrawPixels`, with no
  full-frame conversion copy; IRIS GL keeps its native packed words. Compared with the
  older word raster plus byte copy, this saves about 1.17 MiB on 32-bit hosts or
  2.34 MiB on 64-bit hosts at 640 × 480.
- Changing dimensions or raster format releases obsolete storage before replacing it.
  Same-size staging images reuse their allocation; a changed size replaces the old
  image only after the new one is complete.
- The software texture loader adopts the generated RGB base image instead of copying
  it. OpenGL keeps only that bilinear image; IRIS GL also builds lower mip levels for
  trilinear minification.
- IRIS GL and XGL fill their native images directly from indexed map rows. Native
  OpenGL uploads the RGB staging image and releases it after a successful upload.
  Native libraries may keep their own texture, geometry, and framebuffer storage.

The Earth converter and TIGA packer share build-only file helpers that use POSIX
calls on Unix and Microsoft runtime calls on DOS to reserve and publish output paths
safely. Generated readers handle chunk and 64 KiB boundaries, and texture expansion
and globe color sampling share them.
