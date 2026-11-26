# WebGL

The browser frontend draws the globe with WebGL 2, using the same Earth maps and
palette as the native drivers. A live copy runs at
[testglobe.dev/web](https://testglobe.dev/web/).

## Requirements

A modern browser with WebGL 2 enabled. Browsers without it show an explanation;
there is no WebGL 1 renderer. The frontend is plain JavaScript ES modules, with no
framework, npm dependencies, or server-side code.

## Run and host

Serve the repository root with any static HTTP server, for example:

```sh
python3 -m http.server 8000
```

Then open `http://localhost:8000/web/`. Opening `web/index.html` through a `file:`
URL does not work, because modules and map data must be fetched over HTTP. When
publishing, keep `web/` and `data/` as sibling directories so the relative data URLs
resolve.

The repository's GitHub Pages workflow publishes the README as the site home page
and the frontend at `https://testglobe.dev/web/`; keep the root `CNAME` file, which
sets that custom domain. The workflow runs on `main`, so changes on `develop` go live
once they reach `main`. The checked-in browser files need no build step.

The palette module is checked in. Regenerate it only when `src/e_cmap.h` changes:

```sh
python3 web/gen_cmap.py
python3 web/gen_cmap.py --check
```

The binary maps stay canonical; the browser keeps no separate copy of the map data.
`web/package.json` only gives Node-based checks ES-module scope and adds no
package-install step. Native builds need no browser tooling.

## Rendering and controls

The default is key 5, with texturing off. Keys `1`–`5` select the same 32- through
512-wide maps as the native drivers. Keyboard controls, on-screen buttons, and
mouse or touch dragging operate the globe. **Reset view** restores the initial view
and pauses automatic rotation, and `Esc` pauses rotation and releases dragging.

WebGL keeps vertex and index buffers on the GPU and draws latitude strips with native
transforms, depth testing, and back-face culling; these buffers take the place of
display lists, so there is no `d` control. Flat shading quantizes UV coordinates to
Earth patches, then looks up each patch's palette index and RGB color. Texture mapping
normally uses a separate 512 × 256 RGB image with GPU-generated mipmaps, whatever the
globe resolution. CPU culling (`c`) keeps the GPU buffers and textures and selects
front-facing strip ranges by updating the index buffer.

If map data is missing or memory runs short at startup, the frontend falls back to a
smaller resolution. A failed resolution change keeps the existing globe; **Retry** requests
the chosen level again. Texture data or allocation failures try smaller images
before falling back to flat colors with `texture fail`, and the OSD reports reduced
texture sizes. Toggle texture off and on to retry. Unrelated graphics errors are not
treated as memory shortages. Context loss suspends drawing while keeping the
requested settings, and resources are rebuilt when the browser restores the
context. Errors and recovery messages remain visible even with the overlay hidden.

The canvas follows its available size, with a capped rendering resolution on large or
high-density displays. FPS follows `requestAnimationFrame` and the browser's refresh
scheduling, so it is not directly comparable with native results.

## Validation

See the [WebGL checks](testing.md#webgl) for palette, data, and browser tests. Browser
rendering depends on its graphics implementation and the device's resources; desktop
and mobile environments can differ.
