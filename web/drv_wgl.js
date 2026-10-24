/* testglobe WebGL 2 driver. SPDX-License-Identifier: GPL-3.0-or-later */
import { earth_palette } from "./globe.js";

const drv_vertex_source = `#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main() {
    gl_Position = uMVP * vec4(aPosition, 1.0);
    vUV = aUV;
}`;

const drv_fragment_source = `#version 300 es
precision highp float;
precision highp int;
in vec2 vUV;
uniform highp usampler2D uIndices;
uniform highp sampler2D uPalette;
uniform highp sampler2D uTexture;
uniform ivec2 uMapSize;
uniform bool uTextured;
out vec4 fragmentColor;
void main() {
    if (uTextured) {
        fragmentColor = vec4(texture(uTexture, vUV).rgb, 1.0);
    } else {
        // Each pair of triangles lies within one original C globe patch.
        // Quantization preserves that patch's color without a second vertex grid.
        ivec2 cell = clamp(ivec2(floor(vUV * vec2(uMapSize))),
                            ivec2(0), uMapSize - ivec2(1));
        uint index = texelFetch(uIndices, cell, 0).r;
        fragmentColor = vec4(texelFetch(uPalette, ivec2(int(index), 0), 0).rgb, 1.0);
    }
}`;

function drv_failure(message, code = "WEBGL_ERROR") {
    const error = new Error(message);
    error.code = code;
    return error;
}

function drv_check(gl, operation) {
    if (gl.isContextLost()) {
        throw drv_failure("WebGL context lost", "CONTEXT_LOST");
    }
    let error = gl.getError();
    if (error !== gl.NO_ERROR) {
        const errors = [];
        let allocationFailure = true;
        // One failed transaction can set several distinct error flags, for
        // example an upload and its mipmap generation. Drain those together
        // so the retained previous scene can still draw on a later call.
        for (let count = 0; error !== gl.NO_ERROR && count < 8; ++count) {
            errors.push(`0x${error.toString(16)}`);
            if (error !== gl.OUT_OF_MEMORY) allocationFailure = false;
            error = gl.getError();
        }
        if (gl.isContextLost()) throw drv_failure("WebGL context lost", "CONTEXT_LOST");
        throw drv_failure(`${operation}: WebGL error ${errors.join(", ")}`,
            allocationFailure ? "OUT_OF_MEMORY" : "WEBGL_ERROR");
    }
}

function drv_shader(gl, type, source) {
    const shader = gl.createShader(type);
    if (!shader) {
        drv_check(gl, "Create shader");
        throw drv_failure("Cannot create WebGL shader");
    }
    try {
        gl.shaderSource(shader, source);
        gl.compileShader(shader);
        drv_check(gl, "Compile shader");
        if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
            throw drv_failure(`Cannot compile WebGL shader: ${gl.getShaderInfoLog(shader) || "unknown error"}`);
        }
        return shader;
    } catch (error) {
        gl.deleteShader(shader);
        throw error;
    }
}

function drv_program(gl) {
    let vertex = null;
    let fragment = null;
    let program = null;
    try {
        vertex = drv_shader(gl, gl.VERTEX_SHADER, drv_vertex_source);
        fragment = drv_shader(gl, gl.FRAGMENT_SHADER, drv_fragment_source);
        program = gl.createProgram();
        if (!program) {
            drv_check(gl, "Create program");
            throw drv_failure("Cannot create WebGL program");
        }
        gl.attachShader(program, vertex);
        gl.attachShader(program, fragment);
        gl.linkProgram(program);
        drv_check(gl, "Link program");
        if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
            throw drv_failure(`Cannot link WebGL program: ${gl.getProgramInfoLog(program) || "unknown error"}`);
        }
        gl.detachShader(program, vertex);
        gl.detachShader(program, fragment);
        return program;
    } catch (error) {
        if (program) gl.deleteProgram(program);
        throw error;
    } finally {
        if (vertex) gl.deleteShader(vertex);
        if (fragment) gl.deleteShader(fragment);
    }
}

function drv_map_valid(width, bytes) {
    return Number.isInteger(width) && width >= 32 && width <= 512 &&
        (width & (width - 1)) === 0 && bytes instanceof Uint8Array &&
        bytes.length === width * (width / 2);
}

