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

/* Keep the regular grid factored into rows and longitudes for CPU culling.
 * Boundary vertices reproduce the uploaded Float32 coordinates exactly; no
 * second full vertex grid or Earth map is retained by the WebGL driver. */
export function globe_cull_profile(mesh) {
    const { width, height, vertices } = mesh;
    const rows = new Float64Array(2 * (height + 1));
    const longitude = new Float32Array(2 * (width + 1));
    const step = Math.fround(2 * Math.PI / width);
    for (let column = 0; column <= width; ++column) {
        const angle = Math.fround(column * step);
        longitude[2 * column] = Math.fround(Math.cos(angle));
        longitude[2 * column + 1] = Math.fround(Math.sin(angle));
    }
    for (let row = 0; row <= height; ++row) {
        const first = row * (width + 1) * 5;
        const radius = vertices[first], z = vertices[first + 2];
        rows[2 * row] = radius;
        rows[2 * row + 1] = z;
        for (let column = 0; column <= width; ++column) {
            const index = first + column * 5;
            if (vertices[index] !== Math.fround(radius * longitude[2 * column]) ||
                vertices[index + 1] !== Math.fround(radius * longitude[2 * column + 1]) ||
                vertices[index + 2] !== z) return null;
        }
    }
    return { width, height, rows, longitude, indexCapacity: mesh.indices.length };
}

/* Integer longitude cells whose midpoint lies inside a cyclic angular range.
 * These are constant-size ranges, including a possible split at the UV seam. */
function globe_cull_longitudes(width, phase, threshold) {
    if (threshold <= -1) return [[0, width]];
    if (threshold >= 1) return [];
    const step = 2 * Math.PI / width;
    const center = phase / step - 0.5;
    const half = Math.acos(threshold) / step;
    const ranges = [];
    for (let turn = -1; turn <= 1; ++turn) {
        const first = Math.max(0, Math.floor(center - half + turn * width) + 1);
        const end = Math.min(width, Math.ceil(center + half + turn * width));
        if (first < end) ranges.push([first, end]);
    }
    return ranges.sort((a, b) => a[0] - b[0]);
}

function globe_cull_front(profile, row, facet, eye, points) {
    const column = row === 0 || row === profile.height - 1 ? facet : facet >> 1;
    const second = row === 0 || (row !== profile.height - 1 && (facet & 1));
    const corners = second ? [[row, column + 1], [row + 1, column], [row + 1, column + 1]] :
        [[row, column], [row + 1, column], [row, column + 1]];
    for (let corner = 0; corner < 3; ++corner) {
        const [r, c] = corners[corner], radius = profile.rows[2 * r];
        points[3 * corner] = Math.fround(radius * profile.longitude[2 * c]);
        points[3 * corner + 1] = Math.fround(radius * profile.longitude[2 * c + 1]);
        points[3 * corner + 2] = profile.rows[2 * r + 1];
    }
    const ax = points[3] - points[0], ay = points[4] - points[1], az = points[5] - points[2];
    const bx = points[6] - points[0], by = points[7] - points[1], bz = points[8] - points[2];
    return (ay * bz - az * by) * (eye[0] - points[0]) +
           (az * bx - ax * bz) * (eye[1] - points[1]) +
           (ax * by - ay * bx) * (eye[2] - points[2]) > 0;
}

/* Solve each latitude band's face-plane inequality before visiting vertices.
 * Definite front/back ranges need no per-triangle tests. Only uncertainty
 * near the limb is checked against the actual Float32 triangles. A band
 * exactly grazing an axial view may require checking the entire band. */
export function globe_cull_indices(profile, eye, storage = null) {
    if (!profile || eye.length !== 3 || !eye.every(Number.isFinite)) {
        throw new RangeError("Invalid globe culling view");
    }
    const { width, height, rows } = profile;
    let indices = storage || new Uint32Array(profile.indexCapacity);
    let count = 0, triangleCount = 0, triangleTests = 0;
    const points = new Float64Array(9);
    const phase = Math.atan2(eye[1], eye[0]);
    const horizontal = Math.hypot(eye[0], eye[1]);
    const halfCosine = Math.cos(Math.PI / width);
    const radius = Math.max(Math.abs(rows[1]), Math.abs(rows[2 * height + 1]));
    const epsilon = 64 * 2 ** -23 * radius * (Math.hypot(...eye) + radius);
    const stride = width + 1;
    const append = value => {
        if (count === indices.length) {
            const larger = new Uint32Array(Math.max(16, indices.length * 2));
            larger.set(indices);
            indices = larger;
        }
        indices[count++] = value;
    };
    const restart = () => { if (count) append(0xffffffff); };
    for (let row = 0; row < height; ++row) {
        const top = row * stride, bottom = top + stride;
        const cap = row === 0 || row === height - 1;
        const facets = cap ? 1 : 2;
        const rt = rows[2 * row], zt = rows[2 * row + 1];
        const rb = rows[2 * row + 2], zb = rows[2 * row + 3];
        const nh = (zt - zb) / halfCosine, nz = rb - rt;
        const amplitude = nh * horizontal;
        const offset = nz * eye[2] - (rb * zt - rt * zb);
        const outer = amplitude === 0 ? (offset >= -epsilon ? [[0, width]] : []) :
            globe_cull_longitudes(width, phase, (-offset - epsilon) / amplitude);
        const inner = amplitude === 0 ? (offset > epsilon ? [[0, width]] : []) :
            globe_cull_longitudes(width, phase, (-offset + epsilon) / amplitude);
        const edges = [...new Set([0, width, ...outer.flat(), ...inner.flat()])].sort((a, b) => a - b);
        let run = -1;
        const emit = end => {
            if (run < 0) return;
            triangleCount += end - run;
            if (cap) {
                for (let column = run; column < end; ++column) {
                    restart();
                    append(top + column + (row === 0 ? 1 : 0));
                    append(bottom + column);
                    append(row === 0 ? bottom + column + 1 : top + column + 1);
                }
            } else {
                const vertex = i => (i & 1 ? bottom : top) + (i >> 1);
                restart();
                // Restart resets winding. One degenerate vertex restores an
                // odd-start run's original parity without adding a facet.
                if (run & 1) append(vertex(run));
                for (let i = run; i < end + 2; ++i) append(vertex(i));
            }
            run = -1;
        };
        for (let segment = 0; segment + 1 < edges.length; ++segment) {
            const first = edges[segment], end = edges[segment + 1];
            const inRange = ranges => ranges.some(([a, b]) => first >= a && first < b);
            if (!inRange(outer)) { emit(first * facets); continue; }
            if (inRange(inner)) { if (run < 0) run = first * facets; continue; }
            for (let facet = first * facets; facet < end * facets; ++facet) {
                ++triangleTests;
                if (globe_cull_front(profile, row, facet, eye, points)) {
                    if (run < 0) run = facet;
                } else emit(facet);
            }
        }
        emit(width * facets);
    }
    return { indices, count, triangleCount, triangleTests };
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
