/* Browser data/geometry regressions; no browser, GPU, packages or server. */
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { EARTH_LEVELS, earth_palette, globe_build_mesh, earth_load,
         globe_cull_profile, globe_cull_indices } from "../web/globe.js";

const root = new URL("../", import.meta.url);

function test_vertex(mesh, index) {
    return mesh.vertices.subarray(index * 5, index * 5 + 3);
}

function test_triangle(mesh, tuple, coverage) {
    const stride = mesh.width + 1;
    const rows = tuple.map(index => Math.floor(index / stride));
    const columns = tuple.map(index => index % stride);
    const row = Math.min(...rows);
    const column = Math.min(...columns);
    assert(row < mesh.height && column < mesh.width);
    const top = row * stride + column;
    const bottom = top + stride;
    const first = [top, bottom, top + 1];
    const second = [top + 1, bottom, bottom + 1];
    const bit = tuple[0] === first[0] ? 1 : 2;
    assert.deepEqual(tuple, bit === 1 ? first : second, "ordered C TRI tuple");
    if (row === 0) assert.equal(bit, 2, "north pole omits degenerate triangle");
    if (row === mesh.height - 1) assert.equal(bit, 1, "south pole omits degenerate triangle");
    const patch = row * mesh.width + column;
    assert.equal(coverage[patch] & bit, 0, "triangle submitted once");
    coverage[patch] |= bit;
    const [a, b, c] = tuple.map(index => test_vertex(mesh, index));
    const u = b.map((value, axis) => value - a[axis]);
    const v = c.map((value, axis) => value - a[axis]);
    const normal = [u[1] * v[2] - u[2] * v[1],
                    u[2] * v[0] - u[0] * v[2],
                    u[0] * v[1] - u[1] * v[0]];
    const outward = normal.reduce((sum, value, axis) => sum + value * (a[axis] + b[axis] + c[axis]), 0);
    assert(outward > 0, "outward winding with no degenerate facets");
}

function test_mesh(width) {
    const mesh = globe_build_mesh(width);
    const height = width / 2;
    assert.equal(mesh.width, width);
    assert.equal(mesh.height, height);
    assert.equal(mesh.vertexCount, (width + 1) * (height + 1));
    assert.equal(mesh.triangleCount, 2 * width * (height - 1));
    assert(mesh.vertices instanceof Float32Array);
    assert(mesh.indices instanceof Uint32Array);
    assert.equal(mesh.vertices.length, mesh.vertexCount * 5);
    for (let row = 0; row <= height; ++row) {
        const latitude = Math.PI / 2 - row * Math.PI / height;
        for (let column = 0; column <= width; ++column) {
            const base = (row * (width + 1) + column) * 5;
            const [x, y, z, u, v] = mesh.vertices.subarray(base, base + 5);
            assert([x, y, z, u, v].every(Number.isFinite));
            assert.equal(u, column / width);
            assert.equal(v, row / height);
            assert(Math.abs(Math.hypot(x, y, z) - 1000) < 0.001);
            const longitude = column * 2 * Math.PI / width;
            const reference = [1000 * Math.cos(latitude) * Math.cos(longitude),
                               1000 * Math.cos(latitude) * Math.sin(longitude),
                               1000 * Math.sin(latitude)];
            assert(Math.abs(x - reference[0]) < 0.001);
            assert(Math.abs(y - reference[1]) < 0.001);
            assert(Math.abs(z - reference[2]) < 0.001);
            if (row === 0 || row === height) {
                assert(x === 0 && y === 0);
                assert.equal(z, row === 0 ? 1000 : -1000);
            }
        }
    }
    const coverage = new Uint8Array(width * height);
    let strip = [], triangles = 0, restarts = 0;
    for (const index of mesh.indices) {
        if (index === 0xffffffff) {
            assert(strip.length === 3 || strip.length === 2 * (width + 1));
            strip = [];
            ++restarts;
            continue;
        }
        assert(index < mesh.vertexCount, "index stays inside the shared grid");
        strip.push(index);
        const facet = strip.length - 3;
        if (facet < 0) continue;
        const tuple = facet % 2 === 0 ? strip.slice(-3) :
            [strip.at(-2), strip.at(-3), strip.at(-1)];
        test_triangle(mesh, tuple, coverage);
        ++triangles;
    }
    assert.equal(strip.length, 3, "no trailing primitive restart");
    assert.equal(triangles, mesh.triangleCount);
    assert.equal(restarts, 2 * width + height - 3);
    for (let row = 0; row < height; ++row) {
        for (let column = 0; column < width; ++column) {
            assert.equal(coverage[row * width + column],
                         row === 0 ? 2 : row === height - 1 ? 1 : 3);
        }
    }
}

