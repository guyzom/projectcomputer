# SIMP Assembler and Processor Simulator

A C implementation of a two-pass assembler and a software simulator for the SIMP processor. Assembly programs run against a unified memory model, clocked I/O devices and three interrupt sources, producing instruction traces and device outputs.

The repository includes four assembly programs: descending bubble sort, recursive factorial, disk-sector accumulation and a filled-circle renderer.

## Processor and device model

- 23 instructions, 16 32-bit registers and 4,096 words of instruction/data memory.
- One-word instructions with a signed 12-bit immediate; a second 32-bit word is fetched when an operand uses `$imm2`.
- 23 I/O-register addresses covering interrupts, a timer, LEDs, a seven-segment display, disk control and a monitor.
- A disk with 128 sectors of 128 words, DMA transfers and a 1,024-cycle transfer delay.
- A 256 × 256 grayscale framebuffer, exported as text and raw bytes.
- Timer (`irq0`), disk (`irq1`) and external (`irq2`) interrupts, a shared handler and `reti` return.

The simulator models instruction execution and peripheral timing in software. It does not model a pipeline, cache hierarchy or RTL implementation.

## Build and test

On Linux or macOS, use a C11 compiler, GNU Make and Python 3. The test runner uses only the Python standard library.

```sh
make all
make test
```

The executables are `build/assembler` and `build/simulator`. The compiler is selected through `CC`; use `make CC=gcc` or `make CC=clang` to select it explicitly.

To build directly with GCC:

```sh
mkdir -p build
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 assembler/assembler/assembler.c -o build/assembler
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 simulator/simulator/simulator.c -o build/simulator
```

AddressSanitizer and UndefinedBehaviorSanitizer checks use separate binaries:

```sh
make test-sanitize
```

Each test run writes its outputs to a new directory under `build/test-results/`. The runner prints that directory and applies a timeout to each subprocess.

### Test coverage

The suite assembles and runs all four demos, checks their memory images, and compares the results to independent expected values:

| Program | Result checked |
| --- | --- |
| `sort` | All 16 values are preserved and sorted in descending order at `0x100`–`0x10F`. |
| `factorial` | The result at `0x101` equals `8! = 40,320`. |
| `disktest` | Sector 8 contains the wordwise sum of sectors 0–7; the other sectors are unchanged. |
| `circle` | All 65,536 pixels match the filled-circle predicate for the supplied center and radius. |

Additional fixtures exercise timer, external and disk interrupts, including return from the handler and a disk DMA transfer. Assembler checks cover invalid instructions, registers, literals, labels, fields and memory bounds, including failure without overwriting an existing output file.

These checks cover the demos and selected edge cases; they are not an exhaustive ISA or timing-conformance suite.

## Run a program

From the repository root, assemble and run the factorial example:

```sh
mkdir -p build/example
build/assembler factorial/factorial.asm build/example/memin.txt
build/simulator \
  build/example/memin.txt factorial/diskin.txt factorial/irq2in.txt \
  build/example/memout.txt build/example/regout.txt build/example/trace.txt \
  build/example/hwregtrace.txt build/example/cycles.txt build/example/leds.txt \
  build/example/display7seg.txt build/example/diskout.txt \
  build/example/monitor.txt build/example/monitor.yuv
```

The simulator accepts exactly 13 file-path arguments:

```text
simulator memin diskin irq2in memout regout trace hwregtrace cycles leds display7seg diskout monitor monitor.yuv
```

`memin` and `diskin` contain hexadecimal words, one per line. `irq2in` contains up to 65,536 external-interrupt clock cycles in decimal, in ascending order. Files exceeding this event capacity are rejected before output files are opened. Empty disk and interrupt files are valid. All input files must exist, and output directories must be created before running.

| Output | Contents |
| --- | --- |
| `memout` | Final memory, through the last nonzero word. |
| `regout` | Final register values, R3–R15. |
| `trace` | Execution cycle, PC, instruction word and all 16 register values before execution. |
| `hwregtrace` | I/O-register reads and writes. |
| `cycles` | Total executed clock cycles, in hexadecimal. |
| `leds`, `display7seg` | Clock cycles and values when the corresponding device changes. |
| `diskout` | All 16,384 disk words. |
| `monitor` | Framebuffer text, through the last nonzero pixel. |
| `monitor.yuv` | 65,536 raw grayscale bytes, one byte per pixel in row-major order. |

`monitor.yuv` is a raw framebuffer, without an image header or chroma planes. Programs must execute `halt` to terminate; long-running programs can generate large trace files.

## Assembly format

Instructions use five or six comma- or whitespace-separated fields: `opcode rd rs rt imm1 [imm2]`. The optional second immediate defaults to zero.

```asm
# opcode rd, rs, rt, imm1, imm2
start:
    add  $a0, $zero, $imm1, 8, 0
    halt $zero, $zero, $zero, 0, 0

.word  0x100 8
.array 0x110 4, 1, 3, 2
```

The assembler collects label addresses in its first pass, then resolves immediate values and encodes instructions in its second pass. Labels may be referenced before their definitions. `.word address value` and `.array address values...` write data at explicit addresses without advancing the instruction counter. Comments begin with `#`.

Labels start with a letter or underscore and then contain letters, digits or underscores, up to 63 characters. Literals can be signed decimal or `0x` hexadecimal values in the range −2,147,483,648 through 4,294,967,295. `$imm` is an alias for `$imm1`. Data addresses must be within `0`–`4095`.

Invalid input produces a source-file/line diagnostic and a nonzero exit status. An existing destination file is left unchanged when assembly validation fails.

The first word encodes `opcode[31:24]`, `rd[23:20]`, `rs[19:16]`, `rt[15:12]` and `imm1[11:0]`. If any register operand is `$imm2`, the next word contains the second immediate. Such an instruction occupies two memory words and consumes two simulated cycles.

## Repository layout

```text
assembler/assembler/assembler.c    Two-pass assembler
simulator/simulator/simulator.c    CPU execution and peripheral simulation
sort/                             Descending bubble-sort demo
factorial/                        Recursive factorial demo
circle/                           Framebuffer drawing demo
disktest/                         Disk DMA and polling demo
tests/                            Test runner and assembly fixtures
Makefile                          Portable build and test targets
documentation.md                  Detailed implementation guide in Hebrew
```

Visual Studio 2022 solutions are retained at `assembler/assembler.sln` and `simulator/simulator.sln`. They use the v143 toolset and x64 Debug/Release configurations, producing `assembler.exe` and `simulator.exe`; executable locations depend on the selected configuration. The automated command-line builds use GCC or Clang.

See [documentation.md](documentation.md) for the execution loop, device behavior and demo algorithms.
