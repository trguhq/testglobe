#!/usr/bin/env python3
# Copyright (C) 2026 trguhq
# SPDX-License-Identifier: GPL-3.0-or-later
"""Inspect cross-built applications and their embedded data without running them.

This checks executable packaging, not instruction-set compatibility, native ABI,
SDK behavior, or graphics output. All readers operate on bounded file slices.
"""
import argparse
import binascii
from pathlib import Path
import struct
import sys
import zlib


ROOT = Path(__file__).resolve().parents[1]
EARTH = {
    1000: "e32.bin", 1010: "e64.bin", 1020: "e128.bin",
    1030: "e256_0.bin", 1031: "e256_1.bin",
    **{1040 + i: "e512_%d.bin" % i for i in range(8)},
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def region(data, offset, size, label):
    require(offset >= 0 and size >= 0 and offset <= len(data) and
            size <= len(data) - offset, "%s extends beyond file/table" % label)
    return data[offset:offset + size]


def fields(data, offset, format_, label):
    return struct.unpack(format_, region(data, offset, struct.calcsize(format_), label))


def u16(data, offset, endian="<"):
    return fields(data, offset, endian + "H", "16-bit field")[0]


def u32(data, offset, endian="<"):
    return fields(data, offset, endian + "I", "32-bit field")[0]


def check_mz(data):
    header = fields(data, 0, "<14H", "MZ header")
    require(header[0] == 0x5a4d, "missing DOS MZ signature")
    last, pages, relocations, paragraphs = header[1:5]
    require(pages > 0 and last < 512, "invalid MZ image length")
    image_end = (pages - 1) * 512 + (last or 512)
    header_size = paragraphs * 16
    require(28 <= header_size < image_end <= len(data), "truncated or invalid MZ image")
    table_offset = header[12]
    if relocations:
        require(table_offset >= 28, "MZ relocations overlap fixed header")
        table = region(data[:header_size], table_offset, 4 * relocations, "MZ relocations")
        for offset in range(0, len(table), 4):
            address, segment = fields(table, offset, "<2H", "MZ relocation")
            require(segment * 16 + address + 2 <= image_end - header_size,
                    "MZ relocation points outside loaded image")
    require(header[11] * 16 + header[10] < image_end - header_size,
            "MZ entry point is outside loaded image")
    require(header[6] >= header[5], "MZ maximum allocation is smaller than minimum")
    return image_end, header_size, relocations


def check_dos(path, tiga=False):
    data = path.read_bytes()
    image_end, header_size, relocations = check_mz(data)
    if header_size >= 64:
        newer = u32(data, 0x3c)
        if 0 < newer <= len(data) - 2:
            require(data[newer:newer + 2] not in (b"NE", b"PE", b"LE", b"LX"),
                    "DOS target contains a different executable format")
    image = data[header_size:image_end]
    for name in ("e32.bin", "e64.bin"):
        require((ROOT / "data" / name).read_bytes() in image,
                "DOS load image is missing canonical %s" % name)
    detail = "%d-byte load image, %d relocations, both Earth maps" % (image_end, relocations)
    if tiga:
        magic, offset, size, crc, reserved = fields(data, len(data) - 24, "<8s4I", "TIGA footer")
        require(magic == b"TGRLM001" and reserved == 0, "missing or invalid embedded TIGA module footer")
        require(image_end <= offset and 0 < size <= 1024 * 1024 and
                offset + size + 24 == len(data), "invalid embedded TIGA module bounds")
        payload = region(data, offset, size, "TIGA module")
        require(zlib.crc32(payload) == crc, "TIGA module CRC mismatch")
        detail += ", embedded %d-byte TI module with valid CRC" % size
    print("PASS %s: %s (%s)" % ("TIGA" if tiga else "Matrox", path, detail))


def pascal_name(data, offset, label):
    size = region(data, offset, 1, label)[0]
    return region(data, offset + 1, size, label)


def check_gdi(path):
    data = path.read_bytes()
    check_mz(data)
    ne_offset = u32(data, 0x3c)
    ne = region(data, ne_offset, len(data) - ne_offset, "NE image")
    region(ne, 0, 64, "NE header")
    require(ne[:2] == b"NE" and ne[0x36] == 2, "GDI is not a Win16 NE executable")
    flags = u16(ne, 0x0c)
    require(flags & 0x0008, "GDI must require protected mode")
    require(not flags & (0x8000 | 0x2000 | 0x0080),
            "GDI is a DLL, has link errors, or requires a math coprocessor")
    require(u16(ne, 0x3e) == 0x030a, "GDI must specify Windows 3.1")
    # In a Windows NE header, OS/2's CPU bits are repurposed for EMS behavior.
    # Do not claim CPU instruction validation from those flag positions.
    segments, modules = u16(ne, 0x1c), u16(ne, 0x1e)
    segment_table = u16(ne, 0x22)
    shift = u16(ne, 0x32)
    require(segments > 0 and shift <= 16, "invalid NE segment count/alignment")
    table = region(ne, segment_table, 8 * segments, "NE segment table")
    code_count = 0
    for i in range(segments):
        sector, length, segment_flags, minimum = fields(table, i * 8, "<4H", "NE segment")
        if not segment_flags & 1:
            code_count += 1
        if sector:
            start = sector << shift
            length = length or 65536
            region(data, start, length, "NE segment contents")
            if segment_flags & 0x0100:
                count = u16(data, start + length)
                region(data, start + length + 2, count * 8, "NE segment relocations")
        require((minimum or 65536) >= (length if sector else 0),
                "NE segment allocation smaller than its file contents")
    ip, cs = fields(ne, 0x14, "<2H", "NE entry point")
    require(1 <= cs <= segments, "NE entry segment is invalid")
    _, length, entry_flags, minimum = fields(table, (cs - 1) * 8, "<4H", "NE entry segment")
    require(not entry_flags & 1 and ip < (minimum or 65536), "NE entry is outside code")
    require(code_count > 0, "NE application has no code segments")
    imports = []
    module_table, names = u16(ne, 0x28), u16(ne, 0x2a)
    for i in range(modules):
        name = pascal_name(ne, names + u16(ne, module_table + i * 2), "NE imported module")
        imports.append(name.decode("ascii").upper())
    require({"KERNEL", "USER", "GDI"}.issubset(imports), "missing standard Windows imports")
    require(set(imports).issubset({"KERNEL", "USER", "GDI", "WIN87EM", "KEYBOARD"}),
            "GDI depends on an unexpected module: %s" % ", ".join(imports))
    start, end = u16(ne, 0x24), u16(ne, 0x26)
    resource_table = region(ne, start, end - start, "NE resource table")
    resource_shift = u16(resource_table, 0)
    require(resource_shift <= 16, "invalid NE resource alignment")
    cursor, resources = 2, {}
    while True:
        type_id = u16(resource_table, cursor)
        cursor += 2
        if type_id == 0:
            break
        count = u16(resource_table, cursor)
        region(resource_table, cursor, 6, "NE resource type")
        cursor += 6
        for _ in range(count):
            offset, length, attrs, resource_id, _, _ = fields(
                resource_table, cursor, "<6H", "NE resource descriptor")
            cursor += 12
            payload = region(data, offset << resource_shift, length << resource_shift, "NE resource contents")
            if type_id == 0x800a and resource_id & 0x8000:
                resource_id &= 0x7fff
                require(resource_id not in resources, "duplicate RCDATA resource")
                resources[resource_id] = payload
                if resource_id in EARTH:
                    require(attrs & 0x10 and attrs & 0x1000 and not attrs & 0x40,
                            "Earth resource must remain movable/discardable/load-on-call")
    require(set(resources) == set(EARTH), "GDI must embed all 13 Earth resource chunks")
    for resource_id, name in EARTH.items():
        expected = (ROOT / "data" / name).read_bytes()
        payload = resources[resource_id]
        require(payload[:len(expected)] == expected and
                len(payload) - len(expected) < 1 << resource_shift and
                not any(payload[len(expected):]), "GDI resource differs from canonical %s" % name)
    print("PASS GDI: %s (Win16 NE 3.1 protected mode, optional FPU, %d segments, all 13 Earth chunks; imports %s)" %
          (path, segments, ", ".join(imports)))


def mac_resources(fork):
    data_offset, map_offset, data_size, map_size = fields(fork, 0, ">4I", "resource-fork header")
    data = region(fork, data_offset, data_size, "resource data")
    map_ = region(fork, map_offset, map_size, "resource map")
    region(map_, 0, 28, "resource map header")
    type_offset, name_offset = fields(map_, 24, ">2H", "resource lists")
    types = region(map_, type_offset, name_offset - type_offset, "resource type/reference lists")
    names = region(map_, name_offset, len(map_) - name_offset, "resource names")
    type_count = u16(types, 0, ">") + 1
    region(types, 2, type_count * 8, "resource type table")
    result = {}
    for i in range(type_count):
        type_, count, offset = fields(types, 2 + i * 8, ">4sHH", "resource type")
        for j in range(count + 1):
            entry = region(types, offset + j * 12, 12, "resource reference")
            resource_id, name = fields(entry, 0, ">hH", "resource identity")
            if name != 0xffff:
                pascal_name(names, name, "resource name")
            position = int.from_bytes(entry[5:8], "big")
            size = u32(data, position, ">")
            key = type_, resource_id
            require(key not in result, "duplicate Mac resource identity")
            result[key] = region(data, position + 4, size, "Mac resource contents")
    return result


def check_quickdraw(path):
    data = path.read_bytes()
    header = region(data, 0, 128, "MacBinary header")
    require(header[0] == 0 and 1 <= header[1] <= 63 and header[74] == 0 and header[82] == 0,
            "invalid MacBinary header/name")
    require(header[65:69] == b"APPL" and header[69:73] == b"Tglo", "MacBinary application type/creator mismatch")
    version = header[122]
    if version:
        require(version in (129, 130), "unsupported MacBinary version")
        require(binascii.crc_hqx(header[:124], 0) == u16(header, 124, ">"), "MacBinary header CRC mismatch")
    secondary = u16(header, 120, ">")
    fork_offset = 128 + ((secondary + 127) // 128) * 128
    data_size, resource_size = fields(header, 83, ">2I", "MacBinary fork sizes")
    region(data, fork_offset, data_size, "MacBinary data fork")
    resource_offset = fork_offset + ((data_size + 127) // 128) * 128
    fork = region(data, resource_offset, resource_size, "MacBinary resource fork")
    require(resource_size > 0, "Mac application has no resource fork")
    resources = mac_resources(fork)
    require((b"CODE", 0) in resources and any(key[0] == b"CODE" and key[1] > 0 for key in resources),
            "Mac application requires CODE 0 and executable CODE resources")
    require((b"SIZE", -1) in resources, "Mac application is missing its SIZE resource")
    earth_keys = {key[1] for key in resources if key[0] == b"EART"}
    require(earth_keys == set(EARTH), "QuickDraw must embed all 13 Earth resource chunks")
    for resource_id, name in EARTH.items():
        require(resources[b"EART", resource_id] == (ROOT / "data" / name).read_bytes(),
                "QuickDraw resource differs from canonical %s" % name)
    codes = sum(key[0] == b"CODE" for key in resources)
    print("PASS QuickDraw: %s (APPL/Tglo MacBinary, %d CODE resources, SIZE and all 13 Earth chunks)" % (path, codes))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for driver in ("gdi", "matrox", "tiga", "quickdraw"):
        parser.add_argument("--" + driver, type=Path, metavar="APPLICATION")
    args = parser.parse_args()
    if not any(vars(args).values()):
        parser.error("select at least one application to inspect")
    try:
        if args.gdi:
            check_gdi(args.gdi)
        if args.matrox:
            check_dos(args.matrox)
        if args.tiga:
            check_dos(args.tiga, tiga=True)
        if args.quickdraw:
            check_quickdraw(args.quickdraw)
    except (OSError, ValueError, struct.error, UnicodeError) as error:
        print("FAIL cross-built application: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
