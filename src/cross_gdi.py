# Copyright (C) 2026 trguhq
# SPDX-License-Identifier: GPL-3.0-or-later
"""OpenWatcom cross-build of the existing Windows 3.1 GDI driver."""
import os
import struct

from cross import run, watcom_env


def verify_binary(executable, root):
    """Check the Win16 format and each embedded Earth resource after linking."""
    image = executable.read_bytes()
    if len(image) < 64 or image[:2] != b"MZ":
        raise ValueError("GDI output is not a DOS/Windows executable")
    ne = struct.unpack_from("<I", image, 60)[0]
    if ne + 64 > len(image) or image[ne:ne + 2] != b"NE":
        raise ValueError("GDI output is not a 16-bit NE executable")
    flags, = struct.unpack_from("<H", image, ne + 12)
    version, = struct.unpack_from("<H", image, ne + 62)
    if image[ne + 54] != 2 or version != 0x030a or not flags & 8:
        raise ValueError("GDI output must require protected-mode Windows 3.1")

    resources = {1000: "e32.bin", 1010: "e64.bin", 1020: "e128.bin",
                 1030: "e256_0.bin", 1031: "e256_1.bin"}
    resources.update({1040 + i: "e512_%d.bin" % i for i in range(8)})
    table = ne + struct.unpack_from("<H", image, ne + 36)[0]
    end = ne + struct.unpack_from("<H", image, ne + 38)[0]
    if table + 2 > end or end > len(image):
        raise ValueError("Invalid Win16 resource table")
    shift, = struct.unpack_from("<H", image, table)
    if shift > 16:
        raise ValueError("Invalid Win16 resource alignment")
    table += 2
    found = set()
    terminated = False
    while table + 2 <= end:
        kind, = struct.unpack_from("<H", image, table)
        table += 2
        if kind == 0:
            terminated = True
            break
        if table + 6 > end:
            raise ValueError("Truncated Win16 resource type")
        count, = struct.unpack_from("<H", image, table)
        table += 6
        if table + 12 * count > end:
            raise ValueError("Truncated Win16 resource entries")
        for _ in range(count):
            offset, length, resource_flags, number, _, _ = struct.unpack_from(
                "<6H", image, table)
            table += 12
            if kind != 0x800a:
                continue
            number &= 0x7fff
            if number not in resources or number in found:
                raise ValueError("Unexpected or duplicate Earth resource")
            first, size = offset << shift, length << shift
            expected = (root / "data" / resources[number]).read_bytes()
            if first + size > len(image) or size != len(expected):
                raise ValueError("Wrong Earth resource size: " + resources[number])
            if image[first:first + size] != expected:
                raise ValueError("Wrong Earth resource bytes: " + resources[number])
            if resource_flags & 0x1050 != 0x1010:
                raise ValueError("Earth resources must remain movable and load-on-call")
            found.add(number)
    if not terminated or found != set(resources):
        raise ValueError("Missing embedded Earth resources")
    print("Verified Windows 3.1 NE image and all 13 embedded Earth resources")


def build(config):
    src, out = config["src"], config["out"]
    env = watcom_env(config)
    env["INCLUDE"] += os.pathsep + str(config["watcom"] / "h" / "win")
    # Watcom supplies the Win16 large-model runtime, not a huge-model one.
    # Explicit TG_HUGE mesh/data pointers still normalize selector crossings;
    # ordinary driver packets and global arrays each remain below 64 KiB.
    options = ["-q", "-bt=windows", "-ml", "-2", "-fpi", "-fp2", "-zw",
               "-zp1", "-ot", "-w3", "-dSEG86", "-dDRV_GDI", "-dDRV",
               "-dNO_SINF", "-i=" + str(src)]
    names = ("drv", "drv_gdi", "err", "globe", "main", "soft3d", "soft3fp")
    for name in names:
        run(["wcc"] + options + ["-fo=" + name + ".obj", src / (name + ".c")],
            cwd=out, env=env)
    # Full -zw Windows prologues and the exported callback work with the
    # driver's MakeProcInstance thunk; no smart-callback ABI is assumed.
    link = ["system windows", "option quiet", "option map=testglob.map",
            "option stack=16384", "option heapsize=8192",
            "option alignment=16", "name testglob.exe",
            "segment type code preload moveable discardable",
            "segment type data preload moveable", "export GDI_WINDOW_PROC.1"]
    link.extend("file " + name + ".obj" for name in names)
    (out / "gdi.lnk").write_text("\n".join(link) + "\n", encoding="ascii")
    run(["wlink", "@gdi.lnk"], cwd=out, env=env)
    run(["wrc", "-q", "-r", "-bt=windows", "-fo=gdi.res",
         "-i=" + str(src), "-i=" + str(config["root"] / "data"), src / "gdi.rc"],
        cwd=out, env=env)
    run(["wrc", "-q", "-31", "-t", "gdi.res", "testglob.exe"], cwd=out, env=env)
    executable = out / "testglob.exe"
    verify_binary(executable, config["root"])
    return executable
