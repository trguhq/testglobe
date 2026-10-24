/* Shared browser globe geometry and canonical Earth data.
 * Copyright (C) 2026 trguhq. SPDX-License-Identifier: GPL-3.0-or-later */
export { earth_palette } from "./e_cmap.js";

export const EARTH_LEVELS = Object.freeze([32, 64, 128, 256, 512]);

function earth_validate_width(width) {
    if (!EARTH_LEVELS.includes(width)) {
        throw new RangeError("Earth width must be 32, 64, 128, 256, or 512");
    }
}

/* Match the C grid: north pole at z=1000, u=column/width, v=row/height.
 * Float assignments mirror globe_init_verts; the renderer owns the camera.
 * Independent pole strips retain the original triangles' distinct UVs.
 * WebGL 2's fixed primitive restart resets winding between strips, allowing
 * all bands and caps to share one indexed TRIANGLE_STRIP draw call. */
export function globe_build_mesh(width) {
    earth_validate_width(width);
    const height = width / 2;
    const vertexCount = (width + 1) * (height + 1);
    const triangleCount = 2 * width * (height - 1);
    const vertices = new Float32Array(vertexCount * 5);
    const longitudeStep = Math.fround(2 * Math.PI / width);
    const latitudeStep = Math.fround(Math.PI / height);
    let position = 0;
    for (let row = 0; row <= height; ++row) {
        const latitude = Math.fround(Math.fround(Math.PI / 2) -
                                     Math.fround(row * latitudeStep));
        const pole = row === 0 || row === height;
        const xy = pole ? 0 : Math.fround(1000 * Math.fround(Math.cos(latitude)));
        const z = pole ? (row === 0 ? 1000 : -1000) :
                         Math.fround(1000 * Math.fround(Math.sin(latitude)));
        for (let column = 0; column <= width; ++column) {
            const longitude = Math.fround(column * longitudeStep);
            vertices[position++] = xy * Math.fround(Math.cos(longitude));
            vertices[position++] = xy * Math.fround(Math.sin(longitude));
            vertices[position++] = z;
            vertices[position++] = column / width;
            vertices[position++] = row / height;
        }
    }

    const strips = 2 * width + height - 2;
    const indexCount = 6 * width + 2 * (width + 1) * (height - 2) + strips - 1;
    const indices = new Uint32Array(indexCount);
    let index = 0;
    for (let row = 0; row < height; ++row) {
        const top = row * (width + 1);
        const bottom = top + width + 1;
        if (row === 0 || row === height - 1) {
            for (let column = 0; column < width; ++column) {
                if (index !== 0) indices[index++] = 0xffffffff;
                indices[index++] = top + column + (row === 0 ? 1 : 0);
                indices[index++] = bottom + column;
                indices[index++] = row === 0 ? bottom + column + 1 : top + column + 1;
            }
        } else {
            indices[index++] = 0xffffffff;
            for (let column = 0; column <= width; ++column) {
                indices[index++] = top + column;
                indices[index++] = bottom + column;
            }
        }
    }
    return { width, height, vertices, indices, vertexCount, triangleCount };
}

function earth_check_abort(signal) {
    if (signal?.aborted) {
        throw signal.reason === undefined ?
            new DOMException("Earth load aborted", "AbortError") : signal.reason;
    }
}

/* The one returned map is caller-owned. Larger maps assemble at most one
 * 16 KiB response at a time; no global cache or duplicate full map survives.
 * A one-file map uses the response buffer directly, without copying it. */
export async function earth_load(width, { signal } = {}) {
    earth_validate_width(width);
    earth_check_abort(signal);
    const length = width * (width / 2);
    const chunkCount = Math.ceil(length / 16384);
    const data = chunkCount === 1 ? null : new Uint8Array(length);
    for (let chunk = 0; chunk < chunkCount; ++chunk) {
        earth_check_abort(signal);
        const name = chunkCount === 1 ? `e${width}.bin` : `e${width}_${chunk}.bin`;
        const response = await fetch(new URL(`../data/${name}`, import.meta.url), { signal });
        earth_check_abort(signal);
        if (!response.ok) throw new Error(`Cannot load ${name}: HTTP ${response.status}`);
        const bytes = new Uint8Array(await response.arrayBuffer());
        earth_check_abort(signal);
        const expected = Math.min(16384, length - chunk * 16384);
        if (bytes.byteLength !== expected) {
            throw new Error(`Invalid ${name}: expected ${expected} bytes, received ${bytes.byteLength}`);
        }
        if (data === null) return bytes;
        data.set(bytes, chunk * 16384);
    }
    return data;
}
