#!/usr/bin/env python3
"""Host regressions: geometry, OpenGL, Matrox, XGL, GDI, QuickDraw and IRIS GL.

No test here emulates Microsoft C's segmented code generation, a Matrox board,
Sun's native XGL implementation, Windows 3.1 GDI, the Macintosh Toolbox,
or SGI's native IRIS GL implementation and IRIX ABI.
"""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def run(command, expected_returncode=0, expected_stderr=None, required_output=None):
    try:
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True,
                                timeout=60)
    except subprocess.TimeoutExpired:
        raise SystemExit("Timed out: " + shlex.join(str(part) for part in command))
    if (result.returncode != expected_returncode or
            (expected_stderr is not None and result.stderr != expected_stderr) or
            (required_output is not None and required_output not in result.stdout)):
        print("COMMAND:", shlex.join(str(part) for part in command))
        print(result.stdout, end="")
        print(result.stderr, end="")
        raise SystemExit(result.returncode or 1)
    for line in result.stdout.splitlines():
        if line.startswith("PASS"):
            print(line)
    if result.stderr and expected_stderr is None:
        print(result.stderr, end="")


def check_driver_api(command, driver, hidden_symbols):
    run(command)
    for symbol in hidden_symbols:
        result = subprocess.run(command + ["-DTEST_HIDDEN_SYMBOL=" + symbol],
                                cwd=ROOT, text=True, capture_output=True)
        if result.returncode == 0 or symbol not in result.stderr:
            print("COMMAND:", shlex.join(str(part) for part in command))
            print(result.stdout, end="")
            print(result.stderr, end="")
            raise SystemExit("Unsupported rendering API was exposed: " + symbol)
    print("PASS " + driver + " rendering API: supported callbacks declared, unsupported primitives hidden and plural globe entry points")


