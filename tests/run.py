#!/usr/bin/env python3
"""Host regressions: geometry, OpenGL, Matrox, TIGA, XGL, GDI, QuickDraw, Amiga and IRIS GL.

No test here emulates Microsoft C's segmented code generation, a Matrox/TIGA board,
Sun's native XGL implementation, Windows 3.1 GDI, the Macintosh Toolbox,
AmigaOS graphics and the custom chips, or SGI's native IRIS GL and IRIX ABI.
"""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import zlib


ROOT = Path(__file__).resolve().parents[1]


def run(command, expected_returncode=0, expected_stderr=None, required_output=None,
        expected_stdout=None, cwd=ROOT, env=None):
    try:
        result = subprocess.run(command, cwd=cwd, env=env, text=True, capture_output=True,
                                timeout=60)
    except subprocess.TimeoutExpired:
        raise SystemExit("Timed out: " + shlex.join(str(part) for part in command))
    if (result.returncode != expected_returncode or
            (expected_stderr is not None and result.stderr != expected_stderr) or
            (expected_stdout is not None and result.stdout != expected_stdout) or
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



def check_tiga_extraction(executable, output):
    """Exercise real files around the driver; TIGA itself is a call recorder."""
    profiles = (
        "default", "single", "nomodule", "smallbuffer", "nomouse", "8086", "386", "486",
        "fpu", "8087", "287", "387", "badmodule", "wrongabi", "badscreen", "nocd", "videoerror", "oldversion",
        "nomode", "modeerror", "primitivesfail", "pagehang", "syncerror",
        "nooverlay", "badcrc", "truncated", "oversized", "badoffset", "reserved",
        "badmagic", "missingexe", "diskfull", "closefail", "notemp", "shortwrite",
        "removefail", "collision", "movedcwd", "readonlyexe", "fallbacktmp",
        "longtemp", "moduleioerror", "flushioerror",
    )
    payload = bytes((index * 73 + 19) & 255 for index in range(1549))
    image = bytearray(512)
    image[:2] = b"MZ"
    struct.pack_into("<HHHH", image, 2, 0, 1, 0, 2)
    trailer = struct.pack("<8sIIII", b"TGRLM001", len(image), len(payload),
                          zlib.crc32(payload), 0)
    for profile in profiles:
        work = output / ("tiga_" + profile)
        program = work / "program"
        temporary = work / "temporary"
        other = work / "other"
        program.mkdir(parents=True)
        temporary.mkdir()
        other.mkdir()
        source = program / "testglob.exe"
        data = bytes(image) + payload + trailer
        if profile == "nooverlay":
            data = bytes(image)
        elif profile == "badcrc":
            data = bytes(image) + bytes([payload[0] ^ 1]) + payload[1:] + trailer
        elif profile == "truncated":
            data = data[:-1]
        elif profile == "oversized":
            data = data[:-24] + struct.pack("<8sIIII", b"TGRLM001", len(image),
                                            1048577, zlib.crc32(payload), 0)
        elif profile == "badoffset":
            data = data[:-24] + struct.pack("<8sIIII", b"TGRLM001", 0xffffffff,
                                            len(payload), zlib.crc32(payload), 0)
        elif profile == "reserved":
            data = data[:-4] + struct.pack("<I", 1)
        elif profile == "badmagic":
            data = data[:-24] + b"TGRLM002" + data[-16:]
        if profile != "missingexe":
            source.write_bytes(data)
        env = dict(os.environ)
        env["TEMP"] = env["TMP"] = str(temporary)
        env["TGA_TEST_OTHER_CWD"] = str(other)
        cwd = work
        argument = str(source)
        if profile == "movedcwd":
            argument = str(source.relative_to(work))
        if profile == "notemp":
            env["TEMP"] = env["TMP"] = "/proc"
            cwd = Path("/proc")
        if profile == "fallbacktmp":
            blocked = work / "not_directory"
            blocked.write_bytes(b"keep")
            env["TEMP"] = str(blocked)
        if profile == "longtemp":
            env["TEMP"] = "X" * 65530
        if profile == "readonlyexe":
            source.chmod(0o444)
            program.chmod(0o555)
        try:
            run([str(executable), profile, argument], cwd=cwd, env=env)
        finally:
            if profile == "readonlyexe":
                program.chmod(0o755)
                source.chmod(0o644)
        assert list(temporary.iterdir()) == [], (profile, list(temporary.iterdir()))
        assert list(other.iterdir()) == [], (profile, list(other.iterdir()))
        assert sorted(path.name for path in program.iterdir()) == (
            [] if profile == "missingexe" else ["testglob.exe"])
        if source.exists():
            assert source.read_bytes() == data, profile
        assert not list(work.glob("*.RLM")) and not list(work.glob("*.rlm")), profile
    print("PASS TIGA embedded module: real-file extraction, exact bytes, changed working directory, read-only distribution, malformed payloads, short writes, exclusive-name collisions and cleanup/fallback")


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


def check_production_units(cc, common, options, output, driver, sources):
    """Compile real translation units without a fixture's preceding includes."""
    objects = output / (driver + "_units")
    objects.mkdir()
    for source in sources:
        run(cc + common + options + ["-c", str(ROOT / "src" / source),
            "-o", str(objects / (Path(source).stem + ".o"))])
    print("PASS " + driver + " production units: independent compilation with recorder declarations; native SDK/ABI validation remains separate")


def check_configurations(cc, common):
    source = ["-fsyntax-only", str(ROOT / "tests/configuration.c")]
    clean = ["-U__APPLE__", "-U__MACH__", "-Uapplec", "-U__SC__", "-Usparc", "-U__sparc",
             "-D_huge="]
    modes = (
        ["-D__APPLE__", "-D__MACH__", "-DTEST_DRIVER_OGL"],
        ["-D__APPLE__", "-DTEST_DRIVER_QD"],
        ["-Dapplec", "-DTEST_DRIVER_QD"],
        ["-D__SC__", "-DTEST_DRIVER_QD"],
        ["-Dsparc", "-DTEST_DRIVER_XGL"],
        ["-DDRV_GDI", "-DTEST_DRIVER_GDI"],
        ["-DDRV_MTX", "-DTEST_DRIVER_MTX"],
        ["-DDRV_TGA", "-DTEST_DRIVER_TGA"],
        ["-DDRV_QD", "-DTEST_DRIVER_QD"],
        ["-DDRV_IGL", "-DTEST_DRIVER_IGL"],
        ["-DDRV_AMI", "-DTEST_DRIVER_AMI"],
        ["-DDRV_OGL", "-DOGL_AMIGA", "-DTEST_DRIVER_OGL"],
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
        (["-DDRV_OGL", "-DDRV_IGL"], "Select only one graphics driver"),
        (["-DDRV_TGA", "-DDRV_MTX"], "Select only one graphics driver"),
        (["-DDRV_TGA", "-DFLAT"], "TIGA requires the SEG86 memory model"),
        (["-DDRV_GDI", "-DFLAT"], "Windows GDI requires the SEG86 memory model"),
        (["-DDRV_QD", "-DFLAT"], "Classic QuickDraw requires the SEG68 memory model"),
        (["-DDRV_AMI", "-DSEG68"], "Amiga requires the FLAT memory model"),
        (["-DDRV_AMI", "-DOGL_AMIGA"], "OGL_AMIGA requires the DRV_OGL driver"),
        (["-DDRV_OGL", "-DTRI"], "Select exactly one driver primitive"),
    )
    for options, expected in invalid:
        result = subprocess.run(cc + common + clean + options + source,
                                cwd=ROOT, text=True, capture_output=True, timeout=60)
        if result.returncode == 0 or expected not in result.stderr:
            print(result.stdout, end="")
            print(result.stderr, end="")
            raise SystemExit("Configuration guard failed: " + " ".join(options))
    print("PASS build configuration: platform defaults infer matching memory models, explicit drivers win, unsupported legacy selections and conflicting architectures fail clearly")


def check_earth_destinations(converter, output):
    work = output / "unsafe_destinations"
    maps = work / "maps"
    shutil.copytree(ROOT / "data", maps)

    def snapshot():
        entries = {}
        for path in work.rglob("*"):
            mode = path.lstat().st_mode
            name = str(path.relative_to(work))
            if stat.S_ISLNK(mode):
                entries[name] = ("link", os.readlink(path))
            elif stat.S_ISREG(mode):
                entries[name] = ("file", path.read_bytes())
            elif stat.S_ISDIR(mode):
                entries[name] = ("directory",)
            else:
                entries[name] = ("special", stat.S_IFMT(mode))
        return entries

    def rejected(destination, label):
        before = snapshot()
        try:
            result = subprocess.run([str(converter), "maps", str(destination)],
                                    cwd=work, text=True, capture_output=True, timeout=5)
        except subprocess.TimeoutExpired:
            raise SystemExit("Earth converter blocked on " + label)
        if result.returncode == 0 or snapshot() != before:
            print(result.stdout, end="")
            print(result.stderr, end="")
            raise SystemExit("Earth converter modified an unsafe destination: " + label)

    rejected(maps / "e32.bin", "canonical input")
    rejected("maps/../maps/e32.bin", "relative input alias")
    output_alias = work / "alias.c"
    os.link(maps / "e32.bin", output_alias)
    rejected(output_alias, "hard-linked input alias")
    output_alias.unlink()
    output_alias.symlink_to("maps/e32.bin")
    rejected(output_alias, "symbolic input alias")
    output_alias.unlink()
    output_alias.symlink_to("missing-file")
    rejected(output_alias, "dangling symbolic output")
    output_alias.unlink()
    output_alias.mkdir()
    (output_alias / "keep.txt").write_text("preserve this directory\n")
    rejected(output_alias, "nonempty output directory")
    shutil.rmtree(output_alias)
    os.mkfifo(output_alias)
    rejected(output_alias, "FIFO output")
    output_alias.unlink()
    output_alias.write_text("preserve previous generated source\n")
    for extension in (".tmp", ".bak"):
        reserved = output_alias.with_suffix(extension)
        reserved.write_text("preserve reserved file\n")
        rejected(output_alias, "reserved " + extension + " file")
        reserved.unlink()
        reserved.symlink_to("maps/e32.bin")
        rejected(output_alias, "reserved " + extension + " symbolic input alias")
        reserved.unlink()
        reserved.symlink_to("missing-file")
        rejected(output_alias, "reserved " + extension + " dangling symbolic link")
        reserved.unlink()
        reserved.mkdir()
        (reserved / "keep.txt").write_text("preserve reserved directory\n")
        rejected(output_alias, "reserved " + extension + " directory")
        shutil.rmtree(reserved)
        os.mkfifo(reserved)
        rejected(output_alias, "reserved " + extension + " FIFO")
        reserved.unlink()
    source = maps / "e32.bin"
    original = source.read_bytes()
    source.unlink()
    os.mkfifo(source)
    rejected(output_alias, "FIFO input")
    source.unlink()
    source.write_bytes(original)
    print("PASS Earth converter destinations: input aliases, directories, symlinks, FIFOs and reserved paths fail promptly without changing any files")


def check_earth_data(cc, common, output):
    converter = output / "e_bin"
    generated = output / "e_data.c"
    run(cc + common + [str(ROOT / "src/e_bin.c"), "-o", str(converter)])
    run([str(converter), str(ROOT / "data"), str(generated)])
    expected = (ROOT / "src/e_saved.c").read_bytes()
    if generated.read_bytes() != expected:
        raise SystemExit("Checked-in e_saved.c does not match canonical binary maps")
    run([str(converter), str(ROOT / "data"), str(generated)])
    if generated.read_bytes() != expected:
        raise SystemExit("Earth data generation is not deterministic")
    check_earth_destinations(converter, output)
    mismatch = output / "chunk_mismatch"
    mismatch.mkdir()
    header = (ROOT / "src/earthres.h").read_text()
    changed_header = header.replace("#define TG_EARTH_CHUNK_SIZE 16384UL",
                                    "#define TG_EARTH_CHUNK_SIZE 8192UL")
    if header == changed_header:
        raise SystemExit("Update the chunk-format regression for the new map layout")
    (mismatch / "earthres.h").write_text(changed_header)
    shutil.copyfile(generated, mismatch / "e_data.c")
    result = subprocess.run(cc + common + ["-DFLAT", "-DDRV_OGL", "-c",
        str(mismatch / "e_data.c"), "-o", str(mismatch / "e_data.o")],
        cwd=ROOT, text=True, capture_output=True, timeout=60)
    if result.returncode == 0 or "Regenerate Earth data" not in result.stderr:
        raise SystemExit("Stale generated data accepted a changed chunk format")
    shutil.copyfile(ROOT / "src/e_bin.c", mismatch / "e_bin.c")
    shutil.copyfile(ROOT / "src/buildio.h", mismatch / "buildio.h")
    run(cc + common + [str(mismatch / "e_bin.c"), "-o", str(mismatch / "e_bin")])
    rejected = mismatch / "output.c"
    rejected.write_text("preserve previous generated source\n")
    result = subprocess.run([str(mismatch / "e_bin"), str(ROOT / "data"), str(rejected)],
                            cwd=ROOT, text=True, capture_output=True, timeout=60)
    if result.returncode == 0 or rejected.read_text() != "preserve previous generated source\n":
        raise SystemExit("Incompatible chunk table replaced generated source")
    print("PASS Earth chunk format: stale generated readers and incompatible converter tables reject changed chunk boundaries")
    damaged = output / "damaged_maps"
    shutil.copytree(ROOT / "data", damaged)
    for filename in ("e32.bin", "e512_7.bin"):
        path = damaged / filename
        original = path.read_bytes()
        for mutation in (None, original[:-1], original + b"\x00"):
            generated.write_bytes(b"keep existing output\n")
            if mutation is None:
                path.unlink()
            else:
                path.write_bytes(mutation)
            result = subprocess.run([str(converter), str(damaged), str(generated)],
                                    cwd=ROOT, text=True, capture_output=True, timeout=60)
            if result.returncode == 0 or generated.read_bytes() != b"keep existing output\n":
                print(result.stdout, end="")
                print(result.stderr, end="")
                raise SystemExit("Invalid binary input replaced generated source: " + filename)
            path.write_bytes(original)
    # Compile the freshly regenerated source rather than only the checked-in copy.
    run([str(converter), str(ROOT / "data"), str(generated)])
    empty = output / "without_maps"
    empty.mkdir()
    for name, options, chunks in (
            ("all", ["-DFLAT", "-DDRV_OGL"], 13),
            ("matrox", ["-DSEG86", "-DDRV_MTX", "-D_huge="], 2),
            ("tiga", ["-DSEG86", "-DDRV_TGA", "-D_huge="], 2)):
        binary = output / ("earth_" + name)
        obj = output / ("earth_" + name + ".o")
        run(cc + common + options + [str(ROOT / "tests/earth_data.c"),
            str(generated), "-o", str(binary)])
        run([str(binary)])
        result = subprocess.run([str(binary), "embedded"], cwd=empty,
                                text=True, capture_output=True, timeout=60)
        if result.returncode != 0 or "PASS embedded Earth data:" not in result.stdout:
            print(result.stdout, end="")
            print(result.stderr, end="")
            raise SystemExit("Embedded Earth reader requires runtime map files")
        print(result.stdout, end="")
        object_flags = [flag for flag in common if not flag.startswith("-fsanitize=")]
        run(cc + object_flags + options + ["-c", str(generated), "-o", str(obj)])
        symbols = subprocess.run(["nm", "-S", str(obj)], text=True,
                                 capture_output=True, check=True).stdout
        arrays = [line.split() for line in symbols.splitlines()
                  if line.split() and line.split()[-1].startswith("earth_data_")
                  and len(line.split()) == 4 and line.split()[2].lower() == "r"]
        if len(arrays) != chunks or any(int(row[1], 16) > 16384 for row in arrays):
            raise SystemExit("Embedded map objects exceed 16KiB or include unselected levels")
    print("PASS Earth converter: strict C89, deterministic regeneration, exact-size validation and preservation of existing output on invalid inputs")
    print("PASS segmented Earth objects: every array is at most 16KiB and Matrox/TIGA embed only EARTH_32/EARTH_64")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", default=os.environ.get("MATROX_SDK"),
                        help="SDK root containing XGX1/HEADERS (headers are not vendored)")
    parser.add_argument("--xgl-contract", action="store_true",
                        help="Record XGL calls using manual-derived declarations and real X11 under Xvfb; not native XGL validation")
    parser.add_argument("--gdi-contract", action="store_true",
                        help="Record Win16 GDI calls with host declarations; not Microsoft C 7 ABI or Windows rendering validation")
    parser.add_argument("--tiga-contract", action="store_true",
                        help="Record TIGA 2 calls and execute the TI transform kernel; not Microsoft C ABI, TSR transport or board rendering validation")
    parser.add_argument("--tiga-sdk", default=os.environ.get("TIGA_SDK"),
                        help="Also compile the TIGA driver against original TIGA.H, EXTEND.H and TYPEDEFS.H declarations")
    parser.add_argument("--dos-cpu", "--tiga-cpu", dest="tiga_cpu", action="store_true",
                        help="Run the optional real-mode CPU instruction probe checks (requires GNU as/objcopy and Python unicorn)")
    parser.add_argument("--qd-contract", action="store_true",
                        help="Record Classic Macintosh calls with host declarations; not MPW ABI or QuickDraw rendering validation")
    parser.add_argument("--ami-contract", action="store_true",
                        help="Record AmigaOS calls with host declarations; not VBCC ABI or custom-chip rendering validation")
    parser.add_argument("--amiga-ogl-contract", action="store_true",
                        help="Record PiStorm3D MiniGL rendering and Amiga window/context calls; not GPU or native ABI validation")
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
        check_earth_data(cc, common, output)
        run([sys.executable, str(ROOT / "tests/ami_pack.py")])
        flat = ["-DFLAT", "-DDRV_OGL"]
        tri_mode = ["-include", str(ROOT / "tests/tri_mode.h")]
        api_source = ["-fsyntax-only", str(ROOT / "tests/driver_api.c")]
        api_modes = (
            ("OpenGL", flat, ("drv_draw_quad_flat_color", "drv_draw_quadbatch_flat_color", "drv_draw_tristrip_flat_rgb", "drv_resize", "drv_texture_load")),
            ("IRIS GL", ["-DFLAT", "-DDRV_IGL"], ("drv_draw_tri_flat_rgb", "drv_draw_quad_flat_color", "drv_resize", "drv_texture_load")),
            ("Sun XGL", ["-DFLAT", "-DDRV_XGL"], ("drv_draw_tri_flat_color", "drv_draw_tristrip_flat_color", "drv_draw_quad_flat_color", "drv_resize", "drv_texture_load")),
            ("Matrox", ["-DSEG86", "-DDRV_MTX", "-D_huge="], ("drv_draw_tristrip_flat_color", "drv_draw_quadbatch_flat_color", "drv_draw_tri_flat_uv", "earth_data_128", "texture_generate")),
            ("TIGA", ["-DSEG86", "-DDRV_TGA", "-D_huge="], ("drv_draw_tri_flat_color", "drv_draw_tri_flat_uv", "drv_draw_tristrip_flat_color", "drv_draw_quad_flat_rgb", "earth_data_128", "texture_generate", "drv_resize")),
            ("GDI", ["-DSEG86", "-DDRV_GDI", "-D_huge="], ("drv_draw_tri_flat_color", "drv_draw_tri_flat_uv", "drv_draw_tristrip_flat_color", "drv_draw_quad_flat_rgb", "earth_data_32", "texture_generate", "drv_resize")),
            ("QuickDraw", ["-DSEG68", "-DDRV_QD"], ("drv_draw_tri_flat_color", "drv_draw_tri_flat_uv", "drv_draw_tristrip_flat_color", "drv_draw_quad_flat_rgb", "earth_data_32", "texture_generate", "drv_resize")),
            ("Amiga", ["-DFLAT", "-DDRV_AMI"], ("drv_draw_tri_flat_color", "drv_draw_tri_flat_uv", "drv_draw_tristrip_flat_color", "drv_draw_quad_flat_rgb", "earth_data_32", "texture_generate", "drv_resize")),
        )
        for driver, options, hidden in api_modes:
            check_driver_api(cc + common + options + api_source, driver,
                             hidden + ("drv_fps_history_num",))
        if args.tiga_cpu:
            run([sys.executable, str(ROOT / "tests/dos_cpu.py")])
            print("PASS DOS CPU probe: optional emulated instructions preserve FLAGS, registers and stack across 8086/286/386/486 profiles")
        if args.tiga_contract or args.tiga_sdk:
            tiga = ["-DSEG86", "-DDRV_TGA", "-D_huge=", "-Dfar=", "-D_cdecl=",
                    "-Wno-cast-function-type", "-I" + str(ROOT / "tests/dos_shim")]
            shim = ["-I" + str(ROOT / "tests/tiga_shim")]
            check_production_units(cc, common, tiga + shim, output, "TIGA",
                ("drv.c", "drv_tga.c", "err.c", "globe.c", "main.c", "e_saved.c", "soft3d.c", "soft3fp.c"))
            optimized = output / "tga_386.o"
            run(cc + common + tiga + shim + ["-DSOFT3D_TRANSFORM_386", "-c",
                str(ROOT / "src/soft3d.c"), "-o", str(optimized)])
            run(cc + common + tiga + shim + [str(ROOT / "tests/tiga.c"),
                str(ROOT / "src/e_saved.c"), str(ROOT / "src/globe.c"),
                str(ROOT / "src/drv.c"), str(ROOT / "src/err.c"),
                str(ROOT / "src/soft3d.c"), str(ROOT / "src/soft3fp.c"),
                str(ROOT / "src/tga_gsp.c"), str(optimized), "-lm", "-o", str(output / "tiga")])
            run([sys.executable, str(ROOT / "tests/tiga_pack.py")])
            check_tiga_extraction(output / "tiga", output)
            run(cc + common + tiga + shim + [str(ROOT / "tests/tiga_module.c"),
                str(ROOT / "src/tga_gsp.c"), str(ROOT / "src/soft3d.c"),
                "-lm", "-o", str(output / "tiga_module")])
            run([str(output / "tiga_module")])
            if args.tiga_sdk:
                sdk = Path(args.tiga_sdk)
                candidates = (sdk, sdk / "include", sdk / "INCLUDE", sdk / "TIGA" / "INCLUDE")
                headers = next((path for path in candidates if path.is_dir() and
                    {p.name.lower() for p in path.iterdir()} >= {"tiga.h", "extend.h", "typedefs.h"}), None)
                if headers is None:
                    parser.error("TIGA SDK needs TIGA.H, EXTEND.H and TYPEDEFS.H: " + str(sdk))
                normalized = output / "tiga_sdk"
                normalized.mkdir()
                for header in headers.iterdir():
                    if header.name.lower() in ("tiga.h", "extend.h", "typedefs.h"):
                        (normalized / header.name.lower()).write_bytes(header.read_bytes().rstrip(b"\r\n\x1a") + b"\n")
                check_production_units(cc, common, tiga + ["-I" + str(normalized)], output, "TIGA_SDK",
                    ("drv_tga.c",))
        run(cc + common + flat + tri_mode + api_source)
        run(cc + common + flat + ["-DDRV_PALETTE_DITHER", str(ROOT / "tests/driver_helpers.c"),
            str(ROOT / "src/drv.c"), "-lm", "-o", str(output / "flat_helpers")])
        run([str(output / "flat_helpers")])
        run(cc + common + flat + ["-DDEBUG=1", str(ROOT / "tests/driver_helpers.c"),
            str(ROOT / "src/drv.c"), "-lm", "-o", str(output / "debug_helpers")])
        run([str(output / "debug_helpers")])
        run(cc + common + flat + tri_mode + [str(ROOT / "tests/geometry.c"), str(ROOT / "src/e_saved.c"),
            str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
            str(ROOT / "src/err.c"), "-lm", "-o", str(output / "geometry")])
        run([str(output / "geometry")])
        for mode in ("nan", "infinity"):
            run([str(output / "geometry"), mode], expected_stderr="",
                required_output="PASS invalid globe radius: nonfinite input fails before replacing the existing mesh")
        run(cc + common + flat + [str(ROOT / "tests/ogl_strips.c"), str(ROOT / "src/e_saved.c"),
            str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
            str(ROOT / "src/err.c"), "-lm", "-o", str(output / "ogl_strips")])
        run([str(output / "ogl_strips")])
        strips = ["-DFLAT", "-DDRV_XGL"]
        run(cc + common + strips + [str(ROOT / "tests/strips.c"), str(ROOT / "src/e_saved.c"),
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
        run(cc + common + ["-DFLAT", "-DDRV_IGL", str(ROOT / "tests/igl_strips.c"), str(ROOT / "src/e_saved.c"),
            str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
            str(ROOT / "src/err.c"), "-lm", "-o", str(output / "igl_strips")])
        run([str(output / "igl_strips")])
        run(cc + common + flat + [str(ROOT / "tests/tex_take.c"),
            "-lm", "-o", str(output / "tex_take")])
        run([str(output / "tex_take")])
        run(cc + common + flat + [str(ROOT / "tests/recovery.c"),
            str(ROOT / "src/globe.c"), str(ROOT / "src/e_saved.c"),
            "-lm", "-o", str(output / "flat_recovery")])
        run([str(output / "flat_recovery")])
        diagnostics_pass = "PASS scene diagnostics: committed and recovered meshes only\n"
        run([str(output / "flat_recovery"), "diagnostics"], expected_stderr="",
            expected_stdout="Globe resolution: 32 x 16\nVertices: 561\nTriangles: 960\n" * 2 +
                            diagnostics_pass)
        run(cc + common + ["-DSEG86", "-DDRV_GDI", "-D_huge=",
            str(ROOT / "tests/recovery.c"), str(ROOT / "src/globe.c"),
            "-lm", "-o", str(output / "resource_recovery")])
        run([str(output / "resource_recovery")])
        run([str(output / "resource_recovery"), "diagnostics"], expected_stderr="",
            expected_stdout=diagnostics_pass)
        run(cc + common + ["-DSEG68", "-DDRV_QD",
            str(ROOT / "tests/recovery.c"), str(ROOT / "src/globe.c"),
            "-lm", "-o", str(output / "qd_recovery")])
        run([str(output / "qd_recovery")])
        run([str(output / "qd_recovery"), "diagnostics"], expected_stderr="",
            expected_stdout=diagnostics_pass)
        print("PASS committed mesh diagnostics: startup/recovery print once; no-op and failed attempts are silent; GDI and QuickDraw remain console-free")
        if args.qd_contract:
            check_production_units(cc, common, qd, output, "QuickDraw",
                ("drv.c", "drv_qd.c", "err.c", "globe.c", "main.c", "qd_fpu.c", "soft3d.c", "soft3fp.c"))
            run(cc + common + qd + [str(ROOT / "tests/qd.c"),
                str(ROOT / "src/qd_fpu.c"), str(ROOT / "src/soft3d.c"), str(ROOT / "src/soft3fp.c"),
                str(ROOT / "src/globe.c"),
                str(ROOT / "src/drv.c"), str(ROOT / "src/err.c"),
                "-lm", "-o", str(output / "qd")])
            for mode in ("mono", "1", "4", "8", "16", "32", "fpu", "falsefpu",
                         "oldcolor", "nogworld", "lockfail", "multi", "small640",
                         "small512", "large800", "tall640", "explicit",
                         "explicitwidth", "explicitheight", "patternfail", "polyfail"):
                run([str(output / "qd"), mode])
        if args.gdi_contract:
            check_production_units(cc, common, gdi + ["-Wno-cast-function-type"], output, "GDI",
                ("drv.c", "drv_gdi.c", "err.c", "globe.c", "main.c", "soft3d.c", "soft3fp.c"))
            # Win16 MakeProcInstance requires FARPROC/WNDPROC casts;
            # GCC diagnoses those historical, intentional callback casts.
            run(cc + common + gdi + ["-Wno-cast-function-type", str(ROOT / "tests/gdi.c"),
                str(ROOT / "src/soft3d.c"), str(ROOT / "src/soft3fp.c"), str(ROOT / "src/globe.c"),
                str(ROOT / "src/drv.c"),
                str(ROOT / "src/err.c"), "-lm", "-o", str(output / "gdi")])
            for mode in ("fpu", "fixed", "287", "small640", "small512", "tall640",
                         "fpu-escape", "fixed-escape", "maxframe", "fatal-poly", "fatal-oom"):
                run([str(output / "gdi"), mode])
            for mode in ("fatal-size",):
                run([str(output / "gdi"), mode], expected_returncode=1,
                    expected_stderr="",
                    required_output="PASS GDI fatal error: modal paint/timer/input dispatch cannot reenter rendering or partial scene state, and cleanup releases all resources")
            run(cc + common + gdi + ["-DDEBUG=1", "-Wno-cast-function-type",
                str(ROOT / "tests/gdi.c"), str(ROOT / "src/soft3d.c"), str(ROOT / "src/soft3fp.c"),
                str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
                str(ROOT / "src/err.c"), "-lm", "-o", str(output / "gdi_debug")])
            run([str(output / "gdi_debug"), "fpu"])
        if args.amiga_ogl_contract:
            amiga_ogl = ["-DFLAT", "-DDRV_OGL", "-DOGL_AMIGA",
                         "-I" + str(ROOT / "tests/mgl_shim"),
                         "-I" + str(ROOT / "tests/ami_shim")]
            check_production_units(cc, common, amiga_ogl, output, "Amiga_OpenGL",
                ("drv.c", "drv_ogl.c", "ogl_ami.c", "err.c", "globe.c", "main.c",
                 "texture.c", "texsoft.c"))
            run(cc + common + amiga_ogl + [str(ROOT / "tests/amiga_ogl.c"),
                str(ROOT / "src/drv.c"), str(ROOT / "src/globe.c"),
                str(ROOT / "src/texture.c"), str(ROOT / "src/texsoft.c"),
                str(ROOT / "src/e_saved.c"), str(ROOT / "src/err.c"),
                "-lm", "-o", str(output / "amiga_ogl")])
            run([str(output / "amiga_ogl")],
                expected_stderr="OpenGL texture: Insufficient memory for texture; drawing flat colors\n")
            run(cc + common + amiga_ogl + [str(ROOT / "tests/ogl_ami.c"),
                "-lm", "-o", str(output / "ogl_ami")])
            for mode in ("normal", "nomipmap", "rgb24-in32", "native", "windowfail", "lockfail", "contextfail",
                         "bitmapfail", "wrong16", "wrong24", "unknownbitmap", "wrongformat",
                         "rounded", "shortbitmap", "hugebitmap"):
                run([str(output / "ogl_ami"), mode])
            for mode in ("missing", "missingcgx", "abi", "futureabi", "shorttable", "nullfunction",
                         "stub", "wrongbackend", "stack"):
                run([str(output / "ogl_ami"), mode], expected_returncode=1,
                    required_output="PASS MiniGL startup rejection:")
        if args.ami_contract:
            ami = ["-DFLAT", "-DDRV_AMI", "-DAMI_RTG=1", "-I" + str(ROOT / "tests/ami_shim")]
            check_production_units(cc, common, ami, output, "Amiga",
                ("drv.c", "drv_ami.c", "ami_data.c", "err.c", "globe.c", "main.c",
                 "soft3d.c", "soft3fp.c"))
            enhanced = output / "ami_020.o"
            run(cc + common + ami + ["-DSOFT3D_TRANSFORM_020", "-c",
                str(ROOT / "src/soft3d.c"), "-o", str(enhanced)])
            run(cc + common + ami + [str(ROOT / "tests/amiga.c"),
                str(ROOT / "src/soft3d.c"), str(ROOT / "src/soft3fp.c"),
                str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
                str(ROOT / "src/err.c"), str(enhanced), "-lm", "-o", str(output / "amiga")])
            for mode in ("ocs", "mono", "two", "three", "four", "ecs", "aga", "aga64", "aga128", "020", "fpu", "040",
                         "falsefpu", "aspect", "backfail", "scratchfail", "ham", "oversize", "cachefail", "ocswide", "denisewide", "ecswide",
                         "cgx8", "cgx15", "cgx16", "cgx24", "cgx32", "p968", "p9615", "p9616", "p9624", "p9632",
                         "fs-old", "fs-ocs", "fs-aga", "fs-ntsc", "fs-pointer", "fs-loop", "fs-cgx8", "fs-cgx15", "fs-cgx16", "fs-cgx24", "fs-cgx32",
                         "fs-p968", "fs-p9615", "fs-p9616", "fs-p9624", "fs-p9632",
                         "rtgboth", "p96both", "rtgbackfail", "rtgbadfriend", "rtgunknown", "rtgbaddepth", "rtglockfail", "rtgnative"):
                run([str(output / "amiga"), mode])
            run([str(output / "amiga"), "stack"], expected_returncode=1,
                required_output="PASS Amiga stack guard:")
            native_ami = ["-DAMI_RTG=0" if option == "-DAMI_RTG=1" else option for option in ami]
            check_production_units(cc, common, native_ami, output, "Amiga_native",
                ("drv_ami.c",))
            run(cc + common + native_ami + [str(ROOT / "tests/amiga.c"),
                str(ROOT / "src/soft3d.c"), str(ROOT / "src/soft3fp.c"),
                str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
                str(ROOT / "src/err.c"), str(enhanced), "-lm", "-o", str(output / "amiga_native")])
            for mode in ("ocs", "mono", "two", "three", "four", "aga", "aga64", "aga128", "backfail", "ocswide", "nativeunknown",
                         "fs-old", "fs-ocs", "fs-aga", "fs-ntsc", "fs-pointer", "fs-loop"):
                run([str(output / "amiga_native"), mode])
            # A real packed Hunk file exercises the production DOS resource reader.
            source = output / "amiga-input.hunk"
            packed = output / "testglobe-amiga"
            source.write_bytes(struct.pack(">10I", 1011, 0, 1, 0, 0, 1,
                                           1001, 1, 0x4e754e71, 1010))
            run([sys.executable, str(ROOT / "src/ami_pack.py"), str(source),
                 str(ROOT / "data"), str(packed)])
            run(cc + common + ami + [str(ROOT / "tests/ami_data.c"),
                "-o", str(output / "ami_data")])
            for mode in ("cli", "workbench", "programdir", "clipath", "cliname"):
                run([str(output / "ami_data"), str(packed), mode])
            original = packed.read_bytes()
            for offset in (-36, -20, -192):
                damaged = bytearray(original)
                damaged[offset] ^= 0xff
                packed.write_bytes(damaged)
                run([str(output / "ami_data"), str(packed), "malformed"])
            packed.write_bytes(original)
        if args.xgl_contract:
            if not shutil.which("xvfb-run"):
                parser.error("--xgl-contract requires xvfb-run and Xvfb on PATH")
            xgl_cppflags = shlex.split(os.environ.get("XGL_TEST_CPPFLAGS", ""))
            xgl_ldflags = shlex.split(os.environ.get("XGL_TEST_LDFLAGS", ""))
            check_production_units(cc, common, strips + xgl_cppflags +
                ["-I" + str(ROOT / "tests/xgl_record")], output, "XGL",
                ("drv.c", "drv_xgl.c", "err.c", "globe.c", "main.c", "texture.c", "e_saved.c"))
            run(cc + common + strips + xgl_cppflags +
                ["-I" + str(ROOT / "tests/xgl_record"),
                 str(ROOT / "tests/xgl.c"), str(ROOT / "src/e_saved.c"), str(ROOT / "src/globe.c"),
                 str(ROOT / "src/drv.c"), str(ROOT / "src/texture.c"),
                 str(ROOT / "src/err.c")] + xgl_ldflags +
                ["-lm", "-lX11", "-o", str(output / "xgl")])
            for mode in ("double", "single", "flatstripfail"):
                run(["xvfb-run", "-a", "-s", "-screen 0 1024x768x24 -nolisten tcp",
                     str(output / "xgl"), mode])
            for mode in ("rasterfail", "mipmapfail", "stripallocfail"):
                run(["xvfb-run", "-a", "-s", "-screen 0 1024x768x24 -nolisten tcp",
                     str(output / "xgl"), mode],
                    required_output="PASS XGL bounded resource fallback:")
            xgl_texture_failures = {
                "stagefail": "Cannot allocate XGL texture raster",
                "accessfail": "Cannot access XGL texture raster",
                "buildfail": "Cannot build XGL mipmap",
                "mapfail": "Cannot create XGL texture map",
                "configurefail": "Cannot configure XGL texture map",
                "bindfail": "XGL texture is not active",
                "inactive": "Cannot use XGL texture",
                "drawfail": "Cannot use XGL texture",
                "flushfail": "Cannot use XGL texture",
                "unloaded": "XGL texture data is not loaded",
            }
            xgl_native_error = "XGL error: test-123: injected texture failure (test operator)\n"
            for mode, message in xgl_texture_failures.items():
                native_error = mode in ("buildfail", "mapfail", "configurefail",
                                        "bindfail", "drawfail", "flushfail")
                run(["xvfb-run", "-a", "-s", "-screen 0 1024x768x24 -nolisten tcp",
                     str(output / "xgl"), mode],
                    expected_stderr=(xgl_native_error if native_error else "") +
                                    "Sun XGL texture: " + message + "\n",
                    required_output="PASS XGL recoverable texture failure:")
            for mode in ("fataltexture", "fataloverlay", "fatalcleanup", "fatalosd", "fatalmodel"):
                run(["xvfb-run", "-a", "-s", "-screen 0 1024x768x24 -nolisten tcp",
                     str(output / "xgl"), mode], expected_returncode=1,
                    expected_stderr=xgl_native_error +
                                    "ERROR - Sun XGL operation failed; see the XGL diagnostic\n",
                    required_output=("PASS XGL recorded cleanup:" if mode == "fataloverlay"
                                     else "PASS XGL nonrecoverable error:"))
        if args.ogl_contract:
            ogl = ["-DFLAT", "-DDRV_OGL", "-I" + str(ROOT / "tests/ogl_record")]
            check_production_units(cc, common, ogl, output, "OpenGL",
                ("drv.c", "drv_ogl.c", "err.c", "globe.c", "main.c", "texture.c", "texsoft.c", "e_saved.c"))
            ogl_sources = [str(ROOT / "tests/ogl.c"), str(ROOT / "src/e_saved.c"),
                str(ROOT / "src/texsoft.c"), str(ROOT / "src/globe.c"),
                str(ROOT / "src/drv.c"), str(ROOT / "src/texture.c"),
                str(ROOT / "src/err.c"), "-lm"]
            run(cc + common + ogl + ogl_sources +
                ["-o", str(output / "ogl")])
            for mode in ("rgba", "rgba-single", "rgb24", "rgb16", "indexed", "unknownalpha", "nodepth", "limited", "listfail", "reduced", "softreduced", "uploadreduced", "framefail"):
                run([str(output / "ogl"), mode])
            for mode in ("newlistfail", "listcompilefail"):
                run([str(output / "ogl"), mode],
                    expected_stderr="OpenGL error: Out of memory!\n" * 6,
                    required_output="PASS OpenGL display-list compilation failure: rejected or incomplete lists are deleted before complete immediate rendering")
            for mode in ("validationfail", "callfail"):
                run([str(output / "ogl"), mode],
                    expected_stderr="OpenGL error: Invalid operation!\n" * 6 if mode == "callfail" else "",
                    required_output="PASS OpenGL display-list use failure: invalid or rejected lists are deleted, immediate geometry is complete and OSD reports fail")
            texture_failures = {
                "uploadfail": "Insufficient memory for texture; drawing flat colors",
                "stagefail": "Insufficient memory for texture; drawing flat colors",
                "softframefail": "Cannot prepare indexed OpenGL texture raster",
                "softpixelsfail": "Cannot present indexed OpenGL texture raster",
                "softtransfererror": "Cannot present indexed OpenGL texture raster",
                "maxzero": "OpenGL texture capacity is below 32 pixels",
                "maxtiny": "OpenGL texture capacity is below 32 pixels",
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
            modern_ogl = ogl + ["-DTEST_OGL_FREEGLUT"]
            run(cc + common + modern_ogl + ogl_sources + ["-o", str(output / "ogl_modern")])
            for mode in ("rgba", "rgb24", "rgb16", "limited"):
                run([str(output / "ogl_modern"), mode])
            for mode in ("newlistfail", "listcompilefail"):
                run([str(output / "ogl_modern"), mode],
                    expected_stderr="OpenGL error: Out of memory!\n" * 6,
                    required_output="PASS OpenGL display-list compilation failure: rejected or incomplete lists are deleted before complete immediate rendering")
            for mode in ("validationfail", "callfail"):
                run([str(output / "ogl_modern"), mode],
                    expected_stderr="OpenGL error: Invalid operation!\n" * 6 if mode == "callfail" else "",
                    required_output="PASS OpenGL display-list use failure: invalid or rejected lists are deleted, immediate geometry is complete and OSD reports fail")
            for mode in ("texallocerror", "uploadinvalid", "uploadmixed", "uploadmixedreverse"):
                message = ("Cannot allocate OpenGL texture" if mode == "texallocerror"
                           else "Cannot upload OpenGL texture")
                run([str(output / "ogl_modern"), mode],
                    expected_stderr=("OpenGL error: Out of memory!\n" if mode == "texallocerror" else "OpenGL error: Invalid operation!\n") +
                                    "OpenGL texture: " + message + "\n",
                    required_output="PASS OpenGL recoverable texture failure:")
            for label, options in (("legacy", ogl), ("freeglut", modern_ogl)):
                obj = output / ("ogl_" + label + ".o")
                run(cc + common + options + ["-c", str(ROOT / "src/drv_ogl.c"), "-o", str(obj)])
                symbols = subprocess.run(["nm", "-u", str(obj)], text=True, capture_output=True, check=True).stdout
                if any(name in symbols for name in ("glGenerateMipmap", "gluBuild2DMipmaps", "glutGetProcAddress")):
                    raise SystemExit("OpenGL 1.1 object unexpectedly links a later entry point")
            print("PASS OpenGL 1.1 object linkage: legacy and FreeGLUT builds use base-level uploads without mipmap or extension entry points")
            for label, define in (("missing_query", "TEST_OGL_NO_MODE_QUERY"),
                                  ("old_glut", "TEST_OGL_OLD_GLUT")):
                legacy_options = ogl + ["-D" + define]
                binary = output / ("ogl_" + label)
                run(cc + common + legacy_options + ogl_sources + ["-o", str(binary)])
                for mode in ("legacy-glut", "rgb16"):
                    run([str(binary), mode], expected_stderr="",
                        required_output="PASS legacy GLUT window: one RGB/double request at final size and position, no probes or display strings, actual pixel/depth/buffer attributes validated")
                obj = output / ("ogl_" + label + ".o")
                run(cc + common + legacy_options + ["-c", str(ROOT / "src/drv_ogl.c"),
                    "-o", str(obj)])
                symbols = subprocess.run(["nm", "-u", str(obj)], text=True,
                    capture_output=True, check=True).stdout
                if "glutInitDisplayString" in symbols:
                    raise SystemExit("Legacy GLUT object unexpectedly imports display-string probing")
            print("PASS legacy GLUT linkage: API 3 and API 4 without mode queries create a validated window and never import glutInitDisplayString")
            run(cc + common + ogl + ["-DTEXTURE_CACHE_RASTER=1"] +
                ogl_sources + ["-o", str(output / "ogl_cached")])
            run([str(output / "ogl_cached"), "indexed"])
            run(cc + common + ogl + tri_mode + ogl_sources +
                ["-o", str(output / "ogl_tri")])
            for mode in ("rgba", "indexed"):
                run([str(output / "ogl_tri"), mode])
        if args.igl_contract:
            igl = ["-DFLAT", "-DDRV_IGL", "-I" + str(ROOT / "tests/igl_record")]
            check_production_units(cc, common, igl, output, "IRIS_GL",
                ("drv.c", "drv_igl.c", "err.c", "globe.c", "main.c", "texture.c", "texsoft.c", "e_saved.c"))
            run(cc + common + igl + [str(ROOT / "tests/igl.c"), str(ROOT / "src/e_saved.c"),
                str(ROOT / "src/texsoft.c"), str(ROOT / "src/globe.c"),
                str(ROOT / "src/drv.c"), str(ROOT / "src/texture.c"),
                str(ROOT / "src/err.c"), "-lm", "-o", str(output / "igl")])
            for mode in ("double", "single", "indexed", "indexed-single", "noz", "software", "unknown", "affine", "winquit", "winshut",
                         "rgba", "rgba-single", "rgb-single", "unknown-alpha", "cachefail", "reduced", "softreduced", "framefail", "objectreject"):
                run([str(output / "igl"), mode])
            igl_texture_failures = {
                "stagefail": "Insufficient memory for texture; drawing flat colors",
                "imagefail": "Insufficient memory for texture; drawing flat colors",
                "softmipfail": "Insufficient memory for texture; drawing flat colors",
                "softframefail": "Cannot prepare IRIS GL software texture framebuffer",
                "softpixelsfail": "IRIS GL software texture pixels are unavailable",
                "capabilitylost": "IRIS GL native texturing is unavailable",
                "unloadedtexture": "IRIS GL texture data is not loaded",
            }
            for mode, message in igl_texture_failures.items():
                run([str(output / "igl"), mode],
                    expected_stderr="IRIS GL texture: " + message + "\n",
                    required_output="PASS IRIS GL recoverable texture failure:")
            run(cc + common + igl + ["-DIGL_TEST_OLD_HEADERS",
                str(ROOT / "tests/igl.c"), str(ROOT / "src/e_saved.c"), str(ROOT / "src/texsoft.c"),
                str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
                str(ROOT / "src/texture.c"), str(ROOT / "src/err.c"),
                "-lm", "-o", str(output / "igl_oldheaders")])
            run([str(output / "igl_oldheaders"), "oldheaders"])
            run(cc + common + igl + ["-DIGL_TEST_OLD_HEADERS", "-DIGL_TEST_SUBDIVIDE",
                str(ROOT / "tests/igl.c"), str(ROOT / "src/e_saved.c"), str(ROOT / "src/texsoft.c"),
                str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
                str(ROOT / "src/texture.c"), str(ROOT / "src/err.c"),
                "-lm", "-o", str(output / "igl_affine")])
            run([str(output / "igl_affine"), "affine"])
            run(cc + common + igl + ["-DTEXTURE_CACHE_RASTER=1",
                str(ROOT / "tests/igl.c"), str(ROOT / "src/e_saved.c"), str(ROOT / "src/texsoft.c"),
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
                str(ROOT / "tests/texture.c"), str(ROOT / "src/e_saved.c"), "-o", str(output / "texture")])
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
        check_production_units(cc, common, segmented, output, "Matrox",
            ("drv.c", "drv_mtx.c", "err.c", "globe.c", "main.c", "e_saved.c", "soft3d.c", "soft3fp.c"))
        mtx_optimized = output / "mtx_386.o"
        run(cc + common + segmented + ["-DSOFT3D_TRANSFORM_386", "-c",
            str(ROOT / "src/soft3d.c"), "-o", str(mtx_optimized)])
        run(cc + common + segmented + [str(ROOT / "tests/matrox.c"), str(ROOT / "src/e_saved.c"),
            str(ROOT / "src/globe.c"), str(ROOT / "src/drv.c"),
            str(ROOT / "src/soft3d.c"), str(ROOT / "src/soft3fp.c"), str(mtx_optimized),
            str(ROOT / "src/err.c"), "-lm", "-o", str(output / "matrox")])
        run([str(output / "matrox")])
        for profile in ("reporting-off", "8086", "286", "386", "486", "8087", "287", "387", "fpu",
                        "startup-v3", "startup-world"):
            run([str(output / "matrox"), profile])
        print("PASS host compatibility: strict C89 and supplied SDK prototypes")


if __name__ == "__main__":
    main()
