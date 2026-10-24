/* testglobe browser application. SPDX-License-Identifier: GPL-3.0-or-later */
import { EARTH_LEVELS, earth_load, globe_build_mesh } from "./globe.js";
import { drv_create } from "./drv_wgl.js";

const canvas = document.querySelector("#globe");
const viewport = document.querySelector("#viewport");
const demo = document.querySelector("#demo");
const osd = document.querySelector("#osd");
const stats = document.querySelector("#stats");
const notice = document.querySelector("#notice");
const loading = document.querySelector("#loading");
const unavailable = document.querySelector("#unavailable");
const info = document.querySelector("#renderer-info");
const retry = document.querySelector("#retry");
const resolutionButtons = [...document.querySelectorAll("[data-resolution]")];
const buttons = {
  texture: document.querySelector("#toggle-texture"),
  auto: document.querySelector("#toggle-auto"),
  osd: document.querySelector("#toggle-osd"),
  fps: document.querySelector("#toggle-fps"),
  reset: document.querySelector("#reset-view"),
};
const state = {
  requestedWidth: 512, rotationX: 0, rotationY: 180,
  auto: false, osd: true, fps: true,
  textureRequested: false, texturePending: false, textureFailed: false, textureUsed: false,
  scenePending: false, ready: false, lost: false, paused: false,
};
const number = new Intl.NumberFormat("en-US");
let driver = null;
let frameId = 0;
let lastTick = null;
let previousFrame = null;
let sampleSeconds = 0;
let sampleFrames = 0;
let fps = null;
let sceneTicket = 0;
let textureTicket = 0;
let sceneAbort = null;
let textureAbort = null;
let pointer = null;

function app_notice(message, kind = "info") {
  notice.textContent = message;
  notice.dataset.kind = kind;
}

function app_reset_timing() {
  lastTick = previousFrame = fps = null;
  sampleFrames = sampleSeconds = 0;
}

function app_update() {
  const usable = driver !== null && !state.lost;
  for (const button of resolutionButtons) {
    button.disabled = !usable;
    button.setAttribute("aria-pressed", String(state.ready &&
      Number(button.dataset.resolution) === driver?.scene?.width));
  }
  for (const button of Object.values(buttons)) button.disabled = !usable;
  buttons.texture.setAttribute("aria-pressed", String(state.textureRequested));
  buttons.auto.setAttribute("aria-pressed", String(state.auto));
  buttons.osd.setAttribute("aria-pressed", String(state.osd));
  buttons.fps.setAttribute("aria-pressed", String(state.fps));
  osd.hidden = !state.osd;
  viewport.setAttribute("aria-busy", String(state.scenePending));
  loading.hidden = !state.scenePending || state.lost;
  if (driver) {
    info.textContent = `WebGL 2 · ${driver.width} × ${driver.height} pixels · triangle strips`;
  }
  if (!state.ready || !driver?.scene) {
    stats.textContent = state.lost ? "Graphics context lost" : "No globe loaded";
    return;
  }
  const scene = driver.scene;
  const pieces = [
    `${number.format(scene.triangleCount)} triangles`,
    `${scene.width} × ${scene.height} map`,
  ];
  if (state.fps) pieces.push(`${fps === null ? "—" : fps.toFixed(1)} FPS`);
  let texture = "texture off";
  if (state.textureRequested) {
    if (state.texturePending || (driver.hasTexture && !state.textureUsed &&
        !state.paused && !state.textureFailed)) texture = "texture loading";
    else if (state.textureUsed && !state.textureFailed) {
      texture = "texture on";
      if (driver.textureWidth !== 512) texture += ` (${driver.textureWidth})`;
    } else texture = "texture fail";
  }
  pieces.push(texture);
  stats.textContent = pieces.join(" · ");
}

function app_schedule() {
  if (!frameId && driver && !state.lost && !state.paused && !document.hidden) {
    frameId = requestAnimationFrame(app_frame);
  }
}