function drv_mesh_valid(mesh, bytes) {
    if (!mesh || !drv_map_valid(mesh.width, bytes) || mesh.height !== mesh.width / 2 ||
        !(mesh.vertices instanceof Float32Array) || !(mesh.indices instanceof Uint32Array) ||
        mesh.vertexCount !== (mesh.width + 1) * (mesh.height + 1) ||
        mesh.vertices.length !== mesh.vertexCount * 5 ||
        mesh.triangleCount !== 2 * mesh.width * (mesh.height - 1) ||
        mesh.indices.length === 0 || mesh.indices.length > 1000000) return false;
    for (const value of mesh.vertices) if (!Number.isFinite(value)) return false;
    for (const index of mesh.indices) {
        if (index !== 0xffffffff && index >= mesh.vertexCount) return false;
    }
    return true;
}

function drv_texture(gl, width, height, data, indexed, mipmaps) {
    const texture = gl.createTexture();
    if (!texture) {
        drv_check(gl, "Create texture");
        throw drv_failure("Cannot create WebGL texture", "OUT_OF_MEMORY");
    }
    try {
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
        gl.texImage2D(gl.TEXTURE_2D, 0, indexed ? gl.R8UI : gl.RGB8, width, height,
            0, indexed ? gl.RED_INTEGER : gl.RGB, gl.UNSIGNED_BYTE, data);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER,
            mipmaps ? gl.LINEAR_MIPMAP_LINEAR : gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, mipmaps ? gl.LINEAR : gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, mipmaps ? gl.REPEAT : gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, mipmaps ? gl.REPEAT : gl.CLAMP_TO_EDGE);
        if (mipmaps) gl.generateMipmap(gl.TEXTURE_2D);
        drv_check(gl, "Upload texture");
        return texture;
    } catch (error) {
        gl.deleteTexture(texture);
        throw error;
    }
}

function drv_scene_delete(gl, scene) {
    if (!scene) return;
    if (scene.vao) gl.deleteVertexArray(scene.vao);
    if (scene.vertices) gl.deleteBuffer(scene.vertices);
    if (scene.indices) gl.deleteBuffer(scene.indices);
    if (scene.colors) gl.deleteTexture(scene.colors);
}

/* Postmultiply a column-major matrix by a rotation in two coordinate axes.
 * For X use Y/Z, for Y use Z/X, and for Z use X/Y. */
function drv_rotate(matrix, first, second, degrees) {
    const radians = (degrees % 360) * Math.PI / 180;
    const cosine = Math.cos(radians);
    const sine = Math.sin(radians);
    for (let row = 0; row < 4; ++row) {
        const a = matrix[first * 4 + row];
        const b = matrix[second * 4 + row];
        matrix[first * 4 + row] = a * cosine + b * sine;
        matrix[second * 4 + row] = b * cosine - a * sine;
    }
}

function drv_matrix(matrix, aspect, rotationX, rotationY) {
    const near = 1;
    const far = 10000;
    const scale = 1 / Math.tan(45 * Math.PI / 360);
    matrix.fill(0);
    matrix[0] = scale / aspect;
    matrix[5] = scale;
    matrix[10] = (far + near) / (near - far);
    matrix[11] = -1;
    matrix[14] = -3000 * matrix[10] + 2 * far * near / (near - far);
    matrix[15] = 3000;
    drv_rotate(matrix, 1, 2, rotationX);
    drv_rotate(matrix, 2, 0, rotationY);
    drv_rotate(matrix, 1, 2, -90);
    drv_rotate(matrix, 0, 1, -90);
}

