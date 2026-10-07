#!/usr/bin/env python3
"""Reproducible SIMP examples, interrupt scenarios, and assembler regressions.

Only the Python standard library is required. Each executable invocation has a
10-second limit. Generated files stay in a unique directory under build/.
"""

import argparse
from collections import Counter
import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
MEM_SIZE = 4096
DISK_WORDS = 128 * 128
TIMEOUT_SECONDS = 10
OUTPUT_NAMES = (
    "memout.txt", "regout.txt", "trace.txt", "hwregtrace.txt", "cycles.txt",
    "leds.txt", "display7seg.txt", "diskout.txt", "monitor.txt", "monitor.yuv",
)


class TestFailure(Exception):
    pass


def require(condition, message):
    if not condition:
        raise TestFailure(message)


def run(command, *, expect_success=True, diagnostic_prefix="assembler:"):
    environment = os.environ.copy()
    # UndefinedBehaviorSanitizer otherwise reports some failures without exiting.
    environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    try:
        result = subprocess.run(
            [str(item) for item in command], capture_output=True, text=True,
            timeout=TIMEOUT_SECONDS, env=environment,
        )
    except subprocess.TimeoutExpired as error:
        raise TestFailure(f"{Path(command[0]).name} exceeded {TIMEOUT_SECONDS}s") from error
    diagnostics = result.stderr[-4000:]
    if expect_success:
        require(result.returncode == 0,
                f"{Path(command[0]).name} exited {result.returncode}: {diagnostics}")
    else:
        require(result.returncode > 0,
                f"invalid input did not fail normally (exit {result.returncode}): {diagnostics}")
        require(diagnostic_prefix in diagnostics,
                f"invalid input had no {diagnostic_prefix} diagnostic: {diagnostics}")
        require("AddressSanitizer" not in diagnostics and "runtime error:" not in diagnostics,
                f"sanitizer found an error: {diagnostics}")
    return result


def hex_words(path, length=None):
    try:
        words = [int(line.strip(), 16) for line in path.read_text().splitlines()
                 if line.strip()]
    except (ValueError, OSError) as error:
        raise TestFailure(f"cannot read hexadecimal output {path.name}: {error}") from error
    require(all(0 <= word <= 0xFFFFFFFF for word in words),
            f"out-of-range word in {path.name}")
    if length is not None:
        require(len(words) <= length, f"too many words in {path.name}")
        words.extend([0] * (length - len(words)))
    return words


def write_words(path, words):
    path.write_text("".join(f"{word:08X}\n" for word in words))


