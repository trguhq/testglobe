#!/usr/bin/env python3
"""Embed demand-loaded Earth resources in an Amiga executable HUNK_DEBUG.

Usage: python3 src/ami_pack.py INPUT DATA_DIRECTORY OUTPUT
       python3 src/ami_pack.py --icon OUTPUT.info
The OS loader skips this debug record; drv_ami reads its own executable to
load only the selected map. No external runtime data files are required.
"""
import argparse
import os
from pathlib import Path
import re
import stat
import struct
import sys
import tempfile


HEADER, CODE, DATA, BSS = 1011, 1001, 1002, 1003
RELOC32, SYMBOL, DEBUG, END, SHORT = 1004, 1008, 1009, 1010, 1020
MAGIC = b"TGEARTH1"
TAG = b"\0\0\0\0TGEO"


def earth_inputs():
    """Use the existing chunk IDs; reject incompatible format edits."""
    header = Path(__file__).with_name("earthres.h").read_text(encoding="ascii")
    definitions = dict(re.findall(r"^#define\s+(TG_EARTH_\w+)\s+(\d+)(?:UL)?\s*$",
                                  header, re.M))
    if definitions.get("TG_EARTH_CHUNK_SIZE") != "16384":
        raise ValueError("Amiga resource packer requires 16384-byte Earth chunks")
    result = []
    for width in (32, 64, 128, 256, 512):
        size = width * (width // 2)
        count = (size + 16383) // 16384
        for chunk in range(count):
            symbol = f"TG_EARTH_{width}" + (f"_{chunk}" if chunk else "") + "_ID"
            if symbol not in definitions:
                raise ValueError(f"missing Earth resource ID {symbol}")
            name = f"e{width}" + (f"_{chunk}" if count > 1 else "") + ".bin"
            result.append((int(definitions[symbol]), name, min(size, 16384)))
            size -= min(size, 16384)
    if len(result) != 13 or len({entry[0] for entry in result}) != 13:
        raise ValueError("invalid Earth resource ID table")
    return result


def executable_records(data):
    """Validate the linked, non-overlay Hunk subset emitted by vlink.

    Return whole debug-record spans, allowing our own resource record to be
    replaced without accidentally scanning machine code for marker bytes.
    """
    if len(data) % 4 or len(data) > 0x7fffffff:
        raise ValueError("executable must have a bounded, longword-aligned size")
    position = 0

    def take(size):
        nonlocal position
        if size < 0 or size > len(data) - position:
            raise ValueError("truncated Amiga executable")
        start = position
        position += size
        return data[start:position]

    def word():
        return struct.unpack(">I", take(4))[0]

    def short():
        return struct.unpack(">H", take(2))[0]

    if word() != HEADER:
        raise ValueError("input is not a linked Amiga Hunk executable")
    # Resident library names are not appropriate for this standalone program.
    if word() != 0:
        raise ValueError("resident-library Hunk executables are unsupported")
    table, first, last = word(), word(), word()
    if first != 0 or last >= table or table != last + 1 or table > 65535:
        raise ValueError("invalid or overlay Hunk table")
    sizes = []
    for _ in range(table):
        size = word()
        if size & 0xc0000000 == 0xc0000000:
            word()  # Extended memory flags.
        sizes.append(size & 0x3fffffff)
    hunk = 0
    body = False
    debug = []
    while hunk < table:
        start = position
        raw = word()
        kind = raw & 0x3fffffff
        if kind in (CODE, DATA, BSS):
            if body:
                raise ValueError("multiple primary records in a Hunk")
            size = word()
            if size > sizes[hunk]:
                raise ValueError("Hunk data exceeds its allocation")
            if kind != BSS:
                take(4 * size)
            body = True
        elif kind == RELOC32:
            if not body:
                raise ValueError("relocations precede Hunk contents")
            while True:
                count = word()
                if not count:
                    break
                if word() >= table:
                    raise ValueError("relocation target is outside the Hunk table")
                for _ in range(count):
                    if word() + 4 > sizes[hunk] * 4:
                        raise ValueError("relocation offset exceeds its Hunk")
        elif kind == SHORT:
            if not body:
                raise ValueError("relocations precede Hunk contents")
            while True:
                count = short()
                if not count:
                    break
                if short() >= table:
                    raise ValueError("short relocation target is outside the Hunk table")
                for _ in range(count):
                    if short() + 4 > sizes[hunk] * 4:
                        raise ValueError("short relocation offset exceeds its Hunk")
            if position % 4:
                take(2)
        elif kind == SYMBOL:
            while True:
                size = word()
                if not size:
                    break
                take(size * 4)
                word()
        elif kind == DEBUG:
            size = word()
            payload = take(size * 4)
            debug.append((start, position, payload))
        elif kind == END:
            if not body:
                raise ValueError("Hunk ends without contents")
            hunk += 1
            body = False
        else:
            raise ValueError(f"unsupported linked Hunk record {kind}")
    if position != len(data) or data[-4:] != struct.pack(">I", END):
        raise ValueError("unexpected data after the final Hunk")
    return debug


def packed_executable(executable, directory):
    records = executable_records(executable)
    for start, end, payload in reversed(records):
        if payload.startswith(TAG):
            if end != len(executable) - 4 or payload[-32:-24] != MAGIC:
                raise ValueError("malformed existing Earth debug record")
            executable = executable[:start] + executable[end:]
    executable_records(executable)
    position = len(executable) - 4 + 8 + len(TAG)
    data_offset = position
    maps = bytearray()
    entries = bytearray()
    for resource, name, size in earth_inputs():
        path = directory / name
        if not path.is_file() or path.stat().st_size != size:
            raise ValueError(f"{name} must contain exactly {size} bytes")
        contents = path.read_bytes()
        if len(contents) != size:
            raise ValueError(f"{name} changed while being read")
        entries.extend(struct.pack(">III", resource, position, size))
        maps.extend(contents)
        position += size
    footer = MAGIC + struct.pack(">IIIIII", 1, 13, position, data_offset, len(maps), 0)
    payload = TAG + maps + entries + footer
    if len(payload) % 4 or len(executable) + len(payload) + 8 > 0x7fffffff:
        raise ValueError("packed executable is too large")
    record = struct.pack(">II", DEBUG, len(payload) // 4) + payload
    return executable[:-4] + record + executable[-4:]


def workbench_icon():
    """Original two-plane globe icon in the classic V1 DiskObject format.

    Pointer fields are presence flags on disk, not host pointer values. The
    78-byte DiskObject and 20-byte Image use the native 68K word alignment.
    Workbench uses the requested 32 KiB stack even on OS 1.3, whose VBCC
    startup cannot automatically enlarge a Shell's stack.
    """
    width, height, depth = 48, 24, 2
    gadget = struct.pack(">I4h3H5IHI", 0, 0, 0, width, height, 4, 0, 1,
                         1, 0, 0, 0, 0, 0, 0)
    disk = (struct.pack(">HH", 0xe310, 1) + gadget +
            struct.pack(">Bx7I", 3, 0, 0, 0x80000000, 0x80000000,
                        0, 0, 32768))
    image = struct.pack(">5hIBBI", 0, 0, width, height, depth, 1, 3, 0, 0)
    pixels = []
    for y in range(height):
        row = []
        for x in range(width):
            dx, dy = 2 * x - 47, 2 * y - 23
            ellipse = dx * dx * 100 + dy * dy * 441
            inside = ellipse <= 44100
            outline = inside and ellipse >= 35000
            meridian = abs(dx) <= 1 or abs(dx * dx * 4 + dy * dy - 400) <= 55
            parallel = abs(dy) <= 1 or abs(dy) in (9, 11)
            row.append(1 if outline or (inside and (meridian or parallel))
                       else 2 if inside else 0)
        pixels.append(row)
    planes = bytearray()
    for plane in range(depth):
        for row in pixels:
            for first in range(0, width, 16):
                word = sum(((row[first + bit] >> plane) & 1) << (15 - bit)
                           for bit in range(16))
                planes.extend(struct.pack(">H", word))
    assert len(disk) == 78 and len(image) == 20
    return disk + image + planes


def write_output(path, contents, executable=False):
    """Replace only a regular output, atomically and without following links."""
    if path.is_symlink() or (path.exists() and
                            not stat.S_ISREG(path.stat().st_mode)):
        raise ValueError("output must be a regular file, not a link or special file")
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix=path.name + ".", suffix=".tmp",
                                         dir=path.parent, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(contents)
        temporary.chmod(0o755 if executable else 0o644)
        os.replace(temporary, path)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--icon", type=Path, help="write the optional Workbench Tool icon")
    parser.add_argument("input", type=Path, nargs="?")
    parser.add_argument("data", type=Path, nargs="?")
    parser.add_argument("output", type=Path, nargs="?")
    args = parser.parse_args()
    try:
        if args.icon is not None:
            if any(value is not None for value in (args.input, args.data, args.output)):
                parser.error("--icon does not accept executable packing arguments")
            write_output(args.icon, workbench_icon())
            return 0
        if any(value is None for value in (args.input, args.data, args.output)):
            parser.error("packing requires INPUT DATA_DIRECTORY OUTPUT")
        if not args.input.is_file():
            raise ValueError("input executable must be a regular file")
        output = args.output.resolve()
        sources = [args.input.resolve()] + [(args.data / item[1]).resolve()
                                            for item in earth_inputs()]
        if output in sources or any(args.output.exists() and args.output.samefile(path)
                                    for path in sources if path.exists()):
            raise ValueError("output must not replace an input file")
        if args.output.is_symlink() or (args.output.exists() and
                                       not stat.S_ISREG(args.output.stat().st_mode)):
            raise ValueError("output must be a regular file, not a link or special file")
        if args.input.stat().st_size > 0x7fffffff:
            raise ValueError("input executable is too large")
        result = packed_executable(args.input.read_bytes(), args.data)
        write_output(args.output, result, executable=True)
    except (OSError, ValueError, struct.error) as error:
        print(f"Cannot pack Amiga Earth resources: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