function* test_strip_triangles(indices, count = indices.length) {
    let first, second, vertices = 0;
    for (let i = 0; i < count; ++i) {
        const third = indices[i];
        if (third === 0xffffffff) { vertices = 0; continue; }
        if (vertices >= 2 && first !== second && first !== third && second !== third) {
            yield vertices & 1 ? [second, first, third] : [first, second, third];
        }
        first = second; second = third; ++vertices;
    }
}

function test_facing(mesh, tuple, eye) {
    const [a, b, c] = tuple.map(index => Array.from(test_vertex(mesh, index)));
    const u = b.map((value, axis) => value - a[axis]);
    const v = c.map((value, axis) => value - a[axis]);
    return (u[1]*v[2] - u[2]*v[1]) * (eye[0] - a[0]) +
           (u[2]*v[0] - u[0]*v[2]) * (eye[1] - a[1]) +
           (u[0]*v[1] - u[1]*v[0]) * (eye[2] - a[2]) > 0;
}

function test_culling(width) {
    const mesh = globe_build_mesh(width), profile = globe_cull_profile(mesh);
    assert(profile);
    assert(profile.rows.byteLength + profile.longitude.byteLength < mesh.vertices.byteLength / 10,
           "culling retains factored rows/longitudes, not another vertex grid");
    const poses = [[0, 180], [19, 37], [71, 302], [90, 0], [-90, 37],
                   [247, 121], [0, 0], [89.999, 179.999]];
    const eyes = poses.map(([x, y]) => {
        x *= Math.PI / 180; y *= Math.PI / 180;
        return [3000 * Math.cos(x) * Math.cos(y),
                -3000 * Math.cos(x) * Math.sin(y), 3000 * Math.sin(x)];
    });
    /* An axial camera exactly on a band plane exercises the rare case where
     * Float32 rounding requires checking a whole ambiguous band. */
    const row = Math.floor(profile.height / 4);
    const rt = profile.rows[2 * row], zt = profile.rows[2 * row + 1];
    const rb = profile.rows[2 * row + 2], zb = profile.rows[2 * row + 3];
    eyes.push([0, 0, (rb * zt - rt * zb) / (rb - rt)], [0, 0, 0]);
    let storage = null;
    for (let pose = 0; pose < eyes.length; ++pose) {
        const eye = eyes[pose], output = globe_cull_indices(profile, eye, storage);
        storage = output.indices;
        const retained = new Uint8Array(width * profile.height);
        let triangles = 0;
        for (const tuple of test_strip_triangles(output.indices, output.count)) {
            test_triangle(mesh, tuple, retained);
            assert(test_facing(mesh, tuple, eye), "only front-facing triangles submitted");
            ++triangles;
        }
        let expected = 0;
        for (const tuple of test_strip_triangles(mesh.indices)) {
            if (!test_facing(mesh, tuple, eye)) continue;
            const rows = tuple.map(index => Math.floor(index / (width + 1)));
            const columns = tuple.map(index => index % (width + 1));
            const row = Math.min(...rows), column = Math.min(...columns);
            const first = row * (width + 1) + column;
            const bit = tuple[0] === first ? 1 : 2;
            assert(retained[row * width + column] & bit, "no front-facing silhouette holes");
            ++expected;
        }
        assert.equal(triangles, expected);
        assert.equal(output.triangleCount, expected);
        if (pose < poses.length) {
            assert(output.triangleTests < mesh.triangleCount / 8,
                   "normal views classify bands rather than testing every triangle");
            assert(expected > 0 && expected < mesh.triangleCount / 2);
        }
    }
    const irregular = globe_build_mesh(width);
    irregular.vertices[5] += 1;
    assert.equal(globe_cull_profile(irregular), null, "unrelated geometry cannot use globe band bounds");
}