def check_configurations(cc, common):
    source = ["-fsyntax-only", str(ROOT / "tests/configuration.c")]
    clean = ["-U__APPLE__", "-U__MACH__", "-Uapplec", "-U__SC__", "-Usparc", "-U__sparc"]
    modes = (
        ["-D__APPLE__", "-D__MACH__", "-DTEST_DRIVER_OGL"],
        ["-D__APPLE__", "-DTEST_DRIVER_QD"],
        ["-Dapplec", "-DTEST_DRIVER_QD"],
        ["-D__SC__", "-DTEST_DRIVER_QD"],
        ["-Dsparc", "-DTEST_DRIVER_XGL"],
        ["-DDRV_GDI", "-DTEST_DRIVER_GDI"],
        ["-DDRV_MTX", "-DTEST_DRIVER_MTX"],
        ["-DDRV_QD", "-DTEST_DRIVER_QD"],
        ["-DDRV_OGL", "-D__APPLE__", "-DTEST_DRIVER_OGL"],
        ["-DDRV_OGL", "-D__BORLANDC__", "-DTEST_DRIVER_OGL"],
    )
    for options in modes:
        run(cc + common + clean + options + source)
    invalid = (
        (["-D__BORLANDC__"], "Select a supported graphics driver"),
        (["-DDRV_PGC"], "retained PGC/PHIGS headers have no implemented driver"),
        (["-DDRV_PHG"], "retained PGC/PHIGS headers have no implemented driver"),
        (["-DDRV_OGL", "-DFLAT", "-DSEG86"], "Choose exactly one memory architecture"),
    )
    for options, expected in invalid:
        result = subprocess.run(cc + common + clean + options + source,
                                cwd=ROOT, text=True, capture_output=True, timeout=60)
        if result.returncode == 0 or expected not in result.stderr:
            print(result.stdout, end="")
            print(result.stderr, end="")
            raise SystemExit("Configuration guard failed: " + " ".join(options))
    print("PASS build configuration: platform defaults infer matching memory models, explicit drivers win, unsupported legacy selections and conflicting architectures fail clearly")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", default=os.environ.get("MATROX_SDK"),
                        help="SDK root containing XGX1/HEADERS (headers are not vendored)")
    parser.add_argument("--xgl-contract", action="store_true",
                        help="Record XGL calls using manual-derived declarations and real X11 under Xvfb; not native XGL validation")
    parser.add_argument("--gdi-contract", action="store_true",
                        help="Record Win16 GDI calls with host declarations; not Microsoft C 7 ABI or Windows rendering validation")
    parser.add_argument("--qd-contract", action="store_true",
                        help="Record Classic Macintosh calls with host declarations; not MPW ABI or QuickDraw rendering validation")
    parser.add_argument("--igl-contract", action="store_true",
                        help="Record IRIS GL calls and check software fallback pixels; not SGI SDK, native rendering or IRIX ABI validation")
    parser.add_argument("--ogl-contract", action="store_true",
                        help="Record GLUT format selection, indexed palettes and software pixel transfer; not graphics-driver validation")
    args = parser.parse_args()
    cc = shlex.split(os.environ.get("CC", "cc"))
    common = ["-std=c89", "-pedantic-errors", "-Wall", "-Wextra",
              "-Werror=implicit-function-declaration", "-Werror=incompatible-pointer-types",
              "-DNO_SINF", "-DDRV", "-g", "-fsanitize=address,undefined",
              "-fno-sanitize-recover=undefined",
              "-fno-omit-frame-pointer", "-I" + str(ROOT / "src")]
    with tempfile.TemporaryDirectory(prefix="testglobe-tests-") as temporary:
        output = Path(temporary)
        check_configurations(cc, common)
        flat = ["-DFLAT", "-DDRV_OGL"]
        tri_mode = ["-include", str(ROOT / "tests/tri_mode.h")]
        api_source = ["-fsyntax-only", str(ROOT / "tests/driver_api.c")]
        api_modes = (
            ("OpenGL", flat, ("drv_draw_quad_flat_color", "drv_draw_quadbatch_flat_color", "drv_draw_tristrip_flat_rgb", "drv_resize", "drv_texture_load")),
            ("IRIS GL", ["-DFLAT", "-DDRV_IGL"], ("drv_draw_tri_flat_rgb", "drv_draw_quad_flat_color", "drv_resize", "drv_texture_load")),
            ("Sun XGL", ["-DFLAT", "-DDRV_XGL"], ("drv_draw_tri_flat_color", "drv_draw_tristrip_flat_color", "drv_draw_quad_flat_color", "drv_resize", "drv_texture_load")),
            ("Matrox", ["-DSEG86", "-DDRV_MTX", "-D_huge="], ("drv_draw_tristrip_flat_color", "drv_draw_quadbatch_flat_color", "drv_draw_tri_flat_uv", "earth_data_128", "texture_generate")),
            ("GDI", ["-DSEG86", "-DDRV_GDI", "-D_huge="], ("drv_draw_tri_flat_color", "drv_draw_tri_flat_uv", "drv_draw_tristrip_flat_color", "drv_draw_quad_flat_rgb", "earth_data_32", "texture_generate", "drv_resize")),
            ("QuickDraw", ["-DSEG68", "-DDRV_QD"], ("drv_draw_tri_flat_color", "drv_draw_tri_flat_uv", "drv_draw_tristrip_flat_color", "drv_draw_quad_flat_rgb", "earth_data_32", "texture_generate", "drv_resize")),
        )
        for driver, options, hidden in api_modes:
            check_driver_api(cc + common + options + api_source, driver,
                             hidden + ("drv_fps_history_num",))
        run(cc + common + flat + tri_mode + api_source)
        run(cc + common + flat + [str(ROOT / "tests/driver_helpers.c"),
            str(ROOT / "src/drv.c"), "-lm", "-o", str(output / "flat_helpers")])
        run([str(output / "flat_helpers")])
        run(cc + common + flat + ["-DDEBUG=1", str(ROOT / "tests/driver_helpers.c"),
            str(ROOT / "src/drv.c"), "-lm", "-o", str(output / "debug_helpers")])
        run([str(output / "debug_helpers")])
        run(cc + common + flat + tri_mode + [str(ROOT / "tests/geometry.c"),
            str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
            str(ROOT / "src/err.c"), "-lm", "-o", str(output / "geometry")])
        run([str(output / "geometry")])
        for mode in ("nan", "infinity"):
            run([str(output / "geometry"), mode], expected_returncode=1,
                expected_stderr="ERROR - Invalid globe dimensions\n",
                required_output="PASS invalid globe radius: nonfinite input fails before replacing the existing mesh")
        run(cc + common + flat + [str(ROOT / "tests/ogl_strips.c"),
            str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
            str(ROOT / "src/err.c"), "-lm", "-o", str(output / "ogl_strips")])
        run([str(output / "ogl_strips")])
        strips = ["-DFLAT", "-DDRV_XGL"]
        run(cc + common + strips + [str(ROOT / "tests/strips.c"),
            str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
            str(ROOT / "src/err.c"), "-lm", "-o", str(output / "strips")])
        run([str(output / "strips")])
        gdi = ["-DSEG86", "-DDRV_GDI", "-D_huge=", "-D_far=", "-D_near=",
               "-I" + str(ROOT / "tests/win16_shim")]
        run(cc + common + gdi + [str(ROOT / "tests/driver_helpers.c"),
            str(ROOT / "src/drv.c"), "-lm", "-o", str(output / "segmented_helpers")])
        run([str(output / "segmented_helpers")])
        run(cc + common + gdi + [str(ROOT / "tests/quad_batches.c"),
            str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
            str(ROOT / "src/err.c"), "-lm", "-o", str(output / "gdi_quads")])
        run([str(output / "gdi_quads")])
        qd = ["-DSEG68", "-DDRV_QD", "-I" + str(ROOT / "tests/qd_shim")]
        run(cc + common + qd + [str(ROOT / "tests/quad_batches.c"),
            str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
            str(ROOT / "src/err.c"), "-lm", "-o", str(output / "qd_quads")])
        run([str(output / "qd_quads")])
        run(cc + common + ["-DFLAT", "-DDRV_IGL", str(ROOT / "tests/igl_strips.c"),
            str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
            str(ROOT / "src/err.c"), "-lm", "-o", str(output / "igl_strips")])
        run([str(output / "igl_strips")])
        if args.qd_contract:
            run(cc + common + qd + [str(ROOT / "tests/qd.c"),
                str(ROOT / "src/qd_fpu.c"), str(ROOT / "src/soft3d.c"),
                str(ROOT / "src/globe.c"),
                str(ROOT / "src/drv.c"), str(ROOT / "src/err.c"),
                "-lm", "-o", str(output / "qd")])
            for mode in ("mono", "1", "4", "8", "16", "32", "fpu", "falsefpu",
                         "oldcolor", "nogworld", "lockfail", "multi", "small640",
                         "small512", "large800", "tall640", "explicit",
                         "explicitwidth", "explicitheight"):
                run([str(output / "qd"), mode])
            for mode in ("polyfail", "patternfail"):
                run([str(output / "qd"), mode], expected_returncode=1,
                    expected_stderr="",
                    required_output="PASS QuickDraw fatal cleanup: all transient and persistent resources released; teardown is idempotent")
        if args.gdi_contract:
            run(cc + common + gdi + ["-fsyntax-only", str(ROOT / "src/main.c")])
            # Win16 MakeProcInstance requires FARPROC/WNDPROC casts;
            # GCC diagnoses those historical, intentional callback casts.
            run(cc + common + gdi + ["-Wno-cast-function-type", str(ROOT / "tests/gdi.c"),
                str(ROOT / "src/soft3d.c"), str(ROOT / "src/globe.c"),
                str(ROOT / "src/drv.c"),
                str(ROOT / "src/err.c"), "-lm", "-o", str(output / "gdi")])
            for mode in ("fpu", "fixed", "287", "small640", "small512", "tall640",
                         "fpu-escape", "fixed-escape", "maxframe"):
                run([str(output / "gdi"), mode])
            for mode in ("fatal-poly", "fatal-oom", "fatal-size"):
                run([str(output / "gdi"), mode], expected_returncode=1,
                    expected_stderr="",
                    required_output="PASS GDI fatal error: modal paint/timer/input dispatch cannot reenter rendering or partial scene state, and cleanup releases all resources")
            run(cc + common + gdi + ["-DDEBUG=1", "-Wno-cast-function-type",
                str(ROOT / "tests/gdi.c"), str(ROOT / "src/soft3d.c"),
                str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
                str(ROOT / "src/err.c"), "-lm", "-o", str(output / "gdi_debug")])
            run([str(output / "gdi_debug"), "fpu"])
        if args.xgl_contract:
            if not shutil.which("xvfb-run"):
                parser.error("--xgl-contract requires xvfb-run and Xvfb on PATH")
            xgl_cppflags = shlex.split(os.environ.get("XGL_TEST_CPPFLAGS", ""))
            xgl_ldflags = shlex.split(os.environ.get("XGL_TEST_LDFLAGS", ""))
            run(cc + common + strips + xgl_cppflags +
                ["-I" + str(ROOT / "tests/xgl_record"),
                 str(ROOT / "tests/xgl.c"), str(ROOT / "src/globe.c"),
                 str(ROOT / "src/drv.c"), str(ROOT / "src/texture.c"),
                 str(ROOT / "src/err.c")] + xgl_ldflags +
                ["-lm", "-lX11", "-o", str(output / "xgl")])
            for mode in ("double", "single"):
                run(["xvfb-run", "-a", "-s", "-screen 0 1024x768x24 -nolisten tcp",
                     str(output / "xgl"), mode])
        if args.ogl_contract:
            ogl = ["-DFLAT", "-DDRV_OGL", "-I" + str(ROOT / "tests/ogl_record")]
            ogl_sources = [str(ROOT / "tests/ogl.c"),
                str(ROOT / "src/texsoft.c"), str(ROOT / "src/globe.c"),
                str(ROOT / "src/drv.c"), str(ROOT / "src/texture.c"),
                str(ROOT / "src/err.c"), "-lm"]
            run(cc + common + ogl + ["-DNO_GENMIPMAP"] + ogl_sources +
                ["-o", str(output / "ogl")])
            for mode in ("rgba", "rgba-single", "rgb24", "rgb16", "indexed", "unknownalpha", "nodepth", "limited", "listfail"):
                run([str(output / "ogl"), mode])
            for mode in ("newlistfail", "listcompilefail"):
                run([str(output / "ogl"), mode],
                    expected_stderr="OpenGL error: Out of memory!\n" * 30,
                    required_output="PASS OpenGL display-list compilation failure: rejected or incomplete lists are deleted before complete immediate rendering")
            for mode in ("validationfail", "callfail"):
                run([str(output / "ogl"), mode],
                    expected_stderr="OpenGL error: Invalid operation!\n" * 30 if mode == "callfail" else "",
                    required_output="PASS OpenGL display-list use failure: invalid or rejected lists are deleted, immediate geometry is complete and OSD reports fail")
            texture_failures = {
                "gluerror": "Cannot build OpenGL texture mipmaps",
                "stagefail": "Cannot generate OpenGL texture image",
                "softmipfail": "Cannot allocate indexed OpenGL texture mipmaps",
                "softframefail": "Cannot prepare indexed OpenGL texture raster",
                "softtransferallocfail": "Cannot present indexed OpenGL texture raster",
                "softtransfererror": "Cannot present indexed OpenGL texture raster",
            }
            for mode, message in texture_failures.items():
                run([str(output / "ogl"), mode],
                    expected_stderr=("OpenGL error: Invalid operation!\n" if mode == "softtransfererror" else "") +
                                    "OpenGL texture: " + message + "\n",
                    required_output="PASS OpenGL recoverable texture failure:")
            for mode in ("losttexture", "bindinglost", "disabledtexture", "unloadedtexture"):
                message = "OpenGL texture data is not loaded" if mode == "unloadedtexture" else "OpenGL texture is not active"
                run([str(output / "ogl"), mode], expected_stderr="OpenGL texture: " + message + "\n",
                    required_output="PASS OpenGL actual texture use:")
            run(cc + common + ogl + ogl_sources + ["-o", str(output / "ogl_11")])
            run([str(output / "ogl_11"), "rgba"])
            modern_ogl = ogl + ["-DTEST_OGL_FREEGLUT"]
            run(cc + common + modern_ogl + ogl_sources + ["-o", str(output / "ogl_modern")])
            for mode in ("rgba", "legacy11", "missingproc", "nullversion", "badversion", "limited", "maxzero"):
                run([str(output / "ogl_modern"), mode])
            for mode in ("newlistfail", "listcompilefail"):
                run([str(output / "ogl_modern"), mode],
                    expected_stderr="OpenGL error: Out of memory!\n" * 30,
                    required_output="PASS OpenGL display-list compilation failure: rejected or incomplete lists are deleted before complete immediate rendering")
            for mode in ("validationfail", "callfail"):
                run([str(output / "ogl_modern"), mode],
                    expected_stderr="OpenGL error: Invalid operation!\n" * 30 if mode == "callfail" else "",
                    required_output="PASS OpenGL display-list use failure: invalid or rejected lists are deleted, immediate geometry is complete and OSD reports fail")
            for mode in ("texallocerror", "texuploaderror", "mipmaperror"):
                message = ("Cannot allocate OpenGL texture" if mode == "texallocerror"
                           else "Cannot build OpenGL texture mipmaps")
                run([str(output / "ogl_modern"), mode],
                    expected_stderr="OpenGL error: Out of memory!\nOpenGL texture: " + message + "\n",
                    required_output="PASS OpenGL recoverable texture failure:")
            for label, options in (("legacy", ogl), ("optional", modern_ogl)):
                obj = output / ("ogl_" + label + ".o")
                run(cc + common + options + ["-c", str(ROOT / "src/drv_ogl.c"), "-o", str(obj)])
                symbols = subprocess.run(["nm", "-u", str(obj)], text=True, capture_output=True, check=True).stdout
                if "glGenerateMipmap" in symbols or (label == "legacy" and "glutGetProcAddress" in symbols):
                    raise SystemExit("OpenGL 1.1 object unexpectedly links a later entry point")
            print("PASS OpenGL 1.1 object linkage: legacy and optional builds compile with GL 1.1 declarations and never import glGenerateMipmap")
            run(cc + common + ogl + ["-DNO_GENMIPMAP", "-DTEXTURE_CACHE_RASTER=1"] +
                ogl_sources + ["-o", str(output / "ogl_cached")])
            run([str(output / "ogl_cached"), "indexed"])
            run(cc + common + ogl + ["-DNO_GENMIPMAP"] + tri_mode + ogl_sources +
                ["-o", str(output / "ogl_tri")])
            for mode in ("rgba", "indexed"):
                run([str(output / "ogl_tri"), mode])
        if args.igl_contract:
            igl = ["-DFLAT", "-DDRV_IGL", "-I" + str(ROOT / "tests/igl_record")]
            run(cc + common + igl + ["-fsyntax-only", str(ROOT / "src/main.c")])
            run(cc + common + igl + [str(ROOT / "tests/igl.c"),
                str(ROOT / "src/texsoft.c"), str(ROOT / "src/globe.c"),
                str(ROOT / "src/drv.c"), str(ROOT / "src/texture.c"),
                str(ROOT / "src/err.c"), "-lm", "-o", str(output / "igl")])
            for mode in ("double", "single", "indexed", "indexed-single", "noz", "software", "unknown", "affine", "winquit", "winshut",
                         "rgba", "rgba-single", "rgb-single", "unknown-alpha"):
                run([str(output / "igl"), mode])
            run(cc + common + igl + ["-DIGL_TEST_OLD_HEADERS",
                str(ROOT / "tests/igl.c"), str(ROOT / "src/texsoft.c"),
                str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
                str(ROOT / "src/texture.c"), str(ROOT / "src/err.c"),
                "-lm", "-o", str(output / "igl_oldheaders")])
            run([str(output / "igl_oldheaders"), "oldheaders"])
            run(cc + common + igl + ["-DIGL_TEST_OLD_HEADERS", "-DIGL_TEST_SUBDIVIDE",
                str(ROOT / "tests/igl.c"), str(ROOT / "src/texsoft.c"),
                str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
                str(ROOT / "src/texture.c"), str(ROOT / "src/err.c"),
                "-lm", "-o", str(output / "igl_affine")])
            run([str(output / "igl_affine"), "affine"])
            run(cc + common + igl + ["-DTEXTURE_CACHE_RASTER=1",
                str(ROOT / "tests/igl.c"), str(ROOT / "src/texsoft.c"),
                str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
                str(ROOT / "src/texture.c"), str(ROOT / "src/err.c"),
                "-lm", "-o", str(output / "igl_cached")])
            run([str(output / "igl_cached"), "software"])
        if args.igl_contract or args.ogl_contract:
            run(cc + common + ["-DFLAT", "-DDRV_OGL",
                str(ROOT / "tests/texsoft.c"), str(ROOT / "src/texsoft.c"),
                "-lm", "-o", str(output / "texsoft")])
            run([str(output / "texsoft")])
            run(cc + common + ["-DFLAT", "-DDRV_OGL",
                str(ROOT / "tests/texture.c"), "-o", str(output / "texture")])
            run([str(output / "texture")])
        if not args.sdk:
            print("SKIP Matrox SDK contract checks: supply --sdk or MATROX_SDK")
            return
        headers = Path(args.sdk) / "XGX1" / "HEADERS"
        required = ("DEF.H", "LIBFN.H", "IOLIB.H")
        for name in required:
            if not (headers / name).is_file():
                parser.error("Missing SDK header: " + str(headers / name))
        normalized = output / "sdk" / "XGX1" / "HEADERS"
        normalized.mkdir(parents=True)
        for path in headers.iterdir():
            if path.is_file():
                # DOS uses Ctrl-Z as EOF; GCC treats it as an invalid C token.
                data = path.read_bytes().rstrip(b"\r\n\x1a") + b"\n"
                (normalized / path.name).write_bytes(data)
        segmented = ["-DSEG86", "-DDRV_MTX", "-D_huge=",
                     "-D__interrupt=", "-D__far=", "-D_far=", "-D_near=",
                     "-I" + str(ROOT / "tests/dos_shim"),
                     "-I" + str(output / "sdk")]
        run(cc + common + segmented + ["-fsyntax-only", str(ROOT / "src/main.c")])
        run(cc + common + segmented + [str(ROOT / "tests/matrox.c"),
            str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
            str(ROOT / "src/err.c"), "-lm", "-o", str(output / "matrox")])
        run([str(output / "matrox")])
        print("PASS host compatibility: strict C89 and supplied SDK prototypes")


if __name__ == "__main__":
    main()
