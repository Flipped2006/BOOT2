#!/usr/bin/env python3
"""Run in WSL: python3 boot1st/test_boot1st.py [--emulate].

Image/linker checks use the project's toolchain and Python's standard library.
--emulate additionally requires unicorn and executes the actual Cortex-M loader.
"""

import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
BUNDLED_TOOLCHAIN = ROOT / "tools/gcc-arm-none-eabi/15.3/bin"
GCC_PATH = os.environ.get("GCC_PATH")
if GCC_PATH is None and (BUNDLED_TOOLCHAIN / "arm-none-eabi-gcc").is_file():
    GCC_PATH = str(BUNDLED_TOOLCHAIN)
PREFIX = str(ROOT / GCC_PATH / "arm-none-eabi-") if GCC_PATH else "arm-none-eabi-"
FLASH = 0x08000000
RAM = 0x20000000
RAM_SIZE = 128 * 1024
VTOR = 0xE000ED08
CPACR = 0xE000ED88
MCU = ["-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard"]


def tool(name, *args, check=True):
    return subprocess.run([PREFIX + name, *map(str, args)], cwd=ROOT,
                          check=check, capture_output=True, text=True)


def symbols(elf):
    return {parts[2]: int(parts[0], 16)
            for line in tool("nm", "--defined-only", elf).stdout.splitlines()
            if len(parts := line.split()) == 3}


def fixture(directory, payload, should_link=True):
    binary = directory / "payload.bin"
    obj = directory / "loader.o"
    elf = directory / "loader.elf"
    image = directory / "loader.bin"
    binary.write_bytes(payload)
    tool("gcc", *MCU, "-x", "assembler-with-cpp", "-c",
         f'-DBOOT2_IMAGE_PATH="{binary}"', ROOT / "boot1st/boot1st.s", "-o", obj)
    result = tool("gcc", *MCU, "-nostdlib", "-nostartfiles",
                  "-T", ROOT / "boot1st/boot1st.ld", "-Wl,--gc-sections",
                  obj, "-o", elf, check=False)
    if not should_link:
        assert result.returncode != 0, "invalid payload unexpectedly linked"
        assert "BOOT2 image is missing" in result.stderr or "overlap the magic header" in result.stderr
        return None
    assert result.returncode == 0, result.stderr
    tool("objcopy", "-O", "binary", elf, image)
    return image.read_bytes(), symbols(elf)