function app_frame(timestamp) {
  frameId = 0;
  if (!driver || state.lost || state.paused || document.hidden) return;
  if (state.auto && state.ready && lastTick !== null) {
    state.rotationY = (state.rotationY + (timestamp - lastTick) * .01) % 360;
  }
  lastTick = timestamp;
  try {
    state.textureUsed = driver.draw({ rotationX: state.rotationX, rotationY: state.rotationY,
      textured: state.textureRequested && !state.texturePending &&
        !state.textureFailed && driver.hasTexture });
  } catch (error) {
    if (state.lost || error.code === "CONTEXT_LOST") return;
    state.paused = true;
    state.auto = false;
    state.textureUsed = false;
    app_reset_timing();
    app_notice("Drawing paused after a graphics error. Use Retry to rebuild the scene.", "error");
    retry.hidden = false;
    app_update();
    return;
  }
  if (state.ready && state.fps && previousFrame !== null) {
    const elapsed = (timestamp - previousFrame) / 1000;
    if (elapsed > 0) {
      sampleSeconds += elapsed;
      ++sampleFrames;
      if (sampleSeconds >= .5) {
        fps = sampleFrames / sampleSeconds;
        sampleSeconds = sampleFrames = 0;
      }
    }
  }
  previousFrame = timestamp;
  app_update();
  if (state.auto && state.ready) app_schedule();
}

function app_resize() {
  if (!driver || state.lost) return;
  const box = viewport.getBoundingClientRect();
  try {
    driver.resize(box.width, box.height, window.devicePixelRatio || 1);
  } catch (error) {
    if (!state.lost && error.code !== "CONTEXT_LOST") {
      state.paused = true;
      state.auto = false;
      state.textureUsed = false;
      app_notice("The drawing area could not be resized. Try a smaller window or Retry.", "error");
      retry.hidden = false;
    }
  }
  app_reset_timing();
  app_update();
  app_schedule();
}

async function app_select_resolution(width) {
  if (!driver || state.lost) return;
  state.requestedWidth = width;
  if (state.ready && !state.paused && driver.scene?.width === width && !state.scenePending) return;
  sceneAbort?.abort();
  sceneAbort = new AbortController();
  const signal = sceneAbort.signal;
  const ticket = ++sceneTicket;
  const owner = driver;
  const hadScene = state.ready;
  state.scenePending = true;
  state.paused = false;
  unavailable.hidden = true;
  retry.hidden = true;
  app_notice(`Loading the ${width} × ${width / 2} Earth map.`);
  app_update();
  app_reset_timing();
  let failure = null;
  for (const candidate of EARTH_LEVELS.filter(level => level <= width).reverse()) {
    try {
      const indices = await earth_load(candidate, { signal });
      if (signal.aborted || ticket !== sceneTicket || owner !== driver) return;
      const mesh = globe_build_mesh(candidate);
      owner.setScene(mesh, indices);
      state.ready = true;
      state.scenePending = false;
      app_notice(candidate === width ? "Drag to rotate. Select a lower detail level to see individual polygons." :
        `The ${width}-wide globe is unavailable. Using ${candidate} × ${candidate / 2}; Retry requests ${width} again.`,
        candidate === width ? "info" : "warning");
      retry.hidden = candidate === width;
      app_reset_timing();
      app_update();
      app_schedule();
      if (state.textureRequested && !driver.hasTexture && !state.texturePending) app_load_texture();
      return;
    } catch (error) {
      if (signal.aborted || ticket !== sceneTicket || owner !== driver || state.lost) return;
      if (error.code === "CONTEXT_LOST") return;
      failure = error;
      if (hadScene) {
        state.scenePending = false;
        app_notice(`Could not prepare the ${width}-wide globe. Keeping ${owner.scene.width} × ${owner.scene.height}; Retry tries again.`, "warning");
        retry.hidden = false;
        app_update();
        app_schedule();
        return;
      }
      // A smaller map can recover a resource shortage or missing data. An
      // unrelated graphics error is not evidence that a lower LOD will help.
      if (error.code && error.code !== "OUT_OF_MEMORY") break;
    }
  }
  state.scenePending = false;
  state.ready = false;
  const reason = failure?.code === "OUT_OF_MEMORY" || failure instanceof RangeError ?
    "The globe could not fit in available memory. Use Retry or choose a resolution." :
    failure?.code ? "Graphics resources could not be created. Use Retry to try again." :
    "Earth data could not be loaded. Check your connection and use Retry, or choose a resolution.";
  app_notice(reason, "error");
  retry.hidden = false;
  app_update();
  app_schedule();
}

