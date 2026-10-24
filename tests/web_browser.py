#!/usr/bin/env python3
"""Real Chromium/WebGL browser checks; no hardware-GPU claim is implied.

Requires Python Playwright and Chromium (CHROMIUM_BIN overrides discovery).
The temporary HTTP server serves the repository root, including canonical data/.
Only the standard library is used apart from Playwright.
"""
import asyncio
from collections import Counter
from functools import partial
import hashlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import re
import shutil
import struct
import threading
import zlib

from playwright.async_api import async_playwright, expect


ROOT = Path(__file__).resolve().parents[1]
LEVELS = (32, 64, 128, 256, 512)


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def png_rows(data):
    """Decode Chromium's noninterlaced 8-bit RGB/RGBA screenshots."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "Screenshot is not PNG"
    offset, compressed = 8, bytearray()
    width = height = channels = None
    while offset < len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        offset += 12 + length
        if kind == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = struct.unpack(
                ">IIBBBBB", payload)
            assert depth == 8 and color in (2, 6) and interlace == 0
            assert compression == filtering == 0
            channels = 3 if color == 2 else 4
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
    assert width and height and channels
    raw = zlib.decompress(compressed)
    stride, position, previous = width * channels, 0, bytearray(width * channels)
    rows = []
    assert len(raw) == height * (stride + 1)
    for _ in range(height):
        mode = raw[position]
        row = bytearray(raw[position + 1:position + 1 + stride])
        position += stride + 1
        assert mode in (0, 1, 2, 3, 4)
        if mode:
            for i in range(stride):
                left = row[i - channels] if i >= channels else 0
                above = previous[i]
                upper_left = previous[i - channels] if i >= channels else 0
                if mode == 1:
                    predictor = left
                elif mode == 2:
                    predictor = above
                elif mode == 3:
                    predictor = (left + above) // 2
                else:
                    estimate = left + above - upper_left
                    distances = (abs(estimate - left), abs(estimate - above),
                                 abs(estimate - upper_left))
                    predictor = (left, above, upper_left)[distances.index(min(distances))]
                row[i] = (row[i] + predictor) & 255
        rows.append(row)
        previous = row
    return width, height, channels, rows


async def image_sample(page):
    # A compositor screenshot works with preserveDrawingBuffer=false. Limit the
    # comparison to the middle of the viewport so changing OSD/FPS text is excluded.
    image = await page.get_by_test_id("globe").screenshot(scale="css")
    width, height, channels, rows = png_rows(image)
    corners = [tuple(rows[y][x * channels:x * channels + 3])
               for x, y in ((1, 1), (width - 2, 1),
                            (1, height - 2), (width - 2, height - 2))]
    background = Counter(corners).most_common(1)[0][0]
    pixels = [tuple(rows[y][x * channels:x * channels + 3])
              for y in range(height // 5, height * 4 // 5, 3)
              for x in range(width // 5, width * 4 // 5, 3)]
    foreground = [p for p in pixels if sum(abs(p[i] - background[i]) for i in range(3)) > 24]
    shades = {tuple(component // 16 for component in pixel) for pixel in foreground}
    return {
        "signature": hashlib.sha256(bytes(component for p in pixels for component in p)).hexdigest(),
        "coverage": len(foreground) / len(pixels),
        "shades": len(shades),
        "dimensions": (width, height),
    }


def assert_globe(image):
    assert image["coverage"] > 0.08, f"Canvas has too little rendered geometry: {image}"
    assert image["shades"] >= 6, f"Canvas lacks Earth colors: {image}"


async def ready(page, level=None):
    await expect(page.locator("#viewport")).to_have_attribute("aria-busy", "false")
    if level is None:
        await page.wait_for_function("""() => !!document.querySelector(
            '[data-resolution][aria-pressed="true"]')""")
    else:
        await expect(page.get_by_test_id(f"resolution-{level}")).to_have_attribute("aria-pressed", "true")
    await page.evaluate("() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))")


async def active_level(page):
    values = await page.locator('[data-resolution][aria-pressed="true"]').evaluate_all(
        "buttons => buttons.map(button => Number(button.dataset.resolution))")
    assert len(values) == 1, f"Expected one active resolution, got {values}"
    return values[0]


async def check_counts(page, level):
    text = re.sub(r"[\s,]", "", await page.get_by_test_id("status").text_content())
    triangles = level * (level // 2 - 1) * 2
    assert re.search(rf"(?<!\d){triangles}triangles", text, re.I), text
    assert re.search(rf"(?<!\d){level}[×x]{level // 2}(?!\d)", text), text


async def context_page(browser, mobile=False):
    context = await browser.new_context(
        viewport={"width": 390 if mobile else 1120, "height": 844 if mobile else 800},
        device_scale_factor=1, is_mobile=mobile, has_touch=mobile)
    page = await context.new_page()
    page.set_default_timeout(15000)
    errors = []
    page.on("pageerror", lambda error: errors.append(str(error)))
    return context, page, errors


def assert_no_errors(errors):
    assert not errors, "Uncaught JavaScript errors: " + "\n".join(errors)


async def controls(browser, url):
    context, page, errors = await context_page(browser)
    try:
        await page.goto(url)
        await ready(page, 512)
        await expect(page.get_by_test_id("texture")).to_have_attribute("aria-pressed", "false")
        await check_counts(page, 512)
        assert_globe(await image_sample(page))
        for key, level in enumerate(LEVELS, 1):
            await page.get_by_test_id("globe").focus()
            await page.keyboard.press(str(key))
            await ready(page, level)
            await check_counts(page, level)
        await page.get_by_test_id("resolution-32").click()
        await ready(page, 32)
        flat = await image_sample(page)
        await page.get_by_test_id("texture").focus()
        await page.keyboard.press("Space")
        await expect(page.get_by_test_id("texture")).to_have_attribute("aria-pressed", "true")
        await expect(page.get_by_test_id("status")).to_contain_text(re.compile(r"texture\s+on", re.I))
        textured = await image_sample(page)
        assert_globe(textured)
        assert textured["signature"] != flat["signature"], "Texture toggle did not change rendered pixels"
        await page.get_by_test_id("globe").focus()
        await page.keyboard.press("t")
        await expect(page.get_by_test_id("texture")).to_have_attribute("aria-pressed", "false")
        await page.keyboard.press("o")
        await expect(page.get_by_test_id("osd")).to_have_attribute("aria-pressed", "false")
        await expect(page.get_by_test_id("status")).not_to_be_visible()
        await page.keyboard.press("o")
        await expect(page.get_by_test_id("status")).to_be_visible()
        await page.keyboard.press("f")
        await expect(page.get_by_test_id("fps")).to_have_attribute("aria-pressed", "false")
        await page.keyboard.press("f")
        await expect(page.get_by_test_id("fps")).to_have_attribute("aria-pressed", "true")
        await page.get_by_test_id("reset").click()
        original = await image_sample(page)
        bounds = await page.get_by_test_id("globe").bounding_box()
        x, y = bounds["x"] + bounds["width"] / 2, bounds["y"] + bounds["height"] / 2
        await page.mouse.move(x, y)
        await page.mouse.down()
        await page.mouse.move(x + 90, y + 45, steps=6)
        await page.mouse.up()
        dragged = await image_sample(page)
        assert dragged["signature"] != original["signature"], "Pointer drag did not rotate the globe"
        await page.get_by_test_id("reset").focus()
        await page.keyboard.press("Enter")
        assert (await image_sample(page))["signature"] == original["signature"], "Reset did not restore the original pose"
        await page.get_by_test_id("globe").focus()
        await page.keyboard.press("a")
        await expect(page.get_by_test_id("auto")).to_have_attribute("aria-pressed", "true")
        await page.wait_for_timeout(350)
        assert (await image_sample(page))["signature"] != original["signature"], "Automatic rotation did not move the globe"
        await page.keyboard.press("Escape")
        await expect(page.get_by_test_id("auto")).to_have_attribute("aria-pressed", "false")
        paused = await image_sample(page)
        await page.wait_for_timeout(200)
        assert (await image_sample(page))["signature"] == paused["signature"], "Escape did not pause animation"
        await page.mouse.move(x, y)
        await page.mouse.down()
        await page.keyboard.press("Escape")
        await page.mouse.move(x + 55, y + 30, steps=4)
        await page.mouse.up()
        assert (await image_sample(page))["signature"] == paused["signature"], "Escape did not end the active drag"
        await page.evaluate("""() => {
            const input = document.createElement('input'); input.id = 'outside-editor';
            document.body.appendChild(input); input.focus();
        }""")
        await page.keyboard.type("ta15of")
        assert await page.locator("#outside-editor").input_value() == "ta15of"
        assert await active_level(page) == 32
        await expect(page.get_by_test_id("auto")).to_have_attribute("aria-pressed", "false")
        await expect(page.get_by_test_id("texture")).to_have_attribute("aria-pressed", "false")
        await page.locator("#outside-editor").evaluate("element => element.remove()")
        await page.set_viewport_size({"width": 860, "height": 650})
        await ready(page, 32)
        resized = await image_sample(page)
        assert_globe(resized)
        assert resized["dimensions"] != paused["dimensions"], "Canvas did not resize with the page"
        assert_no_errors(errors)
        print("PASS browser WebGL: rendered Earth colors, five LOD counts, texture pixels, keyboard/buttons, drag/reset, animation/Escape, focus isolation and resize")
    finally:
        await context.close()


async def fetch_recovery(browser, url):
    context, page, errors = await context_page(browser)
    try:
        async def unavailable(route):
            await route.fulfill(status=503, content_type="text/plain", body="Injected unavailable map")
        await page.route("**/data/e512_*.bin", unavailable)
        await page.goto(url)
        await ready(page)
        assert await active_level(page) == 256, "Unavailable startup map did not select the next supported level"
        await check_counts(page, 256)
        assert (await page.get_by_test_id("notice").inner_text()).strip()
        assert_globe(await image_sample(page))
        await page.unroute("**/data/e512_*.bin")
        await page.get_by_test_id("retry").click()
        await ready(page, 512)
        await page.route("**/data/e64.bin", unavailable)
        await page.get_by_test_id("resolution-64").click()
        await ready(page)
        assert await active_level(page) == 512, "A failed replacement discarded the existing globe"
        assert (await page.get_by_test_id("notice").inner_text()).strip()
        await page.unroute("**/data/e64.bin")
        await page.get_by_test_id("retry").click()
        await ready(page, 64)
        await check_counts(page, 64)
        assert_globe(await image_sample(page))
        assert_no_errors(errors)
        print("PASS browser fetch recovery: startup lowers LOD, failed replacement retains the scene, explicit retries restore requested maps")
    finally:
        await context.close()


async def selection_race(browser, url):
    context, page, errors = await context_page(browser)
    gate, requested = asyncio.Event(), asyncio.Event()
    try:
        async def delayed(route):
            requested.set()
            await gate.wait()
            await route.continue_()
        await page.route("**/data/e512_*.bin", delayed)
        await page.goto(url, wait_until="commit")
        await asyncio.wait_for(requested.wait(), 15)
        await page.get_by_test_id("resolution-32").click()
        await ready(page, 32)
        gate.set()
        await page.wait_for_timeout(500)
        assert await active_level(page) == 32, "A stale asynchronous response replaced the selected resolution"
        await check_counts(page, 32)
        assert_globe(await image_sample(page))
        assert_no_errors(errors)
        print("PASS browser asynchronous selection: delayed old requests cannot replace the latest committed globe")
    finally:
        gate.set()
        await context.close()


async def texture_recovery(browser, url):
    context, page, errors = await context_page(browser)
    try:
        async def unavailable(route):
            await route.fulfill(status=503, content_type="text/plain", body="Injected unavailable map")
        await page.goto(url)
        await ready(page, 512)
        await page.get_by_test_id("resolution-32").click()
        await ready(page, 32)
        flat = await image_sample(page)
        await page.route("**/data/*.bin", unavailable)
        await page.get_by_test_id("texture").click()
        await expect(page.get_by_test_id("status")).to_contain_text("texture fail")
        assert await active_level(page) == 32
        assert (await image_sample(page))["signature"] == flat["signature"], "Texture failure changed the flat scene"
        await page.unroute("**/data/*.bin")
        await page.route("**/data/e512_*.bin", unavailable)
        await page.get_by_test_id("texture").click()
        await page.get_by_test_id("texture").click()
        await expect(page.get_by_test_id("status")).to_contain_text("texture on (256)")
        assert (await image_sample(page))["signature"] != flat["signature"]
        assert await active_level(page) == 32, "Texture fallback unexpectedly changed geometry detail"
        assert_no_errors(errors)
        print("PASS browser texture recovery: exhausted maps preserve flat rendering; explicit retry uses a smaller independent texture")
    finally:
        await context.close()


async def empty_recovery(browser, url):
    context, page, errors = await context_page(browser)
    try:
        async def unavailable(route):
            await route.fulfill(status=503, content_type="text/plain", body="Injected unavailable map")
        await page.route("**/data/*.bin", unavailable)
        await page.goto(url)
        await expect(page.locator("#viewport")).to_have_attribute("aria-busy", "false")
        await expect(page.get_by_test_id("status")).to_have_text("No globe loaded")
        await expect(page.get_by_test_id("retry")).to_be_visible()
        assert (await page.get_by_test_id("notice").inner_text()).strip()
        assert await page.locator('[data-resolution][aria-pressed="true"]').count() == 0
        await page.unroute("**/data/*.bin")
        await page.get_by_test_id("retry").click()
        await ready(page, 512)
        assert_globe(await image_sample(page))
        assert_no_errors(errors)
        print("PASS browser empty-scene recovery: all maps unavailable remains usable and Retry reconstructs the globe")
    finally:
        await context.close()


async def resize_recovery(browser, url):
    # Inject one real GL error at the public viewport boundary, then restore
    # the native method before Retry. No production test hooks are needed.
    fault = """(() => {
        const prototype = WebGL2RenderingContext.prototype, viewport = prototype.viewport;
        prototype.viewport = function(...args) {
            prototype.viewport = viewport;
            viewport.call(this, 0, 0, -1, 1);
        };
    })();"""
    for initial in (False, True):
        context, page, errors = await context_page(browser)
        try:
            if initial:
                await page.add_init_script(fault)
            await page.goto(url)
            if not initial:
                await ready(page, 512)
                await page.evaluate(fault)
                await page.set_viewport_size({"width": 860, "height": 650})
            await expect(page.get_by_test_id("notice")).to_contain_text("could not be resized")
            await expect(page.get_by_test_id("retry")).to_be_visible()
            await page.get_by_test_id("retry").click()
            await ready(page, 512)
            await expect(page.get_by_test_id("retry")).to_be_hidden()
            assert_globe(await image_sample(page))
            assert_no_errors(errors)
        finally:
            await context.close()
    print("PASS browser resize recovery: initial and later native resize errors offer Retry and rebuild a usable drawing area")


async def resource_recovery(browser, url):
    context, page, errors = await context_page(browser)
    try:
        await page.add_init_script("""(() => {
            const prototype = WebGL2RenderingContext.prototype, createBuffer = prototype.createBuffer;
            prototype.createBuffer = function() {
                prototype.createBuffer = createBuffer;
                return null;
            };
        })();""")
        await page.goto(url)
        await ready(page, 256)
        await check_counts(page, 256)
        assert_globe(await image_sample(page))
        await expect(page.get_by_test_id("retry")).to_be_visible()
        await page.get_by_test_id("retry").click()
        await ready(page, 512)
        assert_globe(await image_sample(page))
        assert_no_errors(errors)
        print("PASS browser resource recovery: unavailable native buffer lowers startup detail and Retry restores the requested scene")
    finally:
        await context.close()


async def context_recovery(browser, url):
    context, page, errors = await context_page(browser)
    try:
        await page.goto(url)
        await ready(page, 512)
        await page.get_by_test_id("resolution-128").click()
        await ready(page, 128)
        await page.get_by_test_id("texture").click()
        await expect(page.get_by_test_id("status")).to_contain_text(re.compile(r"texture\s+on", re.I))
        await page.get_by_test_id("auto").click()
        extension = await page.get_by_test_id("globe").evaluate_handle(
            "canvas => canvas.getContext('webgl2').getExtension('WEBGL_lose_context')")
        if not await extension.evaluate("extension => !!extension"):
            print("SKIP browser context restoration: WEBGL_lose_context is unavailable")
            return
        await extension.evaluate("extension => extension.loseContext()")
        await expect(page.get_by_test_id("notice")).to_contain_text(re.compile(r"context", re.I))
        await page.wait_for_timeout(150)
        await extension.evaluate("extension => extension.restoreContext()")
        await ready(page, 128)
        await expect(page.get_by_test_id("texture")).to_have_attribute("aria-pressed", "true")
        await expect(page.get_by_test_id("auto")).to_have_attribute("aria-pressed", "true")
        await expect(page.get_by_test_id("status")).to_contain_text(re.compile(r"texture\s+on", re.I))
        assert_globe(await image_sample(page))
        assert_no_errors(errors)
        print("PASS browser WebGL context recovery: readable loss state and restored geometry, texture and requested animation")
    finally:
        await context.close()


async def unavailable_webgl(browser, url):
    context, page, errors = await context_page(browser)
    try:
        await page.add_init_script("""(() => {
            const original = HTMLCanvasElement.prototype.getContext;
            HTMLCanvasElement.prototype.getContext = function(kind, ...args) {
                return String(kind).includes('webgl') ? null : original.call(this, kind, ...args);
            };
        })();""")
        await page.goto(url)
        await expect(page.locator("#unavailable")).to_be_visible()
        await expect(page.locator("#unavailable")).to_contain_text(re.compile(r"WebGL", re.I))
        assert_no_errors(errors)
        print("PASS browser unavailable WebGL: readable failure without an uncaught JavaScript exception")
    finally:
        await context.close()


async def driver_contract(browser, url):
    context, page, errors = await context_page(browser)
    try:
        # Load the public renderer API in an isolated same-origin document.
        await page.route("**/web/driver-contract", lambda route: route.fulfill(
            status=200, content_type="text/html", body="<!doctype html><canvas></canvas>"))
        await page.goto(url + "driver-contract")
        result = await page.evaluate("""async () => {
            const { drv_create } = await import('/web/drv_wgl.js');
            const { globe_build_mesh, earth_load } = await import('/web/globe.js');
            const check = (ok, why) => { if (!ok) throw new Error(why); };
            const canvas = document.querySelector('canvas'), driver = drv_create(canvas);
            const gl = canvas.getContext('webgl2');
            try {
                driver.resize(320, 240);
                driver.setScene(globe_build_mesh(128), await earth_load(128));
                driver.setTexture(512, await earth_load(512));
                const pixels = new Uint8Array(320 * 240 * 4);
                const read = () => {
                    driver.draw({ rotationX: 15, rotationY: 40, textured: true });
                    gl.readPixels(0, 0, 320, 240, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
                    return pixels.slice();
                };
                const original = read();
                const bufferData = gl.bufferData;
                gl.bufferData = () => { throw new Error('Injected buffer allocation failure'); };
                let failed = false;
                try { driver.setScene(globe_build_mesh(32), await earth_load(32)); }
                catch { failed = true; }
                finally { gl.bufferData = bufferData; }
                check(failed && driver.scene.width === 128, 'Failed scene upload lost the original scene');
                check(read().every((v, i) => v === original[i]), 'Scene rollback changed visible pixels');
                const texImage2D = gl.texImage2D;
                gl.texImage2D = () => { gl.enable(0xffffffff); gl.viewport(0, 0, -1, 1); };
                failed = false;
                try { driver.setTexture(256, await earth_load(256)); }
                catch { failed = true; }
                finally { gl.texImage2D = texImage2D; }
                check(failed && driver.hasTexture && driver.textureWidth === 512,
                    'Failed texture upload lost the original texture');
                check(read().every((v, i) => v === original[i]), 'Texture rollback changed visible pixels');
                check(gl.getError() === gl.NO_ERROR, 'Upload rollback left pending GL errors');
                const dimensions = driver.resize(10000, 5000, 5);
                check(dimensions.width <= 4096 && dimensions.height <= 4096 &&
                    dimensions.width === dimensions.height * 2, 'Resize failed its bounded aspect ratio');
                return { renderer: driver.info.name, dimensions };
            } finally {
                driver.dispose(); driver.dispose();
                check(!driver.hasTexture && driver.scene === null, 'Disposal retained live scene metadata');
            }
        }""")
        assert result["renderer"] == "WebGL 2"
        assert_no_errors(errors)
        print("PASS browser driver contract: scene/texture upload rollback preserves exact rendered pixels, GL errors drain, resize is bounded, disposal is idempotent")
    finally:
        await context.close()


async def touch_controls(browser, url):
    context, page, errors = await context_page(browser, mobile=True)
    try:
        await page.goto(url)
        await ready(page, 512)
        await page.get_by_test_id("resolution-64").tap()
        await ready(page, 64)
        await page.get_by_test_id("globe").scroll_into_view_if_needed()
        original = await image_sample(page)
        bounds = await page.get_by_test_id("globe").bounding_box()
        x, y = bounds["x"] + bounds["width"] * 0.45, bounds["y"] + bounds["height"] * 0.5
        session = await context.new_cdp_session(page)
        await session.send("Input.dispatchTouchEvent", {"type": "touchStart", "touchPoints": [{"x": x, "y": y}]})
        # A zero-duration synthetic flick makes Chromium suppress the next tap.
        # Give the native recognizer a realistic contact and continuous motion.
        await page.wait_for_timeout(80)
        for step in range(1, 6):
            await session.send("Input.dispatchTouchEvent", {"type": "touchMove", "touchPoints": [
                {"x": x + 55 * step / 5, "y": y + 25 * step / 5}]})
            await page.wait_for_timeout(30)
        await session.send("Input.dispatchTouchEvent", {"type": "touchEnd", "touchPoints": []})
        assert (await image_sample(page))["signature"] != original["signature"], "Touch drag did not rotate the globe"
        await page.get_by_test_id("reset").tap()
        assert (await image_sample(page))["signature"] == original["signature"], "Touch reset did not restore the pose"
        assert await page.evaluate("document.documentElement.scrollWidth <= window.innerWidth"), "Mobile page overflows horizontally"
        assert_no_errors(errors)
        print("PASS browser touch controls: responsive layout, resolution buttons, native touch drag and reset")
    finally:
        await context.close()


async def run_browser(url):
    executable = os.environ.get("CHROMIUM_BIN") or shutil.which("chromium") or shutil.which("chromium-browser")
    options = {
        "headless": True,
        "args": ["--use-gl=angle", "--use-angle=swiftshader", "--enable-webgl",
                 "--ignore-gpu-blocklist", "--enable-unsafe-swiftshader"],
    }
    if executable:
        options["executable_path"] = executable
    async with async_playwright() as playwright:
        browser = await playwright.chromium.launch(**options)
        try:
            await controls(browser, url)
            await fetch_recovery(browser, url)
            await selection_race(browser, url)
            await texture_recovery(browser, url)
            await empty_recovery(browser, url)
            await resize_recovery(browser, url)
            await resource_recovery(browser, url)
            await context_recovery(browser, url)
            await unavailable_webgl(browser, url)
            await driver_contract(browser, url)
            await touch_controls(browser, url)
        finally:
            await browser.close()


def main():
    server = ThreadingHTTPServer(("127.0.0.1", 0), partial(QuietHandler, directory=str(ROOT)))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        asyncio.run(run_browser(f"http://127.0.0.1:{server.server_port}/web/"))
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


if __name__ == "__main__":
    main()
