#!/usr/bin/env python3
"""Check the DOS TIGA EXE overlay packer without a DOS or TIGA SDK.

The expected footer and CRC are decoded independently with Python's struct and
zlib modules. These checks validate file packaging, not execution of an MZ image.
"""
from pathlib import Path
import os
import shlex
import stat
import struct
import subprocess
import tempfile
import zlib


ROOT = Path(__file__).resolve().parents[1]
MAGIC = b"TGRLM001"
TRAILER_SIZE = 24
MAX_MODULE = 1024 * 1024


def mz_image(size=137):
    """A DOS header with one relocation and a non-page-aligned load image."""
    assert size >= 64
    header = bytearray(64)
    struct.pack_into("<14H", header, 0, 0x5A4D, size % 512,
                     (size + 511) // 512, 1, 4, 16, 0xFFFF,
                     0, 0x100, 0, 0, 0, 0x1C, 0)
    struct.pack_into("<2H", header, 0x1C, 2, 0)
    return bytes(header) + bytes((i * 17 + 9) & 255 for i in range(size - 64))


def verify(packed, base, module):
    assert packed[:len(base)] == base, "The DOS executable was changed"
    assert packed[len(base):-TRAILER_SIZE] == module
    magic, offset, length, crc, reserved = struct.unpack("<8s4I", packed[-TRAILER_SIZE:])
    assert (magic, offset, length, crc, reserved) == (
        MAGIC, len(base), len(module), zlib.crc32(module), 0)
    assert offset + length + TRAILER_SIZE == len(packed)
    # DOS loads only the original e_cp/e_cblp image, not this appended payload.
    last, pages = struct.unpack_from("<2H", packed, 2)
    load_end = pages * 512 if last == 0 else (pages - 1) * 512 + last
    assert load_end <= len(base) and load_end < offset + length


def snapshot(directory):
    result = {}
    for path in directory.rglob("*"):
        name = str(path.relative_to(directory))
        mode = path.lstat().st_mode
        if stat.S_ISLNK(mode):
            result[name] = ("link", os.readlink(path))
        elif stat.S_ISREG(mode):
            result[name] = ("file", path.read_bytes())
        elif stat.S_ISDIR(mode):
            result[name] = ("directory",)
        else:
            result[name] = ("special", stat.S_IFMT(mode))
    return result


def main():
    with tempfile.TemporaryDirectory(prefix="testglobe-tiga-pack-") as temporary:
        work = Path(temporary)
        packer = work / "tga_pack"
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c89", "-pedantic-errors", "-Wall", "-Wextra",
            "-Werror=implicit-function-declaration", "-g",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined",
            "-fno-omit-frame-pointer", str(ROOT / "src/tga_pack.c"), "-o", str(packer)]
        subprocess.run(command, check=True, capture_output=True)
        files = work / "files"
        files.mkdir()
        source = files / "tga_host.exe"
        module_path = files / "tga_gsp.rlm"
        output = files / "testglob.exe"
        base = mz_image() + b"unrelated linker/debug information"
        module = bytes((i * 29 + 5) & 255 for i in range(1549))
        source.write_bytes(base)
        module_path.write_bytes(module)
        output.write_bytes(b"old executable")

        def call(input_path, destination, payload=module_path, success=True):
            command = [str(packer), str(input_path), str(destination)]
            if payload is not None:
                command.append(str(payload))
            result = subprocess.run(command, cwd=files, capture_output=True, timeout=15)
            assert result.returncode >= 0, (command, result.returncode, result.stderr)
            assert b"AddressSanitizer" not in result.stderr and b"runtime error:" not in result.stderr, (
                command, result.stderr)
            assert (result.returncode == 0) == success, (
                command, result.returncode, result.stdout, result.stderr)
            return result

        call(source, output)
        packed = output.read_bytes()
        verify(packed, base, module)
        assert source.read_bytes() == base and module_path.read_bytes() == module
        assert not output.with_suffix(".tmp").exists()
        assert not output.with_suffix(".bak").exists()
        # Repeated builds using the base executable must not accumulate overlays.
        call(source, output)
        assert output.read_bytes() == packed
        # A host-only build remains usable and contains no module footer.
        call(source, output, None)
        assert output.read_bytes() == base
        call(source, output)
        assert output.read_bytes() == packed

        def rejected(input_path=source, destination=output, payload=module_path):
            before = snapshot(files)
            call(input_path, destination, payload, False)
            assert snapshot(files) == before, "Rejected inputs altered existing files"

        rejected(source, source)
        rejected(source, module_path)
        # A previous packaged output is not accepted as a fresh linker image.
        rejected(output, files / "repacked.exe")
        rejected(output, files / "hostcopy.exe", None)
        alias = files / "alias.exe"
        os.link(source, alias)
        rejected(source, alias)
        alias.unlink()
        os.link(module_path, alias)
        rejected(source, alias)
        alias.unlink()
        alias.symlink_to(output)
        rejected(source, alias)
        alias.unlink()
        alias.symlink_to(files / "missing.exe")
        rejected(source, alias)
        alias.unlink()
        alias.mkdir()
        (alias / "keep").write_bytes(b"preserve directory")
        rejected(source, alias)
        (alias / "keep").unlink()
        alias.rmdir()
        # Reserved transaction names are never truncated or silently reused.
        for suffix in (".tmp", ".bak"):
            sibling = output.with_suffix(suffix)
            sibling.write_bytes(b"unrelated recovery file")
            rejected()
            sibling.unlink()

        for bad in (b"", b"MZ", b"not a DOS executable", mz_image()[:63],
                    mz_image()[:-1]):
            source.write_bytes(bad)
            rejected()
        for offset, value in ((2, 512), (4, 0), (8, 0), (8, 0xFFFF)):
            bad = bytearray(mz_image())
            struct.pack_into("<H", bad, offset, value)
            source.write_bytes(bad)
            rejected()
        source.write_bytes(base)

        module_path.unlink()
        rejected()
        os.mkfifo(module_path)
        rejected()
        module_path.unlink()
        for bad in (b"", bytes(MAX_MODULE + 1)):
            module_path.write_bytes(bad)
            rejected()
        # Transfer loops and footer arithmetic must cross 16-bit size boundaries.
        for size in (1, 512, 65535, 65536, MAX_MODULE):
            large = bytes((i * 11) & 255 for i in range(size))
            module_path.write_bytes(large)
            call(source, output)
            verify(output.read_bytes(), base, large)
        module_path.write_bytes(module)
        # Full 512-byte MZ pages encode zero in e_cblp.
        source.write_bytes(mz_image(512))
        call(source, output)
        verify(output.read_bytes(), source.read_bytes(), module)

    print("PASS TIGA executable packing: exact embedded module and CRC, unchanged MZ load image, bounded transfers, repeatable builds, host-only copy, malformed input rejection and protected destination replacement")


if __name__ == "__main__":
    main()