async function app_load_texture() {
  if (!driver || !state.ready || state.lost || !state.textureRequested) return;
  if (driver.hasTexture) {
    state.textureFailed = state.texturePending = false;
    app_update();
    app_schedule();
    return;
  }
  textureAbort?.abort();
  textureAbort = new AbortController();
  const signal = textureAbort.signal;
  const ticket = ++textureTicket;
  const owner = driver;
  state.texturePending = true;
  state.textureFailed = false;
  app_update();
  app_schedule();
  for (const width of [...EARTH_LEVELS].reverse()) {
    try {
      const indices = await earth_load(width, { signal });
      if (signal.aborted || ticket !== textureTicket || owner !== driver) return;
      owner.setTexture(width, indices);
      state.texturePending = false;
      if (width !== 512) app_notice(`Texture reduced to ${width} × ${width / 2}.`, "warning");
      app_reset_timing();
      app_update();
      app_schedule();
      return;
    } catch (error) {
      if (signal.aborted || ticket !== textureTicket || owner !== driver || state.lost) return;
      if (error.code === "CONTEXT_LOST") return;
      if (error.code && error.code !== "OUT_OF_MEMORY") break;
    }
  }
  state.texturePending = false;
  state.textureFailed = true;
  app_notice("Texture unavailable; using flat colors. Toggle Texture off and on to retry.", "warning");
  app_update();
  app_schedule();
}

function app_toggle_texture() {
  state.textureRequested = !state.textureRequested;
  state.textureUsed = false;
  state.textureFailed = false;
  if (state.textureRequested) app_load_texture();
  else {
    textureAbort?.abort();
    ++textureTicket;
    state.texturePending = false;
  }
  app_reset_timing();
  app_update();
  app_schedule();
}

function app_release_pointer() {
  if (pointer && canvas.hasPointerCapture(pointer.id)) canvas.releasePointerCapture(pointer.id);
  pointer = null;
}

function app_pause() {
  state.auto = false;
  app_release_pointer();
  app_reset_timing();
  app_update();
  app_schedule();
}

function app_initialise() {
  sceneAbort?.abort();
  textureAbort?.abort();
  ++sceneTicket;
  ++textureTicket;
  if (frameId) cancelAnimationFrame(frameId);
  frameId = 0;
  driver?.dispose();
  driver = null;
  state.ready = state.paused = state.scenePending = state.texturePending = state.textureFailed = state.textureUsed = false;
  app_reset_timing();
  try {
    driver = drv_create(canvas);
    unavailable.hidden = true;
    app_resize();
    if (!state.paused) app_select_resolution(state.requestedWidth);
  } catch (error) {
    unavailable.hidden = false;
    document.querySelector("#unavailable-title").textContent = "WebGL 2 could not start";
    document.querySelector("#unavailable-message").textContent =
      "Use a browser with WebGL 2 enabled. Graphics acceleration may be disabled or unavailable on this device.";
    info.textContent = "WebGL 2 unavailable";
    app_notice("This demo requires WebGL 2. You can retry after enabling it in your browser.", "error");
    retry.hidden = false;
    app_update();
  }
}

