/*
 * SIMP simulator.
 *
 * Runs the fetch-decode-execute loop of the SIMP processor together with its
 * I/O devices: 32 leds, a 7-segment display, a 256x256 monochrome monitor and
 * a hard disk. The three interrupt sources (timer, disk and the external irq2
 * line) are supported as well.
 *
 * Usage:
 *   simulator.exe memin.txt diskin.txt irq2in.txt memout.txt regout.txt trace.txt
 *           hwregtrace.txt cycles.txt leds.txt display7seg.txt diskout.txt
 *           monitor.txt monitor.yuv
 */

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEM_SIZE      4096
#define NUM_REGS      16
#define NUM_IOREG     23
#define DISK_SECTORS  128
#define SECTOR_WORDS  128
#define MONITOR_DIM   256
#define MONITOR_SIZE  (MONITOR_DIM * MONITOR_DIM)
#define DISK_DELAY    1024
#define MAX_LINE      512
#define MAX_IRQ2_EVENTS 65536

/* I/O register indices, matching the hardware register table. */
enum {
    IO_IRQ0ENABLE = 0, IO_IRQ1ENABLE, IO_IRQ2ENABLE,
    IO_IRQ0STATUS,     IO_IRQ1STATUS, IO_IRQ2STATUS,
    IO_IRQHANDLER,     IO_IRQRETURN,  IO_CLKS,
    IO_LEDS,           IO_DISPLAY7SEG,
    IO_TIMERENABLE,    IO_TIMERCURRENT, IO_TIMERMAX,
    IO_DISKCMD,        IO_DISKSECTOR,   IO_DISKBUFFER, IO_DISKSTATUS,
    IO_RESERVED18,     IO_RESERVED19,
    IO_MONITORADDR,    IO_MONITORDATA,  IO_MONITORCMD
};

static const char *io_names[NUM_IOREG] = {
    "irq0enable", "irq1enable", "irq2enable",
    "irq0status", "irq1status", "irq2status",
    "irqhandler", "irqreturn",  "clks",
    "leds",       "display7seg",
    "timerenable","timercurrent","timermax",
    "diskcmd",    "disksector", "diskbuffer", "diskstatus",
    "reserved",   "reserved",
    "monitoraddr","monitordata","monitorcmd"
};

/* number of bits each I/O register actually holds (32 means no masking) */
static const int io_bits[NUM_IOREG] = {
    1, 1, 1, 1, 1, 1, 12, 12, 32, 32, 32, 1, 32, 32, 2, 7, 12, 1,
    32, 32, 16, 8, 1
};

/* ---- machine state ---- */
static unsigned int reg[NUM_REGS];
static unsigned int mem[MEM_SIZE];
static unsigned int io[NUM_IOREG];
static unsigned int disk[DISK_SECTORS][SECTOR_WORDS];
static unsigned char monitor[MONITOR_SIZE];

static unsigned int pc = 0;
static unsigned int cycle = 0;
static int          in_isr = 0;      /* inside an interrupt handler */

/* external irq2 events (clock cycles on which the line goes high) */
static unsigned int irq2_events[MAX_IRQ2_EVENTS];
static int          irq2_count = 0;
static int          irq2_index = 0;

/* pending disk operation */
static int          disk_busy   = 0;
static int          disk_timer  = 0;

/* output files */
static FILE *ftrace, *fhwreg, *fleds, *f7seg;

/* Sign extend a 12 bit value to 32 bits. */
static unsigned int sign_extend12(unsigned int v)
{
    if (v & 0x800)
        return v | 0xFFFFF000u;
    return v & 0xFFF;
}

static unsigned int mask_bits(unsigned int value, int bits)
{
    if (bits >= 32)
        return value;
    return value & ((1u << bits) - 1);
}

/* ---- file loading ---- */

static void load_memory(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[MAX_LINE];
    int addr = 0;
    if (!f) {
        fprintf(stderr, "simulator: cannot open %s\n", path);
        exit(1);
    }
    while (addr < MEM_SIZE && fgets(line, sizeof(line), f))
        mem[addr++] = (unsigned int)strtoul(line, NULL, 16);
    fclose(f);
}

