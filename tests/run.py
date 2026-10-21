#!/usr/bin/env python3
"""Host regression checks: geometry, SDK-backed Matrox, manual-derived XGL calls.

No test here emulates Microsoft C's segmented code generation, a Matrox board,
or Sun's native XGL implementation.
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