for (const button of resolutionButtons) {
  button.addEventListener("click", () => app_select_resolution(Number(button.dataset.resolution)));
}
buttons.texture.addEventListener("click", app_toggle_texture);
buttons.auto.addEventListener("click", () => {
  state.auto = !state.auto;
  app_reset_timing(); app_update(); app_schedule();
});
buttons.osd.addEventListener("click", () => { state.osd = !state.osd; app_update(); });
buttons.fps.addEventListener("click", () => {
  state.fps = !state.fps;
  app_reset_timing(); app_update(); app_schedule();
});
buttons.reset.addEventListener("click", () => {
  app_pause();
  state.rotationX = 0; state.rotationY = 180;
  app_schedule();
});
retry.addEventListener("click", () => {
  if (state.lost) return;
  if (!driver || state.paused) app_initialise();
  else app_select_resolution(state.requestedWidth);
});
document.addEventListener("keydown", event => {
  if (event.altKey || event.ctrlKey || event.metaKey || event.repeat ||
      event.target.isContentEditable || /^(INPUT|TEXTAREA|SELECT)$/.test(event.target.tagName)) return;
  if (event.target !== document.body && !demo.contains(event.target)) return;
  const key = event.key.toLowerCase();
  if (key === "escape") { app_pause(); event.preventDefault(); return; }
  if (!driver || state.lost) return;
  if (/^[1-5]$/.test(key)) app_select_resolution(EARTH_LEVELS[Number(key) - 1]);
  else if (key === "t") buttons.texture.click();
  else if (key === "a") buttons.auto.click();
  else if (key === "o") buttons.osd.click();
  else if (key === "f") buttons.fps.click();
  else return;
  event.preventDefault();
});
canvas.addEventListener("pointerdown", event => {
  if (!state.ready || state.lost || pointer || event.button !== 0 || !event.isPrimary) return;
  canvas.focus({ preventScroll: true });
  pointer = { id: event.pointerId, x: event.clientX, y: event.clientY };
  canvas.setPointerCapture(event.pointerId);
  event.preventDefault();
});
canvas.addEventListener("pointermove", event => {
  if (!pointer || pointer.id !== event.pointerId || !state.ready || state.lost) return;
  const box = canvas.getBoundingClientRect();
  if (box.width <= 0 || box.height <= 0) return;
  state.rotationY = (state.rotationY + (event.clientX - pointer.x) * 100 / box.width + 360) % 360;
  state.rotationX = (state.rotationX + (event.clientY - pointer.y) * 100 / box.height + 360) % 360;
  pointer.x = event.clientX; pointer.y = event.clientY;
  app_schedule();
});
for (const eventName of ["pointerup", "pointercancel", "lostpointercapture"]) {
  canvas.addEventListener(eventName, event => {
    if (pointer?.id === event.pointerId) app_release_pointer();
  });
}
canvas.addEventListener("webglcontextlost", event => {
  event.preventDefault();
  state.lost = true;
  state.ready = state.scenePending = state.texturePending = state.textureUsed = false;
  sceneAbort?.abort(); textureAbort?.abort();
  ++sceneTicket; ++textureTicket;
  if (frameId) cancelAnimationFrame(frameId);
  frameId = 0;
  app_release_pointer();
  driver?.dispose(); driver = null;
  unavailable.hidden = false;
  document.querySelector("#unavailable-title").textContent = "Rendering paused";
  document.querySelector("#unavailable-message").textContent = "The graphics context was lost. The globe will reload when the browser restores it.";
  app_notice("Waiting for the browser to restore the graphics context.", "warning");
  info.textContent = "WebGL 2 · context lost";
  retry.hidden = true;
  app_reset_timing(); app_update();
});
canvas.addEventListener("webglcontextrestored", () => { state.lost = false; app_initialise(); });
document.addEventListener("visibilitychange", () => {
  if (frameId) cancelAnimationFrame(frameId);
  frameId = 0;
  app_reset_timing();
  if (!document.hidden) app_schedule();
});
window.addEventListener("resize", app_resize);
if (typeof ResizeObserver !== "undefined") new ResizeObserver(app_resize).observe(viewport);
window.addEventListener("pagehide", () => {
  sceneAbort?.abort(); textureAbort?.abort();
  if (frameId) cancelAnimationFrame(frameId);
  frameId = 0;
  driver?.dispose(); driver = null;
});
window.addEventListener("pageshow", event => { if (event.persisted) app_initialise(); });
app_initialise();
