#!/usr/bin/env python3
"""Host regressions: geometry, Matrox SDK, XGL, GDI and QuickDraw contracts.

No test here emulates Microsoft C's segmented code generation, a Matrox board,
Sun's native XGL implementation, Windows 3.1 GDI, or the Macintosh Toolbox.
"""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def run(command):
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    if result.returncode:
        print("COMMAND:", shlex.join(str(part) for part in command))
        print(result.stdout, end="")
        print(result.stderr, end="")
        raise SystemExit(result.returncode)
    for line in result.stdout.splitlines():
        if line.startswith("PASS"):
            print(line)
    if result.stderr:
        print(result.stderr, end="")


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
    args = parser.parse_args()
    cc = shlex.split(os.environ.get("CC", "cc"))
    common = ["-std=c89", "-pedantic-errors", "-Wall", "-Wextra",
              "-Werror=implicit-function-declaration", "-Werror=incompatible-pointer-types",
              "-DNO_SINF", "-DDRV", "-g", "-fsanitize=address,undefined",
              "-fno-omit-frame-pointer", "-I" + str(ROOT / "src")]
    with tempfile.TemporaryDirectory(prefix="testglobe-tests-") as temporary:
        output = Path(temporary)
        flat = ["-DFLAT", "-DDRV_OGL"]
        run(cc + common + flat + [str(ROOT / "tests/geometry.c"),
            str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
            str(ROOT / "src/err.c"), "-lm", "-o", str(output / "geometry")])
        run([str(output / "geometry")])
        strips = ["-DFLAT", "-DDRV_XGL"]
        run(cc + common + strips + [str(ROOT / "tests/strips.c"),
            str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
            str(ROOT / "src/err.c"), "-lm", "-o", str(output / "strips")])
        run([str(output / "strips")])
        gdi = ["-DSEG86", "-DDRV_GDI", "-D_huge=", "-D_far=", "-D_near=",
               "-I" + str(ROOT / "tests/win16_shim")]
        run(cc + common + gdi + [str(ROOT / "tests/gdi_strips.c"),
            str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
            str(ROOT / "src/err.c"), "-lm", "-o", str(output / "gdi_strips")])
        run([str(output / "gdi_strips")])
        qd = ["-DSEG68", "-DDRV_QD", "-I" + str(ROOT / "tests/qd_shim")]
        run(cc + common + qd + [str(ROOT / "tests/qd_strips.c"),
            str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
            str(ROOT / "src/err.c"), "-lm", "-o", str(output / "qd_strips")])
        run([str(output / "qd_strips")])
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
        if args.gdi_contract:
            run(cc + common + gdi + ["-fsyntax-only", str(ROOT / "src/main.c")])
            # Win16 MakeProcInstance requires FARPROC/WNDPROC casts;
            # GCC diagnoses those historical, intentional callback casts.
            run(cc + common + gdi + ["-Wno-cast-function-type", str(ROOT / "tests/gdi.c"),
                str(ROOT / "src/soft3d.c"), str(ROOT / "src/globe.c"),
                str(ROOT / "src/drv.c"),
                str(ROOT / "src/err.c"), "-lm", "-o", str(output / "gdi")])
            for mode in ("fpu", "fixed", "287", "small640", "small512", "tall640",
                         "fpu-escape", "fixed-escape"):
                run([str(output / "gdi"), mode])
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
