#!/usr/bin/env python3
# Copyright (C) 2026 trguhq
# SPDX-License-Identifier: GPL-3.0-or-later
"""Linux-hosted builds for the existing 16-bit DOS/Windows and classic Mac drivers.

The portable Makefiles delegate here so their original platform toolchains do
not depend on modern make syntax. SDKs and cross-compilers remain external.
"""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys


def run(argv, cwd=None, env=None):
    command = [str(part) for part in argv]
    print("+ " + shlex.join(command), flush=True)
    return subprocess.run(command, cwd=cwd, env=env, check=True)


def watcom_env(config):
    root = config["watcom"]
    if root is None:
        raise ValueError("Set WATCOM or pass --watcom with the OpenWatcom directory")
    env = os.environ.copy()
    env["WATCOM"] = str(root)
    env["PATH"] = os.pathsep.join((str(root / "binl64"), str(root / "binl"),
                                  env.get("PATH", "")))
    env["INCLUDE"] = str(root / "h")
    return env


def host_tool(config, name, sources, flags=()):
    directory = config["out"] / "host"
    directory.mkdir(parents=True, exist_ok=True)
    executable = directory / name
    run(config["host_cc"] + ["-O2"] + [str(source) for source in sources] +
        list(flags) + ["-o", str(executable)])
    return executable


def optional_path(value):
    return Path(value).expanduser().resolve() if value else None


def main(argv=None):
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", choices=("gdi", "quickdraw", "tiga", "matrox"))
    parser.add_argument("--build-dir", type=Path, default=root / "build" / "cross")
    parser.add_argument("--watcom", default=os.environ.get("WATCOM"))
    parser.add_argument("--retro68", default=os.environ.get("RETRO68"))
    parser.add_argument("--xgx-root", default=os.environ.get("XGX_ROOT", str(root)))
    parser.add_argument("--tiga-root", default=os.environ.get("TGA_ROOT", str(root / "TIGA")))
    parser.add_argument("--tiga-tools", default=os.environ.get("TIGA_TOOLS"))
    parser.add_argument("--tiga-module", default=os.environ.get("TIGA_MODULE"))
    parser.add_argument("--dosbox", default=os.environ.get("DOSBOX", "dosbox"))
    parser.add_argument("--host-cc", default=os.environ.get("HOST_CC", "cc"))
    parser.add_argument("--clean", action="store_true")
    args = parser.parse_args(argv)
    out = args.build_dir.expanduser().resolve() / args.target
    config = vars(args).copy()
    config.update(root=root, src=root / "src", out=out,
                  host_cc=shlex.split(args.host_cc))
    for name in ("watcom", "retro68", "xgx_root", "tiga_root", "tiga_tools", "tiga_module"):
        config[name] = optional_path(config[name])
    if not config["host_cc"]:
        parser.error("--host-cc must name a compiler")
    if args.clean:
        if out.exists():
            shutil.rmtree(out)
        return 0
    out.mkdir(parents=True, exist_ok=True)
    try:
        if args.target == "gdi":
            from cross_gdi import build
        elif args.target == "quickdraw":
            from cross_qd import build
        elif args.target == "matrox":
            from cross_dos import build_matrox as build
        else:
            from cross_dos import build_tiga as build
        outputs = build(config)
        if isinstance(outputs, (str, Path)):
            outputs = [outputs]
        for output in outputs:
            output = Path(output)
            if not output.is_file():
                raise ValueError("Build did not produce " + str(output))
            print("Built " + str(output))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print("cross build failed: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