async function test_data() {
    const originalFetch = globalThis.fetch;
    const requests = [];
    let responseBuffer;
    globalThis.fetch = async (url, options) => {
        assert(url instanceof URL);
        assert.equal(options.signal, undefined);
        requests.push(fileURLToPath(url));
        const bytes = await readFile(url);
        responseBuffer = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
        return { ok: true, arrayBuffer: async () => responseBuffer };
    };
    try {
        for (const width of EARTH_LEVELS) {
            requests.length = 0;
            const data = await earth_load(width);
            const chunks = width <= 128 ? [`e${width}.bin`] :
                Array.from({ length: width === 256 ? 2 : 8 }, (_, index) => `e${width}_${index}.bin`);
            const expected = Buffer.concat(await Promise.all(chunks.map(name => readFile(new URL(`data/${name}`, root)))));
            assert(data instanceof Uint8Array);
            assert.equal(data.length, width * width / 2);
            assert.deepEqual(Buffer.from(data), expected);
            assert.deepEqual(requests, chunks.map(name => fileURLToPath(new URL(`data/${name}`, root))));
            if (chunks.length === 1) assert.equal(data.buffer, responseBuffer, "small maps adopt response storage");
            const second = await earth_load(width);
            assert.notEqual(second.buffer, data.buffer, "no retained map cache");
            assert.equal(requests.length, chunks.length * 2);
        }
        requests.length = 0;
        await assert.rejects(earth_load(33), RangeError);
        assert.equal(requests.length, 0);
        globalThis.fetch = async () => ({ ok: false, status: 404,
            arrayBuffer() { throw new Error("HTTP failure must not read its body"); } });
        await assert.rejects(earth_load(32), /e32\.bin: HTTP 404/);
        for (const length of [0, 511, 513]) {
            globalThis.fetch = async () => ({ ok: true, arrayBuffer: async () => new ArrayBuffer(length) });
            await assert.rejects(earth_load(32), /expected 512 bytes/);
        }
        let calls = 0;
        globalThis.fetch = async () => ({ ok: ++calls === 1, status: 404,
            arrayBuffer: async () => new ArrayBuffer(16384) });
        await assert.rejects(earth_load(512), /e512_1\.bin: HTTP 404/);
        assert.equal(calls, 2, "failed large map stops without returning partial data");

        const before = new AbortController();
        before.abort();
        calls = 0;
        globalThis.fetch = async () => { ++calls; throw new Error("unexpected fetch"); };
        await assert.rejects(earth_load(32, { signal: before.signal }), { name: "AbortError" });
        assert.equal(calls, 0);
        for (const phase of ["response", "body"]) {
            const controller = new AbortController();
            const reason = new Error(`abort ${phase}`);
            calls = 0;
            globalThis.fetch = async (url, { signal }) => {
                assert.equal(signal, controller.signal);
                ++calls;
                if (phase === "response") controller.abort(reason);
                return { ok: true, arrayBuffer: async () => {
                    controller.abort(reason);
                    return new ArrayBuffer(16384);
                } };
            };
            await assert.rejects(earth_load(512, { signal: controller.signal }), error => error === reason);
            assert.equal(calls, 1, "abort stops before loading later chunks");
        }
        globalThis.fetch = async () => { throw new TypeError("network unavailable"); };
        await assert.rejects(earth_load(32), /network unavailable/);
    } finally {
        globalThis.fetch = originalFetch;
    }
}

assert.deepEqual(EARTH_LEVELS, [32, 64, 128, 256, 512]);
assert(Object.isFrozen(EARTH_LEVELS));
for (const invalid of [0, -1, 33, 1024, NaN, Infinity, "32", null]) {
    assert.throws(() => globe_build_mesh(invalid), RangeError);
}
for (const width of EARTH_LEVELS) test_mesh(width);
for (const width of EARTH_LEVELS) test_culling(width);
assert.throws(() => globe_cull_indices(null, [0, 0, 3000]), RangeError);
assert.throws(() => globe_cull_indices(globe_cull_profile(globe_build_mesh(32)), [NaN, 0, 3000]), RangeError);
console.log("PASS WebGL band culling: all LODs, finite perspective eyes, exact source facets/colors/UV winding, seams/poles, axial grazing, bounded boundary tests and compact geometry");
const paletteSource = await readFile(new URL("src/e_cmap.h", root), "utf8");
const paletteRows = [...paletteSource.matchAll(/\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}/g)];
assert.equal(paletteRows.length, 256);
const referencePalette = Uint8Array.from(paletteRows.flatMap(row => row.slice(1).map(Number)));
assert.deepEqual(earth_palette, referencePalette);
assert.equal(earth_palette.length, 768);
await test_data();
console.log("PASS WebGL mesh: all five grids, C triangle tuples, restart parity, nondegenerate poles, winding and UVs");
console.log("PASS browser Earth data: canonical maps/palette, bounded chunk lengths, no cache, HTTP failures and cancellation");