static void load_disk(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[MAX_LINE];
    int index = 0;
    if (!f) {
        fprintf(stderr, "simulator: cannot open %s\n", path);
        exit(1);
    }
    while (index < DISK_SECTORS * SECTOR_WORDS && fgets(line, sizeof(line), f)) {
        disk[index / SECTOR_WORDS][index % SECTOR_WORDS] =
            (unsigned int)strtoul(line, NULL, 16);
        index++;
    }
    fclose(f);
}

static void load_irq2(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[MAX_LINE];
    if (!f) {
        fprintf(stderr, "simulator: cannot open %s\n", path);
        exit(1);
    }
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '\0' || line[0] == '\n' || line[0] == '\r')
            continue;
        if (irq2_count >= MAX_IRQ2_EVENTS) {
            fprintf(stderr, "simulator: too many external interrupt events (maximum %d)\n",
                    MAX_IRQ2_EVENTS);
            fclose(f);
            exit(1);
        }
        irq2_events[irq2_count++] = (unsigned int)strtoul(line, NULL, 10);
    }
    fclose(f);
}

/* ---- output helpers ---- */

static void write_trace_line(unsigned int trace_cycle, unsigned int inst)
{
    int i;
    fprintf(ftrace, "%08X %03X %08X", trace_cycle, pc, inst);
    for (i = 0; i < NUM_REGS; i++)
        fprintf(ftrace, " %08X", reg[i]);
    fprintf(ftrace, "\n");
}

static void hwreg_log(unsigned int trace_cycle, const char *rw,
                      int index, unsigned int data)
{
    fprintf(fhwreg, "%08X %s %s %08X\n", trace_cycle, rw, io_names[index], data);
}

/* ---- I/O access (used by the in/out instructions) ---- */

static unsigned int io_read(int index, unsigned int trace_cycle)
{
    unsigned int value;

    if (index < 0 || index >= NUM_IOREG)
        return 0;

    if (index == IO_MONITORCMD)
        value = 0;                 /* reading monitorcmd always returns 0 */
    else
        value = io[index];

    hwreg_log(trace_cycle, "READ", index, value);
    return value;
}

static void io_write(int index, unsigned int value, unsigned int trace_cycle)
{
    if (index < 0 || index >= NUM_IOREG)
        return;

    value = mask_bits(value, io_bits[index]);
    hwreg_log(trace_cycle, "WRITE", index, value);

    switch (index) {
    case IO_LEDS:
        if (value != io[IO_LEDS])
            fprintf(fleds, "%08X %08X\n", trace_cycle, value);
        io[IO_LEDS] = value;
        break;

    case IO_DISPLAY7SEG:
        if (value != io[IO_DISPLAY7SEG])
            fprintf(f7seg, "%08X %08X\n", trace_cycle, value);
        io[IO_DISPLAY7SEG] = value;
        break;

    case IO_MONITORCMD:
        io[IO_MONITORCMD] = value;
        if (value == 1) {          /* draw the pixel that was set up */
            unsigned int addr = io[IO_MONITORADDR] & 0xFFFF;
            if (addr < MONITOR_SIZE)
                monitor[addr] = (unsigned char)(io[IO_MONITORDATA] & 0xFF);
        }
        break;

    case IO_DISKCMD:
        io[IO_DISKCMD] = value;
        if ((value == 1 || value == 2) && io[IO_DISKSTATUS] == 0) {
            io[IO_DISKSTATUS] = 1;
            disk_busy  = 1;
            disk_timer = DISK_DELAY;
        }
        break;

    default:
        io[index] = value;
        break;
    }
}

/* Perform the disk DMA transfer once the 1024 cycle delay has elapsed. */
static void disk_complete(void)
{
    unsigned int sector = io[IO_DISKSECTOR] & 0x7F;
    unsigned int buffer = io[IO_DISKBUFFER] & 0xFFF;
    int i;

    if (io[IO_DISKCMD] == 1) {                 /* read sector into memory */
        for (i = 0; i < SECTOR_WORDS; i++)
            mem[(buffer + i) & 0xFFF] = disk[sector][i];
    } else if (io[IO_DISKCMD] == 2) {          /* write memory into sector */
        for (i = 0; i < SECTOR_WORDS; i++)
            disk[sector][i] = mem[(buffer + i) & 0xFFF];
    }

    io[IO_DISKCMD]    = 0;
    io[IO_DISKSTATUS] = 0;
    io[IO_IRQ1STATUS] = 1;
    disk_busy = 0;
}

