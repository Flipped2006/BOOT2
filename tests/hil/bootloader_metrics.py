#!/usr/bin/env python3
"""STM32F407 实板测试：两种截断重传、完整升级、CRC16/CRC32 拦截。"""
import argparse
import binascii
import csv
from contextlib import ExitStack
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import time
from zlib import crc32
import serial

FLASH, HEADER, APP = 0x08000000, 0x0800C000, 0x08010000
FLASH_SIZE, CHUNK, RETRY_S = 0x80000, 4096, 0.150
QUERY, REQUEST, ERASE, PROGRAM, VERIFY, BOOT = 1, 2, 0x81, 0x82, 0x83, 0x22
# 负面测试先执行，最后以 20 次完整升级结束。
PLAN = (("plain_retry", 10), ("firmware_retry", 10), ("crc16_reject", 10),
        ("crc32_reject", 10), ("upgrade", 20))


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def crc16(data):
    return binascii.crc_hqx(data, 0)


def frame(opcode, payload=b""):
    body = struct.pack("<BBH", 0xAA, opcode, len(payload)) + payload
    return body + struct.pack("<H", crc16(body))


def program_frame(address, data):
    return frame(PROGRAM, struct.pack("<II", address, len(data)) + data)


def load_xbin(path):
    raw = Path(path).read_bytes()
    check(len(raw) >= 4104, "truncated XBIN")
    _, offset, address, size, checksum = struct.unpack_from("<5I", raw, 32)
    location, header_crc = struct.unpack_from("<2I", raw, 248)
    check(struct.unpack_from("<I", raw)[0] == 0x4D414749, "wrong XBIN magic")
    check(offset == 4096 and address == APP and location == HEADER, "wrong XBIN layout")
    check(crc32(raw[:252]) == header_crc, "bad header CRC32")
    app, header = raw[offset:], raw[:offset]
    check(size == len(app) and 8 <= size <= 0x70000 and size % 4 == 0, "wrong APP size")
    check(crc32(app) == checksum, "bad APP CRC32")
    stack, entry = struct.unpack_from("<II", app)
    check(stack % 8 == 0 and (0x20000000 < stack <= 0x20020000 or
          0x10000000 < stack <= 0x10010000), "invalid APP stack")
    check(entry & 1 and APP <= (entry & ~1) < APP + size, "invalid APP entry")
    return app, header, hashlib.sha256(raw).hexdigest()


class DeviceError(Exception):
    def __init__(self, code):
        self.code = code
        super().__init__(f"device error {code}")


class BootChanged(AssertionError):
    pass


def open_uart(port, baud):
    uart = serial.Serial(port=None, baudrate=baud, timeout=0.05, write_timeout=2)
    uart.dtr = uart.rts = False
    uart.port = port
    uart.open()
    return uart


