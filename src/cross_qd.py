# Copyright (C) 2026 trguhq
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the existing QuickDraw driver with the Retro68 68000 toolchain."""
import os
from pathlib import Path
import shutil

from cross import run


def build(config):
    toolchain = config.get("retro68")
    if toolchain is None:
        compiler = shutil.which("m68k-apple-macos-gcc")
        if compiler is None:
            raise ValueError("Set RETRO68 or pass --retro68 with the Retro68 toolchain directory")
        toolchain = Path(compiler).resolve().parent.parent
    compiler = toolchain / "bin" / "m68k-apple-macos-gcc"
    rez = toolchain / "bin" / "Rez"
    rincludes = toolchain / "m68k-apple-macos" / "RIncludes"
    for required in (compiler, rez, rincludes / "Retro68APPL.r"):
        if not required.is_file():
            raise ValueError("Retro68 installation is missing " + str(required))
    source, output = config["src"], config["out"]
    environment = os.environ.copy()
    environment["PATH"] = str(toolchain / "bin") + os.pathsep + environment.get("PATH", "")
    definitions = ["-DSEG68", "-DDRV_QD", "-DDRV", "-DNO_SINF", "-DQD_MULTIVERSAL"]
    common = ["-std=c89", "-Os", "-Wall", "-Wextra", "-ffunction-sections",
              "-fdata-sections", "-I" + str(source)] + definitions
    baseline = ["-m68000", "-msoft-float"]
    objects = []
    for name in ("drv", "drv_qd", "err", "globe", "main", "soft3d", "qd_fpu", "soft3fp"):
        target = output / (name + ".o")
        # Keep 68881 instructions behind the driver's existing CPU/FPU check.
        # Only pointers/integers cross this boundary; never use -mshort here.
        machine = ["-m68020", "-m68881"] if name in ("qd_fpu", "soft3fp") else baseline
        run([compiler] + common + machine + ["-c", source / (name + ".c"), "-o", target],
            env=environment)
        objects.append(target)
    code = output / "TestglobeQD.code.bin"
    run([compiler] + baseline + ["-Wl,-gc-sections", "-Wl,--mac-strip-macsbug"] +
        objects + ["-lm", "-o", code], env=environment)
    resources = output / "qd.r.rsrc.bin"
    run([rez, "-DQD_MULTIVERSAL=1", "-I" + str(rincludes), "-I" + str(source),
         source / "qd.r", "-o", resources], cwd=source, env=environment)
    binary = output / "TestglobeQD.bin"
    disk = output / "TestglobeQD.dsk"
    # The application resources follow the template so their SIZE overrides
    # its defaults. --copy would process them too early in Retro68 Rez.
    run([rez, rincludes / "Retro68APPL.r", "-I" + str(rincludes), "--copy", code,
         "-o", binary, "-t", "APPL", "-c", "Tglo", "--cc", disk,
         "--cc", output / "TestglobeQD.APPL", resources], cwd=output, env=environment)
    return [binary, disk]
