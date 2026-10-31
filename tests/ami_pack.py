#!/usr/bin/env python3
"""Validate the Amiga single-executable Earth resource packer without an SDK."""
from pathlib import Path
import os
import shutil
import struct
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
PACKER = ROOT / "src" / "ami_pack.py"
HEADER, CODE, DATA, DEBUG, END = 1011, 1001, 1002, 1009, 1010


def words(*values):
    return struct.pack(">" + "I" * len(values), *values)


def call(source, maps, destination, success):
    result = subprocess.run([sys.executable, str(PACKER), str(source), str(maps),
                             str(destination)], capture_output=True, timeout=10)
    assert (result.returncode == 0) == success, (
        result.args, result.returncode, result.stdout, result.stderr)


def check_payload(packed, executable):
    assert packed[:len(executable) - 4] == executable[:-4]
    assert packed[-4:] == words(END)
    magic, version, count, directory, data, size, reserved = struct.unpack(
        ">8s6I", packed[-36:-4])
    assert magic == b"TGEARTH1" and version == 1 and count == 13 and reserved == 0
    assert size == 174592 and data + size == directory
    assert directory + count * 12 == len(packed) - 36
    assert packed[data - 8:data] == words(0) + b"TGEO"
    tag, length = struct.unpack_from(">2I", packed, data - 16)
    assert tag == DEBUG and (length * 4) == len(packed) - 4 - (data - 8)
    names = ["e32.bin", "e64.bin", "e128.bin"]
    names += [f"e256_{i}.bin" for i in range(2)]
    names += [f"e512_{i}.bin" for i in range(8)]
    # Resource IDs are part of the shared disk/native-resource contract.
    ids = [1000, 1010, 1020, 1030, 1031, 1040, 1041, 1042, 1043, 1044, 1045, 1046, 1047]
    position = data
    for index, (name, expected_id) in enumerate(zip(names, ids)):
        identifier, offset, length = struct.unpack_from(">3I", packed, directory + index * 12)
        canonical = (ROOT / "data" / name).read_bytes()
        assert identifier == expected_id and offset == position and length == len(canonical)
        assert packed[offset:offset + length] == canonical
        position += length
    assert position == directory


def main():
    with tempfile.TemporaryDirectory(prefix="testglobe-amiga-pack-") as temporary:
        work = Path(temporary)
        maps = work / "maps"
        shutil.copytree(ROOT / "data", maps)
        # Two loadable hunks and an unrelated debugger record must survive.
        executable = (words(HEADER, 0, 2, 0, 1, 1, 1, CODE, 1, 0x4e754e71,
                            DEBUG, 2, 0) + b"LINE" +
                      words(END, DATA, 1, 0x12345678, END))
        source = work / "input.hunk"
        output = work / "testglobe-amiga"
        source.write_bytes(executable)
        output.write_bytes(b"older executable must be replaced atomically")
        call(source, maps, output, True)
        packed = output.read_bytes()
        check_payload(packed, executable)
        assert source.read_bytes() == executable
        repacked = work / "repacked"
        call(output, maps, repacked, True)
        assert repacked.read_bytes() == packed, "Repacking duplicated the embedded Earth data"

        def rejected(raw):
            source.write_bytes(raw)
            call(source, maps, output, False)
            assert output.read_bytes() == packed and source.read_bytes() == raw

        for raw in (b"", words(HEADER), executable[:-1], executable[:-4],
                    executable[:-4] + words(0), executable + b"garbage",
                    words(HEADER, 0, 1, 0, 0, 1, CODE, 0x7fffffff, END)):
            rejected(raw)
        source.write_bytes(executable)
        for name in ("e32.bin", "e256_1.bin", "e512_7.bin"):
            path = maps / name
            original = path.read_bytes()
            for damaged in (original[:-1], original + b"x"):
                path.write_bytes(damaged)
                call(source, maps, output, False)
                assert output.read_bytes() == packed
            path.unlink()
            call(source, maps, output, False)
            assert output.read_bytes() == packed
            path.write_bytes(original)

        icon = work / "testglobe-amiga.info"
        result = subprocess.run([sys.executable, str(PACKER), "--icon", str(icon)],
                                capture_output=True, timeout=10)
        assert result.returncode == 0, result.stderr
        contents = icon.read_bytes()
        assert struct.unpack_from(">2H", contents) == (0xe310, 1)
        assert contents[48] == 3  # WBTOOL, not a project requiring DefaultTool.
        assert struct.unpack_from(">I", contents, 74)[0] == 32768
        left, top, width, height, depth = struct.unpack_from(">5h", contents, 78)
        assert left == top == 0 and 0 < width <= 64 and 0 < height <= 64 and depth == 2
        assert contents[92:94] == bytes((3, 0))  # PlanePick and PlaneOnOff.
        plane_size = ((width + 15) // 16) * 2 * height
        assert len(contents) == 78 + 20 + 2 * plane_size
        assert any(contents[98:98 + plane_size]) and any(contents[98 + plane_size:])
        protected_icon = work / "icon-link"
        protected_icon.symlink_to(icon)
        result = subprocess.run([sys.executable, str(PACKER), "--icon", str(protected_icon)],
                                capture_output=True, timeout=10)
        assert result.returncode != 0 and icon.read_bytes() == contents and protected_icon.is_symlink()

        call(source, maps, source, False)
        assert source.read_bytes() == executable
        alias = work / "alias"
        os.link(source, alias)
        call(source, maps, alias, False)
        assert source.read_bytes() == executable
        alias.unlink()
        alias.symlink_to(output)
        call(source, maps, alias, False)
        assert alias.is_symlink() and output.read_bytes() == packed
        alias.unlink()
        alias.mkdir()
        (alias / "keep").write_bytes(b"preserve directory")
        call(source, maps, alias, False)
        assert (alias / "keep").read_bytes() == b"preserve directory"
    print("PASS Amiga executable data: exact canonical maps and directory, preserved loadable/debug hunks, idempotent repack, classic Workbench Tool icon/stack, malformed input rejection and atomic destination protection")


if __name__ == "__main__":
    main()
