# Copyright (C) 2026 trguhq
# SPDX-License-Identifier: GPL-3.0-or-later
"""OpenWatcom huge-model DOS builds with the original graphics SDK libraries."""
import os
from pathlib import Path
import shutil

from cross import host_tool, run, watcom_env


def _find(root, candidates):
    """DOS SDK archives vary in directory spelling and filename case."""
    for candidate in candidates:
        current = root
        for part in Path(candidate).parts:
            if not current.is_dir():
                break
            matches = [entry for entry in current.iterdir()
                       if entry.name.lower() == part.lower()]
            if len(matches) != 1:
                break
            current = matches[0]
        else:
            return current
    raise ValueError("Missing SDK input under %s: %s" % (root, candidates[0]))


def _tiga_headers(config):
    source = _find(config["tiga_root"], ("include", "tigainc/TIGA/INCLUDE"))
    output = config["out"] / "sdk" / "include"
    output.mkdir(parents=True, exist_ok=True)
    for name in ("tiga.h", "extend.h", "typedefs.h"):
        header = _find(source, (name,))
        # DOS text files may end with Ctrl-Z; Linux-hosted compilers do not
        # treat it as EOF. Preserve the SDK and normalize only a build copy.
        data = header.read_bytes().split(b"\x1a", 1)[0].replace(b"\r\n", b"\n")
        (output / name).write_bytes(data.rstrip() + b"\n")
    return output


def _matrox_headers(config):
    source = _find(config["xgx_root"], ("XGX1/HEADERS",))
    root = config["out"] / "sdk"
    output = root / "XGX1" / "HEADERS"
    output.mkdir(parents=True, exist_ok=True)
    for name in ("DEF.H", "LIBFN.H", "IOLIB.H"):
        header = _find(source, (name,))
        data = header.read_bytes().split(b"\x1a", 1)[0].replace(b"\r\n", b"\n")
        (output / name).write_bytes(data.rstrip() + b"\n")
    return root


def _compile(config, driver, extra_sources=(), includes=()):
    env = watcom_env(config)
    out = config["out"]
    source = config["src"]
    compiler = shutil.which("wcc", path=env["PATH"])
    if compiler is None:
        raise ValueError("OpenWatcom wcc was not found under --watcom")
    converter = host_tool(config, "e_bin", [source / "e_bin.c"])
    data = out / "e_data.c"
    run([converter, config["root"] / "data", data])
    # C stack arguments match both original far-code/far-data SDK bindings.
    # -fpi keeps x87 emulation available; -0 is the baseline application CPU.
    flags = ["-q", "-bt=dos", "-mh", "-0", "-fpi", "-ecc", "-zp2", "-os", "-w3",
             "-dSEG86", "-dDRV_" + driver, "-dDRV", "-dNO_SINF",
             "-i=" + str(source)] + ["-i=" + str(path) for path in includes]
    names = ["drv", "drv_" + driver.lower(), "drv_cpu", "err", "globe",
             "main", "soft3d", "soft3fp"]
    sources = [source / (name + ".c") for name in names] + [data] + list(extra_sources)
    objects = []
    for path in sources:
        obj = out / (path.stem + ".obj")
        run([compiler] + flags + ["-fo=" + str(obj), path], cwd=out, env=env)
        objects.append(obj)
    optimized = out / "soft386.obj"
    run([compiler] + flags + ["-3", "-dSOFT3D_TRANSFORM_386",
        "-fo=" + str(optimized), source / "soft3d.c"], cwd=out, env=env)
    objects.append(optimized)
    return env, objects


def _link(config, env, objects, library, filename):
    linker = shutil.which("wlink", path=env["PATH"])
    if linker is None:
        raise ValueError("OpenWatcom wlink was not found under --watcom")
    out = config["out"]
    executable = out / filename
    response = out / "link.lnk"
    def quoted(path):
        text = str(path)
        if "'" in text or "\n" in text or "\r" in text:
            raise ValueError("Unsupported quote/newline in linker path: " + text)
        return "'" + text + "'"
    response.write_text("system dos\noption quiet\noption stack=16384\n" +
        "option map=" + quoted(out / "testglob.map") + "\nname " + quoted(executable) + "\n" +
        "".join("file " + quoted(obj) + "\n" for obj in objects) +
        "library " + quoted(library) + "\n")
    run([linker, "@" + str(response)], cwd=out, env=env)
    data = executable.read_bytes()
    if len(data) < 28 or data[:2] != b"MZ":
        raise ValueError("DOS linker did not produce an MZ executable")
    return executable


