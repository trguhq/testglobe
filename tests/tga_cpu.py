#!/usr/bin/env python3
"""Exercise the actual inline CPU-probe instructions in a 16-bit emulator.

Requires GNU as/objcopy and the optional Python unicorn package. The emulator
runs the assembled source, while a hook supplies 8086/286/386 FLAGS restrictions
that Unicorn's current x86 CPU does not model. This checks the instruction
branches, stack alignment/restoration and register/flag preservation; it does
not establish native Microsoft C code generation or BIOS/x87 correctness.
"""
import pathlib
import re
import subprocess
import tempfile

from unicorn import Uc, UC_ARCH_X86, UC_HOOK_CODE, UC_MODE_16
from unicorn.x86_const import (
    UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_DS, UC_X86_REG_EFLAGS,
    UC_X86_REG_ES, UC_X86_REG_SI, UC_X86_REG_SP, UC_X86_REG_SS,
)


ROOT = pathlib.Path(__file__).resolve().parents[1]


def probe_code():
    source = (ROOT / "src/tga_cpu.c").read_text()
    body = source.split("int tga_cpu_detect(void)", 1)[1].split("_asm {", 1)[1]
    body = body.split("\n    }", 1)[0]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    body = re.sub(r"\b([0-9][0-9a-f]*)h\b", r"0x\1", body, flags=re.I)
    body = body.replace("_emit", ".byte").replace("mov result, ax", "nop")
    with tempfile.TemporaryDirectory(prefix="testglobe-cpu-") as directory:
        directory = pathlib.Path(directory)
        asm = directory / "probe.s"
        obj = directory / "probe.o"
        binary = directory / "probe.bin"
        asm.write_text(".intel_syntax noprefix\n.code16\n.text\n" + body + "\n")
        subprocess.run(["as", "--32", str(asm), "-o", str(obj)], check=True)
        subprocess.run(["objcopy", "-O", "binary", "-j", ".text",
                        str(obj), str(binary)], check=True)
        return binary.read_bytes()


def flags_for(cpu, flags):
    if cpu == 86:
        flags |= 0xf000
    elif cpu == 286:
        flags &= ~0xf000
    if cpu < 486:
        flags &= ~0x40000
    return flags | 2


def check(cpu, stack, flags, code):
    machine = Uc(UC_ARCH_X86, UC_MODE_16)
    machine.mem_map(0, 1024 * 1024)
    machine.mem_write(0x1000, code)
    machine.reg_write(UC_X86_REG_SS, 0x8000)
    machine.reg_write(UC_X86_REG_SP, stack)
    machine.reg_write(UC_X86_REG_BX, 0x3141)
    machine.reg_write(UC_X86_REG_SI, 0x2718)
    machine.reg_write(UC_X86_REG_DS, 0x7000)
    machine.reg_write(UC_X86_REG_ES, 0x6000)
    flags = flags_for(cpu, flags)
    machine.reg_write(UC_X86_REG_EFLAGS, flags)
    previous_popf = [False]
    wide_instructions = [0]

    def before_instruction(uc, address, size, _):
        if previous_popf[0]:
            uc.reg_write(UC_X86_REG_EFLAGS,
                         flags_for(cpu, uc.reg_read(UC_X86_REG_EFLAGS)))
        instruction = bytes(uc.mem_read(address, size))
        previous_popf[0] = instruction in (b"\x9d", b"\x66\x9d")
        if instruction[0] == 0x66:
            assert cpu >= 386, "386 opcode reached on an older CPU"
            assert instruction in (b"\x66\x9c", b"\x66\x9d")
            assert uc.reg_read(UC_X86_REG_SP) % 4 == 0
            wide_instructions[0] += 1

    machine.hook_add(UC_HOOK_CODE, before_instruction)
    machine.emu_start(0x1000, 0x1000 + len(code), count=200)
    assert machine.reg_read(UC_X86_REG_AX) == cpu
    assert machine.reg_read(UC_X86_REG_SP) == stack
    assert machine.reg_read(UC_X86_REG_BX) == 0x3141
    assert machine.reg_read(UC_X86_REG_SI) == 0x2718
    assert machine.reg_read(UC_X86_REG_DS) == 0x7000
    assert machine.reg_read(UC_X86_REG_ES) == 0x6000
    assert machine.reg_read(UC_X86_REG_EFLAGS) == flags
    assert wide_instructions[0] == (4 if cpu >= 386 else 0)


def main():
    code = probe_code()
    cases = 0
    for cpu in (86, 286, 386, 486):
        for stack in (0x7ffc, 0x7ffe):
            for flags in (0x202, 0x203, 0x40202):
                check(cpu, stack, flags, code)
                cases += 1
    print("TIGA CPU probe: %d emulated FLAGS/stack cases passed" % cases)


if __name__ == "__main__":
    main()