/* Advance the hardware by a single clock: update the timer, the disk delay and
 * sample the external irq2 line. */
static void tick(unsigned int now)
{
    /* timer */
    if (io[IO_TIMERENABLE]) {
        if (io[IO_TIMERCURRENT] == io[IO_TIMERMAX]) {
            io[IO_IRQ0STATUS]  = 1;
            io[IO_TIMERCURRENT] = 0;
        } else {
            io[IO_TIMERCURRENT]++;
        }
    }

    /* disk */
    if (disk_busy) {
        disk_timer--;
        if (disk_timer == 0)
            disk_complete();
    }

    /* external interrupt line */
    while (irq2_index < irq2_count && irq2_events[irq2_index] == now) {
        io[IO_IRQ2STATUS] = 1;
        irq2_index++;
    }
}

/* ---- main execution loop ---- */

int main(int argc, char *argv[])
{
    FILE *f;
    int   i;
    int   halted = 0;

    if (argc != 14) {
        fprintf(stderr, "usage: %s memin diskin irq2in memout regout trace "
                        "hwregtrace cycles leds display7seg diskout monitor "
                        "monitor.yuv\n", argv[0]);
        return 1;
    }

    load_memory(argv[1]);
    load_disk(argv[2]);
    load_irq2(argv[3]);

    ftrace = fopen(argv[6],  "w");
    fhwreg = fopen(argv[7],  "w");
    fleds  = fopen(argv[9],  "w");
    f7seg  = fopen(argv[10], "w");
    if (!ftrace || !fhwreg || !fleds || !f7seg) {
        fprintf(stderr, "simulator: cannot open an output file\n");
        return 1;
    }

    while (!halted) {
        unsigned int inst, imm1, imm2, trace_cycle;
        int opcode, rd, rs, rt, two_word, ncyc;
        unsigned int irq;
        int branch_taken = 0;

        io[IO_CLKS] = cycle;

        /* check for an interrupt at the instruction boundary */
        irq = (io[IO_IRQ0ENABLE] & io[IO_IRQ0STATUS]) |
              (io[IO_IRQ1ENABLE] & io[IO_IRQ1STATUS]) |
              (io[IO_IRQ2ENABLE] & io[IO_IRQ2STATUS]);
        if (irq && !in_isr) {
            io[IO_IRQRETURN] = pc;
            pc = io[IO_IRQHANDLER] & 0xFFF;
            in_isr = 1;
        }

        /* fetch and decode */
        inst   = mem[pc];
        opcode = (inst >> 24) & 0xFF;
        rd     = (inst >> 20) & 0xF;
        rs     = (inst >> 16) & 0xF;
        rt     = (inst >> 12) & 0xF;
        imm1   = inst & 0xFFF;

        two_word = (rd == 2 || rs == 2 || rt == 2);
        imm2     = two_word ? mem[(pc + 1) & 0xFFF] : 0;
        ncyc     = two_word ? 2 : 1;

        /* the constant registers are recomputed every instruction */
        reg[0] = 0;
        reg[1] = sign_extend12(imm1);
        reg[2] = imm2;

        trace_cycle = cycle + (two_word ? 1 : 0);
        write_trace_line(trace_cycle, inst);

        /* execute */
        switch (opcode) {
        case 0:  reg[rd] = reg[rs] + reg[rt]; break;                 /* add */
        case 1:  reg[rd] = reg[rs] - reg[rt]; break;                 /* sub */
        case 2:  reg[rd] = reg[rs] * reg[rt]; break;                 /* mul */
        case 3:  reg[rd] = reg[rs] * reg[rt] + reg[rd]; break;       /* mac */
        case 4:  reg[rd] = reg[rs] & reg[rt]; break;                 /* and */
        case 5:  reg[rd] = reg[rs] | reg[rt]; break;                 /* or  */
        case 6:  reg[rd] = reg[rs] ^ reg[rt]; break;                 /* xor */
        case 7:  reg[rd] = reg[rs] << (reg[rt] & 31); break;         /* sll */
        case 8:  reg[rd] = (unsigned int)((int)reg[rs] >> (reg[rt] & 31));
                 break;                                              /* sra */
        case 9:  reg[rd] = reg[rs] >> (reg[rt] & 31); break;         /* srl */
        case 10: if (reg[rs] == reg[rt]) { pc = reg[rd] & 0xFFF; branch_taken = 1; }
                 break;                                              /* beq */
        case 11: if (reg[rs] != reg[rt]) { pc = reg[rd] & 0xFFF; branch_taken = 1; }
                 break;                                              /* bne */
        case 12: if ((int)reg[rs] <  (int)reg[rt]) { pc = reg[rd] & 0xFFF; branch_taken = 1; }
                 break;                                              /* blt */
        case 13: if ((int)reg[rs] >  (int)reg[rt]) { pc = reg[rd] & 0xFFF; branch_taken = 1; }
                 break;                                              /* bgt */
        case 14: if ((int)reg[rs] <= (int)reg[rt]) { pc = reg[rd] & 0xFFF; branch_taken = 1; }
                 break;                                              /* ble */
        case 15: if ((int)reg[rs] >= (int)reg[rt]) { pc = reg[rd] & 0xFFF; branch_taken = 1; }
                 break;                                              /* bge */
        case 16:                                                    /* jal */
            reg[rd] = pc + ncyc;
            pc = reg[rs] & 0xFFF;
            branch_taken = 1;
            break;
        case 17:                                                    /* lw */
            reg[rd] = mem[(reg[rs] + reg[rt]) & 0xFFF];
            break;
        case 18:                                                    /* sw */
            mem[(reg[rs] + reg[rt]) & 0xFFF] = reg[rd];
            break;
        case 19:                                                    /* reti */
            pc = io[IO_IRQRETURN] & 0xFFF;
            in_isr = 0;
            branch_taken = 1;
            break;
        case 20:                                                    /* in */
            reg[rd] = io_read((reg[rs] + reg[rt]) & 0xFF, trace_cycle);
            break;
        case 21:                                                    /* out */
            io_write((reg[rs] + reg[rt]) & 0xFF, reg[rd], trace_cycle);
            break;
        case 22:                                                    /* halt */
            halted = 1;
            break;
        default:
            break;
        }

        /* writes to the constant registers are discarded */
        reg[0] = 0;
        reg[1] = sign_extend12(imm1);
        reg[2] = imm2;

        if (!branch_taken && !halted)
            pc = (pc + ncyc) & 0xFFF;

        /* advance the clock over the cycles this instruction consumed */
        for (i = 0; i < ncyc; i++)
            tick(cycle + i);
        cycle += ncyc;
    }

    fclose(ftrace);
    fclose(fhwreg);
    fclose(fleds);
    fclose(f7seg);

    /* memout: memory up to the last non-zero word */
    f = fopen(argv[4], "w");
    if (f) {
        int last = -1;
        for (i = 0; i < MEM_SIZE; i++)
            if (mem[i] != 0)
                last = i;
        for (i = 0; i <= last; i++)
            fprintf(f, "%08X\n", mem[i]);
        fclose(f);
    }

    /* regout: registers R3..R15 */
    f = fopen(argv[5], "w");
    if (f) {
        for (i = 3; i < NUM_REGS; i++)
            fprintf(f, "%08X\n", reg[i]);
        fclose(f);
    }

    /* cycles */
    f = fopen(argv[8], "w");
    if (f) {
        fprintf(f, "%08X\n", cycle);
        fclose(f);
    }

    /* diskout: the whole disk */
    f = fopen(argv[11], "w");
    if (f) {
        int s, w;
        for (s = 0; s < DISK_SECTORS; s++)
            for (w = 0; w < SECTOR_WORDS; w++)
                fprintf(f, "%08X\n", disk[s][w]);
        fclose(f);
    }

    /* monitor.txt: pixels up to the last non-zero one */
    f = fopen(argv[12], "w");
    if (f) {
        int last = -1;
        for (i = 0; i < MONITOR_SIZE; i++)
            if (monitor[i] != 0)
                last = i;
        for (i = 0; i <= last; i++)
            fprintf(f, "%02X\n", monitor[i]);
        fclose(f);
    }

    /* monitor.yuv: the raw frame buffer */
    f = fopen(argv[13], "wb");
    if (f) {
        fwrite(monitor, 1, MONITOR_SIZE, f);
        fclose(f);
    }

    return 0;
}