def emulate(image, sym, payload, valid=True, startup=None):
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
    from unicorn.arm_const import UC_ARM_REG_MSP, UC_ARM_REG_PRIMASK, UC_ARM_REG_XPSR

    machine = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    machine.mem_map(FLASH, 64 * 1024)
    machine.mem_map(RAM, RAM_SIZE)
    machine.mem_map(0xE000E000, 4096)
    machine.mem_write(FLASH, image)
    machine.mem_write(RAM, b"\xa5" * RAM_SIZE)
    initial_msp, reset = struct.unpack_from("<II", image)
    next_msp, next_reset = struct.unpack_from("<II", payload)
    machine.reg_write(UC_ARM_REG_MSP, initial_msp)
    machine.reg_write(UC_ARM_REG_XPSR, 1 << 24)
    stops = []
    initialized = []
    destination = startup["main"] if startup else next_reset
    expected = destination & ~1 if valid else sym["Default_Handler"]

    def on_instruction(uc, address, _size, _data):
        if startup and address == startup["__libc_init_array"]:
            # Check the real second-stage startup before constructors can
            # legitimately change initialized data or zero-initialized globals.
            data_start = startup["_sdata"]
            data_size = startup["_edata"] - data_start
            assert bytes(uc.mem_read(data_start, data_size)) == \
                payload[data_start - RAM:data_start - RAM + data_size]
            bss_start = startup["_sbss"]
            bss_size = startup["_ebss"] - bss_start
            assert bytes(uc.mem_read(bss_start, bss_size)) == bytes(bss_size)
            initialized.append(True)
        if address == expected or address == sym["Default_Handler"]:
            stops.append(address)
            uc.emu_stop()

    def on_write(_uc, _access, address, size, _value, _data):
        limit = RAM + RAM_SIZE if startup else RAM + len(payload)
        system_registers = {VTOR, CPACR} if startup else {VTOR}
        assert (RAM <= address and address + size <= limit) or \
            (address in system_registers and size == 4), \
            f"unexpected write at 0x{address:08x}"

    machine.hook_add(UC_HOOK_CODE, on_instruction)
    machine.hook_add(UC_HOOK_MEM_WRITE, on_write)
    machine.emu_start(reset, 0, count=500000)
    assert stops == [expected], f"loader did not reach 0x{expected:08x}: {stops}"
    if startup:
        assert initialized == [True], "second-stage C initialization was skipped"
        assert struct.unpack("<I", machine.mem_read(CPACR, 4))[0] & (0xF << 20) == (0xF << 20), \
            "SystemInit did not enable the FPU"
    else:
        assert bytes(machine.mem_read(RAM, len(payload))) == payload
        assert bytes(machine.mem_read(RAM + len(payload), 16)) == b"\xa5" * 16
    assert machine.reg_read(UC_ARM_REG_MSP) == (next_msp if valid else initial_msp)
    assert machine.reg_read(UC_ARM_REG_PRIMASK) == (0 if valid else 1)
    assert struct.unpack("<I", machine.mem_read(VTOR, 4))[0] == (RAM if valid else 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulate", action="store_true")
    options = parser.parse_args()
    subprocess.run(["make", "-s", "-j4", "boot1st"], cwd=ROOT, check=True)
    payload = (ROOT / "build/boot.bin").read_bytes()
    image = (ROOT / "build/boot1st.bin").read_bytes()
    sym = symbols(ROOT / "build/boot1st.elf")
    second = symbols(ROOT / "build/boot.elf")
    offset = sym["__boot2_flash_start__"] - FLASH
    assert sym["g_pfnVectors"] == FLASH
    assert sym["__boot2_ram_start__"] == second["g_pfnVectors"] == RAM
    assert sym["__boot2_image_size__"] == len(payload)
    assert sym["__boot2_ram_end__"] == RAM + len(payload)
    assert sym["__boot2_flash_end__"] == FLASH + len(image) <= 0x0800C000
    assert offset % 4 == 0 and image[offset:] == payload
    assert struct.unpack_from("<II", image) == (RAM + RAM_SIZE, sym["Reset_Handler"] | 1)
    assert struct.unpack_from("<II", payload) == (second["_estack"], second["Reset_Handler"] | 1)
    assert RAM < second["_estack"] <= RAM + RAM_SIZE
    assert RAM <= second["Reset_Handler"] < RAM + len(payload)
    assert second["_sidata"] == second["_sdata"] <= second["_edata"] <= RAM + len(payload)
    vectors = struct.unpack_from("<98I", image)
    reserved = {7, 8, 9, 10, 13}
    for index in range(2, 98):
        assert vectors[index] == (0 if index in reserved else sym["Default_Handler"] | 1)
    print(f"PASS Flash layout: BOOT2 at 0x{FLASH + offset:08x}, {len(payload)} bytes; combined {len(image)} bytes")
    print("PASS complete Flash vectors, RAM entry/MSP, initialized data and embedded binary")

    with tempfile.TemporaryDirectory(prefix="boot1st-test-") as path:
        directory = Path(path)
        fixture(directory, b"\0" * 391, should_link=False)
        fixture(directory, b"\0" * (48 * 1024), should_link=False)
        print("PASS linker rejects a truncated vector table and magic-header overlap")
        if options.emulate:
            emulate(image, sym, payload)
            # Exercise each possible byte tail using the production assembly.
            for tail in (1, 2, 3):
                extended = payload + bytes(range(1, tail + 1))
                generated, fixture_sym = fixture(directory, extended)
                emulate(generated, fixture_sym, extended)
            print("PASS ARM execution: exact word/byte copy, guard bytes, VTOR, MSP, unmasked jump")

            emulate(image, sym, payload, startup=second)
            print("PASS ARM execution: BOOT2 SystemInit, .data, .bss and C runtime reach main with RAM vectors")

            for word, value in [(0, 0xffffffff), (0, RAM), (0, RAM - 8),
                                (0, RAM + RAM_SIZE + 8), (0, RAM + RAM_SIZE - 4),
                                (1, 0xffffffff), (1, second["Reset_Handler"] & ~1),
                                (1, FLASH | 1), (1, (RAM - 2) | 1),
                                (1, (RAM + len(payload)) | 1)]:
                bad_payload = bytearray(payload)
                struct.pack_into("<I", bad_payload, word * 4, value)
                bad_image = bytearray(image)
                bad_image[offset:] = bad_payload
                emulate(bytes(bad_image), sym, bytes(bad_payload), valid=False)
            print("PASS ARM execution: invalid MSP/Thumb/entry ranges halt before handoff")


if __name__ == "__main__":
    main()