export function drv_create(canvas) {
    if (!canvas || typeof canvas.getContext !== "function") {
        throw drv_failure("A canvas is required", "BAD_INPUT");
    }
    const gl = canvas.getContext("webgl2", {
        alpha: false, depth: true, stencil: false, antialias: true,
        premultipliedAlpha: false, preserveDrawingBuffer: false
    });
    if (!gl) throw drv_failure("WebGL 2 is unavailable", "UNAVAILABLE");
    let program = null;
    let palette = null;
    let scene = null;
    let texture = null;
    let textureWidth = 0;
    let disposed = false;
    const matrix = new Float32Array(16);
    let uniforms;
    let info;
    let maxWidth;
    let maxHeight;
    let maxTexture;
    try {
        drv_check(gl, "Initialize WebGL");
        if (!(earth_palette instanceof Uint8Array) || earth_palette.length !== 768) {
            throw drv_failure("Invalid Earth palette", "BAD_DATA");
        }
        const attributes = gl.getContextAttributes();
        const viewport = gl.getParameter(gl.MAX_VIEWPORT_DIMS);
        const renderbuffer = gl.getParameter(gl.MAX_RENDERBUFFER_SIZE);
        maxTexture = gl.getParameter(gl.MAX_TEXTURE_SIZE);
        drv_check(gl, "Query WebGL limits");
        maxWidth = Math.min(4096, renderbuffer, viewport[0]);
        maxHeight = Math.min(4096, renderbuffer, viewport[1]);
        info = Object.freeze({
            name: "WebGL 2",
            version: String(gl.getParameter(gl.VERSION)),
            antialias: Boolean(attributes && attributes.antialias),
            depthBits: gl.getParameter(gl.DEPTH_BITS),
            colorBits: gl.getParameter(gl.RED_BITS) + gl.getParameter(gl.GREEN_BITS) +
                gl.getParameter(gl.BLUE_BITS) + gl.getParameter(gl.ALPHA_BITS)
        });
        drv_check(gl, "Query WebGL limits");
        if (maxWidth < 1 || maxHeight < 1 || maxTexture < 512 || info.depthBits < 1) {
            throw drv_failure("WebGL 2 lacks the required texture or depth buffers", "UNAVAILABLE");
        }
        program = drv_program(gl);
        uniforms = {};
        for (const name of ["uMVP", "uIndices", "uPalette", "uTexture", "uMapSize", "uTextured"]) {
            uniforms[name] = gl.getUniformLocation(program, name);
            if (uniforms[name] === null) throw drv_failure(`Missing WebGL uniform ${name}`);
        }
        gl.useProgram(program);
        gl.uniform1i(uniforms.uIndices, 0);
        gl.uniform1i(uniforms.uPalette, 1);
        gl.uniform1i(uniforms.uTexture, 2);
        gl.activeTexture(gl.TEXTURE1);
        palette = drv_texture(gl, 256, 1, earth_palette, false, false);
        gl.enable(gl.CULL_FACE);
        gl.frontFace(gl.CCW);
        gl.cullFace(gl.BACK);
        gl.enable(gl.DEPTH_TEST);
        gl.depthFunc(gl.LEQUAL);
        gl.disable(gl.BLEND);
        gl.clearColor(0, 0, 0, 1);
        drv_check(gl, "Initialize rendering state");
    } catch (error) {
        if (palette) gl.deleteTexture(palette);
        if (program) gl.deleteProgram(program);
        throw error;
    }

    function drv_alive() {
        if (disposed) throw drv_failure("WebGL driver is disposed", "DISPOSED");
        drv_check(gl, "Use WebGL driver");
    }

    return Object.freeze({
        get hasTexture() { return texture !== null; },
        get textureWidth() { return textureWidth; },
        get width() { return gl.drawingBufferWidth; },
        get height() { return gl.drawingBufferHeight; },
        get scene() { return scene ? scene.stats : null; },
        get info() { return info; },

        setScene(mesh, indexBytes) {
            drv_alive();
            if (!drv_mesh_valid(mesh, indexBytes)) {
                throw drv_failure("Invalid globe mesh or Earth map", "BAD_DATA");
            }
            const replacement = { vao: null, vertices: null, indices: null, colors: null, stats: null };
            try {
                replacement.vao = gl.createVertexArray();
                replacement.vertices = gl.createBuffer();
                replacement.indices = gl.createBuffer();
                if (!replacement.vao || !replacement.vertices || !replacement.indices) {
                    drv_check(gl, "Create globe buffers");
                    throw drv_failure("Cannot create globe buffers", "OUT_OF_MEMORY");
                }
                gl.bindVertexArray(replacement.vao);
                gl.bindBuffer(gl.ARRAY_BUFFER, replacement.vertices);
                gl.bufferData(gl.ARRAY_BUFFER, mesh.vertices, gl.STATIC_DRAW);
                gl.enableVertexAttribArray(0);
                gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 20, 0);
                gl.enableVertexAttribArray(1);
                gl.vertexAttribPointer(1, 2, gl.FLOAT, false, 20, 12);
                gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, replacement.indices);
                gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, mesh.indices, gl.STATIC_DRAW);
                drv_check(gl, "Upload globe buffers");
                gl.activeTexture(gl.TEXTURE0);
                replacement.colors = drv_texture(gl, mesh.width, mesh.height, indexBytes, true, false);
                replacement.stats = Object.freeze({
                    width: mesh.width, height: mesh.height, vertexCount: mesh.vertexCount,
                    triangleCount: mesh.triangleCount, indexCount: mesh.indices.length
                });
            } catch (error) {
                drv_scene_delete(gl, replacement);
                throw error;
            } finally {
                gl.bindVertexArray(null);
                gl.bindBuffer(gl.ARRAY_BUFFER, null);
                gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, null);
            }
            const previous = scene;
            scene = replacement;
            drv_scene_delete(gl, previous);
            return scene.stats;
        },

        setTexture(width, indexBytes) {
            drv_alive();
            if (!drv_map_valid(width, indexBytes) || width > maxTexture) {
                throw drv_failure("Invalid Earth texture", "BAD_DATA");
            }
            let rgb;
            try {
                rgb = new Uint8Array(indexBytes.length * 3);
            } catch (error) {
                if (error instanceof RangeError) {
                    throw drv_failure("Cannot allocate RGB texture staging data", "OUT_OF_MEMORY");
                }
                throw error;
            }
            for (let index = 0; index < indexBytes.length; ++index) {
                const color = indexBytes[index] * 3;
                rgb[3 * index] = earth_palette[color];
                rgb[3 * index + 1] = earth_palette[color + 1];
                rgb[3 * index + 2] = earth_palette[color + 2];
            }
            gl.activeTexture(gl.TEXTURE2);
            const replacement = drv_texture(gl, width, width / 2, rgb, false, true);
            const previous = texture;
            texture = replacement;
            textureWidth = width;
            if (previous) gl.deleteTexture(previous);
        },

        resize(cssWidth, cssHeight, dpr = 1) {
            drv_alive();
            if (!Number.isFinite(cssWidth) || !Number.isFinite(cssHeight) ||
                !Number.isFinite(dpr) || cssWidth <= 0 || cssHeight <= 0 || dpr <= 0) {
                throw drv_failure("Invalid canvas dimensions", "BAD_INPUT");
            }
            const ratio = Math.min(dpr, 2, maxWidth / cssWidth, maxHeight / cssHeight);
            const width = Math.max(1, Math.floor(cssWidth * ratio));
            const height = Math.max(1, Math.floor(cssHeight * ratio));
            if (canvas.width !== width) canvas.width = width;
            if (canvas.height !== height) canvas.height = height;
            gl.viewport(0, 0, gl.drawingBufferWidth, gl.drawingBufferHeight);
            drv_check(gl, "Resize drawing buffer");
            return Object.freeze({ width: gl.drawingBufferWidth, height: gl.drawingBufferHeight, pixelRatio: ratio });
        },

        draw({ rotationX = 0, rotationY = 0, textured = false } = {}) {
            drv_alive();
            if (!Number.isFinite(rotationX) || !Number.isFinite(rotationY)) {
                throw drv_failure("Invalid globe rotation", "BAD_INPUT");
            }
            gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
            if (!scene || gl.drawingBufferWidth < 1 || gl.drawingBufferHeight < 1) return false;
            drv_matrix(matrix, gl.drawingBufferWidth / gl.drawingBufferHeight, rotationX, rotationY);
            gl.useProgram(program);
            gl.uniformMatrix4fv(uniforms.uMVP, false, matrix);
            gl.uniform2i(uniforms.uMapSize, scene.stats.width, scene.stats.height);
            const useTexture = Boolean(textured && texture);
            gl.uniform1i(uniforms.uTextured, useTexture ? 1 : 0);
            gl.activeTexture(gl.TEXTURE0);
            gl.bindTexture(gl.TEXTURE_2D, scene.colors);
            gl.activeTexture(gl.TEXTURE1);
            gl.bindTexture(gl.TEXTURE_2D, palette);
            gl.activeTexture(gl.TEXTURE2);
            // Every active sampler needs a compatible complete texture even
            // while its shader branch is disabled.
            gl.bindTexture(gl.TEXTURE_2D, texture || palette);
            gl.bindVertexArray(scene.vao);
            // WebGL 2 always enables fixed-index restart for indexed strips.
            gl.drawElements(gl.TRIANGLE_STRIP, scene.stats.indexCount, gl.UNSIGNED_INT, 0);
            gl.bindVertexArray(null);
            drv_check(gl, "Draw globe");
            return useTexture;
        },

        dispose() {
            if (disposed) return;
            disposed = true;
            // Deletion is harmless after context loss; never query or wait for
            // recovery here. The controller creates a new driver on restoration.
            drv_scene_delete(gl, scene);
            if (texture) gl.deleteTexture(texture);
            if (palette) gl.deleteTexture(palette);
            if (program) gl.deleteProgram(program);
            scene = texture = palette = program = null;
            textureWidth = 0;
        }
    });
}