class Suite:
    def __init__(self, assembler, simulator, work):
        self.assembler = assembler
        self.simulator = simulator
        self.work = work

    def assemble(self, source, output):
        run([self.assembler, source, output])
        require(output.is_file(), "assembler did not create its memory image")

    def simulate(self, memory, disk, irq, destination):
        outputs = {name: destination / name for name in OUTPUT_NAMES}
        run([self.simulator, memory, disk, irq, *outputs.values()])
        require(all(path.is_file() for path in outputs.values()),
                "simulator did not produce every required output file")
        require(len(hex_words(outputs["regout.txt"])) == 13,
                "regout must contain R3 through R15")
        cycles = hex_words(outputs["cycles.txt"])
        require(len(cycles) == 1 and cycles[0] > 0, "invalid cycle count")
        require(len(hex_words(outputs["diskout.txt"])) == DISK_WORDS,
                "diskout must contain all 128 sectors")
        require(outputs["monitor.yuv"].stat().st_size == 256 * 256,
                "raw monitor frame must contain 65536 bytes")
        return outputs

    def examples(self):
        for name in ("sort", "factorial", "disktest", "circle"):
            directory = self.work / name
            directory.mkdir()
            source = ROOT / name
            memory = directory / "memin.txt"
            self.assemble(source / f"{name}.asm", memory)
            before = hex_words(memory, MEM_SIZE)
            require(before == hex_words(source / "memin.txt", MEM_SIZE),
                    f"{name}: assembled image differs from the supplied memory image")
            outputs = self.simulate(memory, source / "diskin.txt",
                                    source / "irq2in.txt", directory)
            after = hex_words(outputs["memout.txt"], MEM_SIZE)
            disk_before = hex_words(source / "diskin.txt", DISK_WORDS)
            disk_after = hex_words(outputs["diskout.txt"], DISK_WORDS)

            if name == "sort":
                original = before[0x100:0x110]
                sorted_array = after[0x100:0x110]
                require(len(original) == 16 and Counter(original) == Counter(sorted_array),
                        "sort: input elements were lost or duplicated")
                require(sorted_array == sorted(original, reverse=True),
                        "sort: values are not in descending order")
                require(after[:0x100] == before[:0x100] and after[0x110:] == before[0x110:],
                        "sort: memory outside the array changed")
            elif name == "factorial":
                require(before[0x100] == 8 and after[0x100] == 8,
                        "factorial: input n must remain 8")
                require(after[0x101] == 40320, "factorial: result must be 8! = 40320")
                require(hex_words(outputs["regout.txt"])[0] == 40320,
                        "factorial: R3 must retain the returned result")
            elif name == "disktest":
                expected_sector = [
                    sum(disk_before[sector * 128 + index] for sector in range(8)) & 0xFFFFFFFF
                    for index in range(128)
                ]
                require(disk_after[8 * 128:9 * 128] == expected_sector,
                        "disktest: sector 8 is not the word-by-word sum of sectors 0..7")
                require(disk_after[:8 * 128] == disk_before[:8 * 128]
                        and disk_after[9 * 128:] == disk_before[9 * 128:],
                        "disktest: a sector other than 8 changed")
            else:
                require(before[0x100:0x102] == [0x8080, 50],
                        "circle: expected center (128,128) and radius 50")
                expected = bytes(
                    255 if (column - 128) ** 2 + (row - 128) ** 2 <= 50 ** 2 else 0
                    for row in range(256) for column in range(256)
                )
                require(outputs["monitor.yuv"].read_bytes() == expected,
                        "circle: raw frame differs from the independent circle predicate")
                require(hex_words(outputs["monitor.txt"], 256 * 256) == list(expected),
                        "circle: textual frame differs from the independent circle predicate")
                require(after == before, "circle: memory changed")

            if name != "disktest":
                require(disk_after == disk_before, f"{name}: disk changed")
            print(f"PASS {name}: image baseline and independent result")

    def interrupts(self):
        for name, return_pc in (("irq2", 4), ("timer", 8), ("disk-irq", 10)):
            directory = self.work / name
            directory.mkdir()
            memory = directory / "memin.txt"
            disk = directory / "diskin.txt"
            irq = directory / "irq2in.txt"
            self.assemble(ROOT / "tests" / "fixtures" / f"{name}.asm", memory)
            disk_values = [((index * 65537) ^ 0xA5A55A5A) & 0xFFFFFFFF
                           for index in range(4 * 128)] if name == "disk-irq" else []
            write_words(disk, disk_values)
            irq.write_text("20\n" if name == "irq2" else "")
            outputs = self.simulate(memory, disk, irq, directory)
            mem = hex_words(outputs["memout.txt"], MEM_SIZE)
            require(hex_words(outputs["regout.txt"])[0] == 1,
                    f"{name}: handler must run exactly once")
            if name == "disk-irq":
                require(mem[0x300:0x380] == disk_values[3 * 128:4 * 128],
                        "disk-irq: DMA buffer differs from sector 3")
                require(mem[0x380:0x383] == [1, 0, return_pc],
                        "disk-irq: wrong irq1 status, disk status or return address")
                require(hex_words(outputs["cycles.txt"])[0] >= 1024,
                        "disk-irq: completed before disk latency elapsed")
            else:
                require(mem[0x300:0x302] == [123, return_pc],
                        f"{name}: caller did not resume at the saved return address")
            trace_instructions = [int(line.split()[2], 16)
                                  for line in outputs["trace.txt"].read_text().splitlines()]
            require(sum(instruction >> 24 == 19 for instruction in trace_instructions) == 1,
                    f"{name}: expected exactly one reti instruction")
            hw_lines = [line.split() for line in outputs["hwregtrace.txt"].read_text().splitlines()]
            status_register = {"irq2": "irq2status", "timer": "irq0status",
                               "disk-irq": "irq1status"}[name]
            require(any(fields[1:] == ["WRITE", status_register, "00000000"]
                        for fields in hw_lines), f"{name}: interrupt status was not acknowledged")
            if name == "timer":
                require(any(fields[1:] == ["WRITE", "timerenable", "00000000"]
                            for fields in hw_lines), "timer: handler did not stop the timer")
            require(hex_words(outputs["diskout.txt"], DISK_WORDS)
                    == disk_values + [0] * (DISK_WORDS - len(disk_values)),
                    f"{name}: read-only scenario changed the disk")
            print(f"PASS {name}: interrupt, acknowledgement and reti")

    def simulator_input_limits(self):
        directory = self.work / "irq2-capacity"
        directory.mkdir()
        source = directory / "halt.asm"
        memory = directory / "memin.txt"
        disk = directory / "diskin.txt"
        source.write_text("halt $zero, $zero, $zero, 0, 0\n")
        disk.write_text("")
        self.assemble(source, memory)

        accepted = directory / "accepted"
        accepted.mkdir()
        irq = directory / "65536-events.txt"
        irq.write_text("0\n" * 65536)
        outputs = self.simulate(memory, disk, irq, accepted)
        require(hex_words(outputs["cycles.txt"]) == [1],
                "irq2 capacity: maximum input did not halt in one cycle")
        require(hex_words(outputs["regout.txt"]) == [0] * 13,
                "irq2 capacity: disabled external interrupts changed registers")
        require(hex_words(outputs["memout.txt"]) == hex_words(memory),
                "irq2 capacity: maximum input changed memory")

        rejected = directory / "rejected"
        rejected.mkdir()
        irq = directory / "65537-events.txt"
        irq.write_text("0\n" * 65537)
        outputs = [rejected / name for name in OUTPUT_NAMES]
        result = run([self.simulator, memory, disk, irq, *outputs],
                     expect_success=False, diagnostic_prefix="simulator:")
        require("too many external interrupt events (maximum 65536)" in result.stderr,
                "irq2 capacity: overflow input had no clear capacity diagnostic")
        require(not any(path.exists() for path in outputs),
                "irq2 capacity: overflow input created output files")
        print("PASS simulator input limits: 65536 IRQ2 events accepted; 65537 rejected before outputs")

    def assembler_regressions(self):
        directory = self.work / "assembler"
        directory.mkdir()
        halt = "halt $zero, $zero, $zero, 0, 0\n"
        edges = (
            "_start: add $t0, $zero, $imm, -1\n"
            "add $t1 $zero $imm2 0 0xFFFFFFFF\n"
            "beq $imm1, $zero, $zero, done, 0\n" + halt
            + "done: " + halt
            + ".word 0x100 -2147483648\n"
            + ".array 0x101 0xFFFFFFFF, +0X2A, -0x1, 0008, _start, done\n"
            + ".word 4095 4294967295\n"
        )
        expected = [0] * MEM_SIZE
        expected[:6] = [0x00801FFF, 0x00902000, 0xFFFFFFFF, 0x0A100005,
                        0x16000000, 0x16000000]
        expected[0x100:0x107] = [0x80000000, 0xFFFFFFFF, 42, 0xFFFFFFFF, 8, 0, 5]
        expected[4095] = 0xFFFFFFFF
        valid = (
            ("numeric-and-label-edges", edges, expected),
            ("maximum-label", "_" + "a" * 62 + ": " + halt, [0x16000000]),
            ("full-memory", halt * MEM_SIZE, [0x16000000] * MEM_SIZE),
            ("crlf-no-final-newline", "# comment\r\n\r\n" + halt.rstrip("\n"), [0x16000000]),
            ("optional-imm2", "add $t0 $zero $imm2 0\n" + halt,
             [0x00802000, 0, 0x16000000]),
        )
        for name, text, expected_words in valid:
            source = directory / f"valid-{name}.asm"
            output = directory / f"valid-{name}.txt"
            source.write_text(text)
            self.assemble(source, output)
            require(hex_words(output) == expected_words, f"assembler: wrong encoding for {name}")

        invalid = {
            "unknown-opcode": "unknown $zero $zero $zero 0\n",
            "unknown-rd": "add $bad $zero $zero 0\n",
            "unknown-rs": "add $t0 $bad $zero 0\n",
            "unknown-rt": "add $t0 $zero $bad 0\n",
            "missing-field": "add $t0 $zero $zero\n",
            "extra-field": "add $t0 $zero $zero 0 0 0\n",
            "undefined-label": "beq $imm1 $zero $zero absent\n",
            "undefined-word-value": ".word 256 absent\n",
            "duplicate-label": "same: " + halt + "same: " + halt,
            "invalid-label": "1bad: " + halt,
            "label-too-long": "a" * 64 + ": " + halt,
            "token-too-long": "add $t0 $zero $imm1 " + "a" * 64 + "\n",
            "line-too-long": "#" + "a" * 511 + "\n",
            "unknown-directive": ".unknown 256 1\n",
            "word-missing-value": ".word 256\n",
            "word-extra-value": ".word 256 1 2\n",
            "array-missing-value": ".array 256\n",
            "negative-address": ".word -1 42\n",
            "address-too-high": ".word 4096 42\n",
            "unsigned-address-too-high": ".word 0xFFFFFFFF 42\n",
            "array-overflow": ".array 4095 1 2\n",
            "positive-overflow": ".word 256 4294967296\n",
            "negative-overflow": ".word 256 -2147483649\n",
            "invalid-decimal": "add $t0 $zero $imm1 12abc\n",
            "invalid-hex": "add $t0 $zero $imm1 0xGG\n",
            "trailing-hex-garbage": ".word 256 0x1g\n",
            "bare-sign": ".word 256 -\n",
            "leading-comma": ",add $t0 $zero $zero 0\n",
            "consecutive-comma": "add $t0,, $zero, $zero, 0\n",
            "trailing-comma": "add $t0, $zero, $zero, 0,\n",
            "instruction-overflow": halt * (MEM_SIZE + 1),
            "two-word-overflow": halt * (MEM_SIZE - 1) + "add $t0 $zero $imm2 0 1\n",
        }
        sentinel = b"existing-output-must-remain-unchanged\n"
        for name, text in invalid.items():
            source = directory / f"invalid-{name}.asm"
            absent = directory / f"invalid-{name}-absent.txt"
            existing = directory / f"invalid-{name}-existing.txt"
            source.write_text(text)
            result = run([self.assembler, source, absent], expect_success=False)
            require(f"{source}:" in result.stderr, f"{name}: no source/line diagnostic")
            require(not absent.exists(), f"{name}: invalid input created an output")
            existing.write_bytes(sentinel)
            run([self.assembler, source, existing], expect_success=False)
            require(existing.read_bytes() == sentinel, f"{name}: existing output was changed")
        print(f"PASS assembler: {len(valid)} valid boundaries, {len(invalid)} invalid inputs; outputs preserved")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assembler", type=Path, default=ROOT / "build" / "assembler")
    parser.add_argument("--simulator", type=Path, default=ROOT / "build" / "simulator")
    arguments = parser.parse_args()
    assembler = arguments.assembler.resolve()
    simulator = arguments.simulator.resolve()
    for executable in (assembler, simulator):
        if not executable.is_file():
            parser.error(f"executable does not exist: {executable}; run make all first")
    results = ROOT / "build" / "test-results"
    results.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="run-", dir=results))
    print(f"Test outputs: {work.relative_to(ROOT)}", flush=True)
    suite = Suite(assembler, simulator, work)
    try:
        suite.examples()
        suite.interrupts()
        suite.simulator_input_limits()
        suite.assembler_regressions()
    except (TestFailure, OSError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print("All tests passed. Coverage is targeted; this is not complete ISA conformance testing.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