def _tiga_module(config):
    if config["tiga_module"] is not None:
        module = config["tiga_module"]
        if not module.is_file():
            raise ValueError("Missing supplied TI module: " + str(module))
        return module
    tools = config["tiga_tools"]
    if tools is None:
        raise ValueError("TIGA needs --tiga-tools with gspcl.exe/gsplnk.exe, "
                         "or --tiga-module with a previously built module")
    _find(tools, ("gspcl.exe",))
    _find(tools, ("gsplnk.exe",))
    directory = config["out"] / "gsp"
    directory.mkdir(parents=True, exist_ok=True)
    for name in ("tga_gsp.c", "tga_cmd.h", "tga_ext.asm"):
        shutil.copyfile(config["src"] / name, directory / name)
    # A stale successful module must never mask a failed TI tool invocation.
    for name in ("tga_gsp.rlm", "TGA_GSP.RLM", "build.ok", "BUILD.OK"):
        (directory / name).unlink(missing_ok=True)
    script = ("@echo off\r\nset PATH=D:\\;Z:\\\r\nC:\r\n"
              "gspcl -c -o -v10 tga_gsp.c tga_ext.asm > compile.log\r\n"
              "if errorlevel 1 goto failed\r\n"
              "gsplnk -o tga_gsp.rlm -r -cr tga_gsp.obj tga_ext.obj > link.log\r\n"
              "if errorlevel 1 goto failed\r\n"
              "echo ok > build.ok\r\n:failed\r\nexit\r\n")
    (directory / "build.bat").write_bytes(script.encode("ascii"))
    settings = directory / "dosbox.conf"
    settings.write_text("[sdl]\nfullscreen=false\n[dosbox]\nmemsize=16\n"
                        "[cpu]\ncycles=max\n[mixer]\nnosound=true\n")
    env = os.environ.copy()
    env.setdefault("SDL_VIDEODRIVER", "dummy")
    env.setdefault("SDL_AUDIODRIVER", "dummy")
    run([config["dosbox"], "-conf", settings, "-c", 'mount c "' + str(directory) + '"',
         "-c", 'mount d "' + str(tools) + '"', "-c", "c:", "-c", "build.bat"], env=env)
    try:
        _find(directory, ("build.ok",))
        module = _find(directory, ("tga_gsp.rlm",))
    except ValueError:
        for log in ("compile.log", "link.log"):
            try:
                print(_find(directory, (log,)).read_text(errors="replace"))
            except ValueError:
                pass
        raise ValueError("TI module compilation failed; see " + str(directory))
    return module


def build_tiga(config):
    headers = _tiga_headers(config)
    library = _find(config["tiga_root"], ("libs/ai.lib", "tigalibs/TIGA/LIBS/AI.LIB"))
    module = _tiga_module(config)
    env, objects = _compile(config, "TGA", includes=(headers,))
    host = _link(config, env, objects, library, "tga_host.exe")
    packer = host_tool(config, "tga_pack", [config["src"] / "tga_pack.c"])
    executable = config["out"] / "testglob.exe"
    run([packer, host, executable, module])
    return executable


def build_matrox(config):
    library = _find(config["xgx_root"], ("libsv5/lscl.lib",))
    headers = _matrox_headers(config)
    env, objects = _compile(config, "MTX", (config["src"] / "mtx_ow.c",),
                           (headers,))
    librarian = shutil.which("wlib", path=env["PATH"])
    if librarian is None:
        raise ValueError("OpenWatcom wlib was not found under --watcom")
    compatible = config["out"] / "sdk" / "lscl-ow.lib"
    # Keep the supplied SDK untouched. Its objects ask for Microsoft's
    # LLIBCE runtime by default; the adapter supplies the used interfaces
    # through Watcom instead. No instructions or symbols are removed.
    run([librarian, "-q", "-b", "-zll", "-o=" + str(compatible), library],
        cwd=config["out"], env=env)
    return _link(config, env, objects, compatible, "testglob.exe")