class Board:
    def __init__(self, config, output, uart, log, events, app_log, app, header):
        self.config, self.output, self.uart, self.log = config, output, uart, log
        self.events, self.app_log, self.app, self.header = events, app_log, app, header
        self.rx, self.boot, self.version, self.case = bytearray(), None, None, "baseline"

    def note(self, event, **data):
        self.events.write(json.dumps(dict(time=time.monotonic(), case=self.case,
                                         event=event, **data)) + "\n")
        self.events.flush()

    def send(self, packet):
        self.note("tx", hex=packet.hex())
        check(self.uart.write(packet) == len(packet), "short serial write")

    def wait(self, opcode, timeout=2):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            while len(self.rx) >= 7:
                if self.rx[0] != 0x55:
                    del self.rx[0]
                    continue
                _, actual, error, size = struct.unpack_from("<BBBH", self.rx)
                if size > 4104:
                    del self.rx[0]
                    continue
                length = size + 7
                if len(self.rx) < length:
                    break
                packet = bytes(self.rx[:length])
                if crc16(packet[:-2]) != struct.unpack("<H", packet[-2:])[0]:
                    del self.rx[0]
                    continue
                del self.rx[:length]
                if actual == opcode:
                    self.note("response", opcode=actual, error=error, hex=packet.hex())
                    if error:
                        raise DeviceError(error)
                    return packet[5:-2]
            data = self.uart.read(min(self.uart.in_waiting or 1, 8192))
            if data:
                self.note("rx", hex=data.hex())
                self.rx.extend(data)
        raise TimeoutError(f"no response to 0x{opcode:02x} within {timeout}s")

    def command(self, opcode, payload=b"", timeout=2):
        self.send(frame(opcode, payload))
        return self.wait(opcode, timeout)

    def jlink(self, commands):
        with tempfile.TemporaryDirectory(prefix="boot2-") as directory:
            script = Path(directory) / "commands.jlink"
            script.write_text("\n".join(["connect", *commands, "Exit"]) + "\n", encoding="ascii")
            result = subprocess.run([self.config["jlink"], "-USB", self.config["probe"],
                "-Device", "STM32F407VE", "-If", "SWD", "-Speed", "4000", "-NoGui", "1",
                "-AutoConnect", "0", "-ExitOnError", "1", "-CommandFile", str(script)],
                capture_output=True, text=True, errors="replace", timeout=20,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        self.note("jlink", commands=commands, stdout=result.stdout, stderr=result.stderr)
        if (result.returncode or re.search(r"(?im)^(?:\*+\s*)?(?:error:|could not |cannot |failed )", result.stdout)
                or result.stdout.count("CPU is not halted.") != commands.count("IsHalted")):
            raise RuntimeError("J-Link unavailable/read failed; see events.jsonl")

    def reset(self):
        self.uart.reset_input_buffer()
        self.rx.clear()
        self.jlink(["r", "g", "IsHalted"])
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            try:
                version = self.command(QUERY, b"\x00", 0.25)
                check(self.version is None or version == self.version, "VERSION changed")
                return version
            except TimeoutError:
                pass
        raise TimeoutError("Bootloader did not answer after reset")

    def snapshot(self, name):
        # 分块读取避免调试器整片 SaveBin 超时；不暂停 CPU，不写 Flash。
        with tempfile.TemporaryDirectory(prefix="boot2-read-") as directory:
            parts = [Path(directory) / f"{i:06x}.bin" for i in range(0, FLASH_SIZE, CHUNK)]
            commands = ["IsHalted"] + [f'SaveBin "{p}" 0x{FLASH + i * CHUNK:08x} 0x1000'
                       for i, p in enumerate(parts)] + ["IsHalted"]
            self.jlink(commands)
            if any(not p.is_file() or p.stat().st_size != CHUNK for p in parts):
                raise RuntimeError("incomplete J-Link readback")
            data = b"".join(p.read_bytes() for p in parts)
        with (self.output / f"{self.case}-{name}.bin").open("xb") as output:
            output.write(data)
        if self.boot is not None and data[:0xC000] != self.boot:
            raise BootChanged("Boot region changed; stopped")
        return data

    def check_image(self, flash, app):
        check(flash[0xC000:0xD000] == self.header, "header readback mismatch")
        check(flash[0x10000:0x10000 + len(app)] == app, "APP readback mismatch")

    def initialize(self):
        self.version = self.reset()
        check(self.command(QUERY, b"\x01") == struct.pack("<H", 4104), "unsupported MTU")
        flash = self.snapshot("flash")
        self.boot = flash[:0xC000]
        self.check_image(flash, self.app)

    def verify(self, address, data):
        self.command(VERIFY, struct.pack("<III", address, len(data), crc32(data)), 10)

    def transfer(self, address, data, verify=True):
        self.command(REQUEST, struct.pack("<BI", int(address == HEADER), len(data)))
        self.command(ERASE, struct.pack("<II", address, len(data)), 10)
        for offset in range(0, len(data), CHUNK):
            self.send(program_frame(address + offset, data[offset:offset + CHUNK]))
            self.wait(PROGRAM)  # 正常升级不重试 PROGRAM，避免无序号协议重复累计。
        if verify:
            self.verify(address, data)

    def block_packet(self):
        data = self.app[:CHUNK]
        self.command(REQUEST, struct.pack("<BI", 0, len(data)))
        self.command(ERASE, struct.pack("<II", APP, len(data)), 10)
        return data, program_frame(APP, data)

    def retry(self, packet, cut, opcode):
        start = time.monotonic()
        self.send(packet[:cut])
        try:
            self.wait(opcode, RETRY_S)
        except TimeoutError:
            pass
        else:
            raise AssertionError("truncated frame was acknowledged")
        resend = time.monotonic()
        self.send(packet)  # 两次发送之间没有复位、查询或调试器操作。
        reply = self.wait(opcode)
        end = time.monotonic()
        return reply, dict(cut_bytes=cut, retry_rtt_ms=1000 * (end - resend),
                           total_recovery_ms=1000 * (end - start))

    def observe_app(self, running):
        self.log.reset_input_buffer()  # 旧 APP 日志不能计入本次启动。
        try:
            self.command(BOOT)
        except DeviceError:
            if running:
                raise
        text = bytearray()
        deadline = time.monotonic() + (40 if running else 3.2)
        marker = b"[SYSTEM]: Build Date:"
        while time.monotonic() < deadline:
            data = self.log.read(self.log.in_waiting or 1)
            self.app_log.write(data)
            self.app_log.flush()
            text.extend(data)
            start = text.find(marker)
            if not running:
                check(start < 0, "corrupt APP was started")
            elif start >= 0 and text[start + len(marker):].count(b"[WiFi INFO]") >= 3:
                return
        check(not running, "new APP startup and 3 subsequent heartbeats not observed")
        check(self.command(QUERY, b"\x00") == self.version, "corrupt image did not remain in Boot")

    def test_plain_retry(self, index):
        """1. 普通包截断，超时后只重发一次原包，核对 VERSION。"""
        self.reset()
        packet = frame(QUERY, b"\x00")
        reply, metrics = self.retry(packet, 1 + (index - 1) % 6, QUERY)
        check(reply == self.version, "wrong response after retry")
        return metrics

    def test_firmware_retry(self, index):
        """2. PROGRAM 包截断，原包重传应答后读回并校验数据。"""
        self.reset()
        data, packet = self.block_packet()
        cuts = (4, 5, 12, 132, len(packet) // 2, len(packet) - 2, len(packet) - 1)
        _, metrics = self.retry(packet, cuts[(index - 1) % len(cuts)], PROGRAM)
        check(self.snapshot("data")[0x10000:0x11000] == data, "retried data mismatch")
        self.verify(APP, data)
        return metrics

    def test_crc16_reject(self, index):
        """3. 坏 CRC16 帧不能写 Flash；随后有效对照包必须能写入。"""
        self.reset()
        data, packet = self.block_packet()
        before = self.snapshot("before")
        bad = bytearray(packet)
        bad[12 + (index * 373) % CHUNK] ^= 1 << ((index - 1) % 8)
        self.send(bad)
        try:
            self.wait(PROGRAM, RETRY_S)
        except (TimeoutError, DeviceError):
            pass
        else:
            raise AssertionError("CRC16-corrupt frame accepted")
        check(self.command(QUERY, b"\x00") == self.version, "receiver unavailable")
        check(self.snapshot("after") == before, "bad frame changed Flash")
        self.send(packet)
        self.wait(PROGRAM)
        check(self.snapshot("control")[0x10000:0x11000] == data, "valid control frame not written")
        return {}

    def test_crc32_reject(self, index):
        """4. 帧 CRC16 合法的坏镜像必须 VERIFY/5，且保持 Boot。"""
        self.reset()
        check(self.snapshot("metadata")[0xC000:0xD000] == self.header, "invalid baseline header")
        bad = bytearray(self.app)
        bad[8 + (index * 7919) % (len(bad) - 8)] ^= 1 << ((index - 1) % 8)
        self.transfer(APP, bad, verify=False)
        try:
            self.verify(APP, self.app)
        except DeviceError as error:
            check(error.code == 5, "expected VERIFY error 5")
        else:
            raise AssertionError("CRC32-corrupt image accepted")
        self.check_image(self.snapshot("damaged"), bad)
        self.observe_app(running=False)
        return {}

    def test_upgrade(self, index):
        """5. 完整升级：两区校验、独立读回、新 APP 启动及 3 次周期日志。"""
        self.reset()
        start = time.monotonic()
        self.transfer(APP, self.app)
        self.transfer(HEADER, self.header)
        elapsed = time.monotonic() - start
        self.check_image(self.snapshot("flash"), self.app)
        self.observe_app(running=True)
        return dict(protocol_s=elapsed, kib_per_s=(len(self.app) + len(self.header)) / 1024 / elapsed,
                    end_to_end_s=time.monotonic() - start)


def save_results(output, rows, info):
    summary = {}
    for name, count in PLAN:
        selected = [r for r in rows if r["test"] == name]
        counts = {state: sum(r["status"] == state for r in selected) for state in ("PASS", "FAIL", "ERROR")}
        summary[name] = dict(planned=count, **counts, not_run=count - len(selected),
            pass_rate=counts["PASS"] / count if len(selected) == count and not counts["ERROR"] else None)
        for key in ("retry_rtt_ms", "total_recovery_ms", "protocol_s", "kib_per_s", "end_to_end_s"):
            values = sorted(r["metrics"][key] for r in selected if r["status"] == "PASS" and key in r["metrics"])
            if values:
                summary[name][key] = dict(mean=sum(values) / len(values), p95=values[math.ceil(.95 * len(values)) - 1])
    (output / "summary.json").write_text(json.dumps(dict(info, summary=summary, trials=rows),
                                       ensure_ascii=False, indent=2), encoding="utf-8")
    with (output / "trials.csv").open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=("test", "round", "status", "elapsed_s", "reason", "metrics"))
        writer.writeheader()
        for row in rows:
            writer.writerow(dict(row, metrics=json.dumps(row["metrics"])))
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=Path(__file__).with_name("hil.local.json"))
    args = parser.parse_args()
    config = json.loads(args.config.read_text(encoding="utf-8-sig"))
    check(config["port"] != config["log_port"], "upgrade/log ports must differ")
    app, header, digest = load_xbin(config["firmware"])
    output = Path(__file__).parent / "results" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    output.mkdir(parents=True)
    (output / "runner.py").write_bytes(Path(__file__).read_bytes())
    rows, info = [], dict(config=config, firmware_sha256=digest, baudrate=2000000, chunk_bytes=CHUNK, retry_ms=150)
    try:
        with ExitStack() as stack:
            uart = stack.enter_context(open_uart(config["port"], 2000000))
            log = stack.enter_context(open_uart(config["log_port"], 115200))
            events = stack.enter_context((output / "events.jsonl").open("w", encoding="utf-8"))
            app_log = stack.enter_context((output / "uart.log").open("wb"))
            board = Board(config, output, uart, log, events, app_log, app, header)
            board.initialize()
            info["boot_sha256"] = hashlib.sha256(board.boot).hexdigest()
            stop = False
            for name, count in PLAN:
                for index in range(1, count + 1):
                    board.case = f"{name}-{index:02d}"
                    row = dict(test=name, round=index, status="PASS", reason="", metrics={})
                    start = time.monotonic()
                    try:
                        row["metrics"] = getattr(board, "test_" + name)(index)
                    except (AssertionError, TimeoutError, DeviceError) as error:
                        row.update(status="FAIL", reason=str(error))
                        stop = isinstance(error, BootChanged)
                    except Exception as error:
                        row.update(status="ERROR", reason=str(error))
                        stop = True
                    row["elapsed_s"] = time.monotonic() - start
                    rows.append(row)
                    board.note("result", **row)
                    save_results(output, rows, info)
                    print(f"{row['status']} {name} {index}/{count} {row['reason']}", flush=True)
                    if stop:
                        break
                if stop:
                    break
    except (Exception, KeyboardInterrupt) as error:
        info["error"] = str(error) or "interrupted"
    finally:
        summary = save_results(output, rows, info)
        for name, item in summary.items():
            print(f"{name}: {item['PASS']}/{item['planned']} FAIL={item['FAIL']} ERROR={item['ERROR']} NOT_RUN={item['not_run']}")
        print(f"Evidence: {output.resolve()}")
    return 0 if len(rows) == 60 and all(r["status"] == "PASS" for r in rows) and "error" not in info else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"ERROR: {error}")
        raise SystemExit(2)
