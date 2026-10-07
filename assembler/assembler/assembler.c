/*
 * SIMP assembler.
 *
 * Translates an assembly program into the simulator's memory image in two
 * passes. The first pass records label addresses; the second encodes words.
 * Instructions use five or six fields: opcode rd rs rt imm1 [imm2].
 * A register operand of $imm2 makes an instruction occupy two words.
 * .word address value and .array address value... write data independently
 * of the instruction counter. Decimal and hexadecimal literals represent
 * 32-bit words; labels name instruction-counter addresses.
 *
 * Usage: assembler program.asm memin.txt
 */

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>

#define MEM_SIZE     4096
#define MAX_LINE     512     /* Includes the terminating null byte. */
#define MAX_LABEL    64      /* Includes the terminating null byte. */
#define MAX_LABELS   4096
#define MAX_FIELDS   300

typedef struct {
    char name[MAX_LABEL];
    int address;
} Label;

static Label labels[MAX_LABELS];
static int label_count;
static uint32_t mem[MEM_SIZE];
static int max_addr = -1;

static int diagnostic(const char *path, unsigned long line, const char *format, ...)
{
    va_list arguments;
    if (line != 0)
        fprintf(stderr, "%s:%lu: assembler: ", path, line);
    else
        fprintf(stderr, "%s: assembler: ", path);
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    return 0;
}

static int reg_number(const char *name)
{
    static const char *names[16] = {
        "$zero", "$imm1", "$imm2", "$v0", "$a0", "$a1", "$a2", "$a3",
        "$t0", "$t1", "$t2", "$s0", "$s1", "$gp", "$sp", "$ra"
    };
    int i;
    for (i = 0; i < 16; i++) {
        if (strcmp(name, names[i]) == 0)
            return i;
    }
    return strcmp(name, "$imm") == 0 ? 1 : -1;
}

static int opcode_number(const char *name)
{
    static const char *names[23] = {
        "add", "sub", "mul", "mac", "and", "or", "xor", "sll",
        "sra", "srl", "beq", "bne", "blt", "bgt", "ble", "bge",
        "jal", "lw", "sw", "reti", "in", "out", "halt"
    };
    int i;
    for (i = 0; i < 23; i++) {
        if (strcmp(name, names[i]) == 0)
            return i;
    }
    return -1;
}

static int identifier_start(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int valid_label(const char *name)
{
    size_t i;
    if (!identifier_start((unsigned char)name[0]))
        return 0;
    for (i = 1; name[i] != '\0'; i++) {
        unsigned char c = (unsigned char)name[i];
        if (!identifier_start(c) && !(c >= '0' && c <= '9'))
            return 0;
    }
    return i < MAX_LABEL;
}

static int find_label(const char *name)
{
    int i;
    for (i = 0; i < label_count; i++) {
        if (strcmp(labels[i].name, name) == 0)
            return labels[i].address;
    }
    return -1;
}

/* Returns 1 for a complete line, 0 at EOF, and -1 for an input error. */
static int read_line(FILE *input, char *line, const char *path, unsigned long number)
{
    size_t count = 0;
    int c, invalid = 0;
    while ((c = fgetc(input)) != EOF && c != '\n') {
        if (c == '\0' && !invalid) {
            diagnostic(path, number, "null byte in input");
            invalid = 1;
        }
        if (count >= MAX_LINE - 1) {
            if (!invalid)
                diagnostic(path, number, "line exceeds %d characters", MAX_LINE - 1);
            invalid = 1;
        } else {
            line[count++] = (char)c;
        }
    }
    line[count] = '\0';
    if (ferror(input)) {
        diagnostic(path, number, "cannot read input");
        return -1;
    }
    if (invalid)
        return -1;
    return c == EOF && count == 0 ? 0 : 1;
}

/* Removes a comment and a single leading label, then tokenizes the statement.
 * Whitespace and single commas separate fields; empty comma fields are invalid.
 */
static int parse_line(char *line, char *label, char *fields[], int *count,
                      const char *path, unsigned long number)
{
    char *p, *colon, *comment;
    label[0] = '\0';
    *count = 0;
    comment = strchr(line, '#');
    if (comment)
        *comment = '\0';

    p = line;
    while (isspace((unsigned char)*p))
        p++;
    colon = strchr(p, ':');
    if (colon) {
        char *end = colon;
        size_t length;
        if (strchr(colon + 1, ':'))
            return diagnostic(path, number, "multiple labels on one line");
        while (end > p && isspace((unsigned char)end[-1]))
            end--;
        length = (size_t)(end - p);
        if (length == 0 || length >= MAX_LABEL)
            return diagnostic(path, number, "label must contain 1 to %d characters", MAX_LABEL - 1);
        memcpy(label, p, length);
        label[length] = '\0';
        if (!valid_label(label))
            return diagnostic(path, number, "invalid label '%s'", label);
        p = colon + 1;
        while (isspace((unsigned char)*p))
            p++;
    }

    if (*p == ',')
        return diagnostic(path, number, "empty field before comma");
    while (*p != '\0') {
        char *start = p;
        int commas = 0;
        if (*count >= MAX_FIELDS)
            return diagnostic(path, number, "too many fields (maximum %d)", MAX_FIELDS);
        while (*p && !isspace((unsigned char)*p) && *p != ',')
            p++;
        if ((size_t)(p - start) >= MAX_LABEL)
            return diagnostic(path, number, "token exceeds %d characters", MAX_LABEL - 1);
        fields[(*count)++] = start;
        if (*p) {
            if (*p == ',')
                commas++;
            *p++ = '\0';
        }
        while (isspace((unsigned char)*p) || *p == ',') {
            if (*p == ',' && ++commas > 1)
                return diagnostic(path, number, "empty field between commas");
            p++;
        }
        if (*p == '\0' && commas != 0)
            return diagnostic(path, number, "empty field after comma");
    }
    return 1;
}

/* Labels are syntax-checked in pass one and resolved in pass two. Numeric
 * literals accept an optional sign, decimal or 0x hexadecimal digits, and
 * values from INT32_MIN through UINT32_MAX.
 */
static int parse_value(const char *token, int resolve, uint32_t *value,
                       const char *path, unsigned long number)
{
    const char *p = token;
    uint64_t magnitude = 0;
    uint64_t limit;
    unsigned int base = 10;
    int negative = 0;

    if (identifier_start((unsigned char)*p)) {
        int address;
        if (!valid_label(token))
            return diagnostic(path, number, "invalid label reference '%s'", token);
        address = resolve ? find_label(token) : 0;
        if (address < 0)
            return diagnostic(path, number, "undefined label '%s'", token);
        *value = (uint32_t)address;
        return 1;
    }

    if (*p == '+' || *p == '-') {
        negative = *p == '-';
        p++;
    }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    }
    if (*p == '\0')
        return diagnostic(path, number, "invalid numeric literal '%s'", token);
    limit = negative ? UINT64_C(2147483648) : UINT64_C(4294967295);
    for (; *p; p++) {
        unsigned int digit;
        if (*p >= '0' && *p <= '9')
            digit = (unsigned int)(*p - '0');
        else if (*p >= 'a' && *p <= 'f')
            digit = (unsigned int)(*p - 'a') + 10;
        else if (*p >= 'A' && *p <= 'F')
            digit = (unsigned int)(*p - 'A') + 10;
        else
            return diagnostic(path, number, "invalid numeric literal '%s'", token);
        if (digit >= base)
            return diagnostic(path, number, "invalid numeric literal '%s'", token);
        if (magnitude > (limit - digit) / base)
            return diagnostic(path, number, "numeric literal out of 32-bit range '%s'", token);
        magnitude = magnitude * base + digit;
    }
    *value = negative ? UINT32_C(0) - (uint32_t)magnitude : (uint32_t)magnitude;
    return 1;
}

static void store_word(int address, uint32_t value)
{
    mem[address] = value;
    if (address > max_addr)
        max_addr = address;
}

static int assemble_pass(FILE *input, const char *path, int pass)
{
    char line[MAX_LINE], label[MAX_LABEL];
    char *fields[MAX_FIELDS];
    int address = 0, status;
    unsigned long number = 0;

    while ((status = read_line(input, line, path, ++number)) > 0) {
        int count, i, opcode, registers[3], words;
        uint32_t immediate1, immediate2 = 0;
        if (!parse_line(line, label, fields, &count, path, number))
            return 0;
        if (pass == 1 && label[0]) {
            if (find_label(label) >= 0)
                return diagnostic(path, number, "duplicate label '%s'", label);
            if (address >= MEM_SIZE)
                return diagnostic(path, number, "label address exceeds memory");
            if (label_count >= MAX_LABELS)
                return diagnostic(path, number, "too many labels (maximum %d)", MAX_LABELS);
            strcpy(labels[label_count].name, label);
            labels[label_count++].address = address;
        }
        if (count == 0)
            continue;

        if (fields[0][0] == '.') {
            uint32_t data_address, value;
            int is_word = strcmp(fields[0], ".word") == 0;
            if (!is_word && strcmp(fields[0], ".array") != 0)
                return diagnostic(path, number, "unknown directive '%s'", fields[0]);
            if ((is_word && count != 3) || (!is_word && count < 3))
                return diagnostic(path, number, "%s requires an address and %s", fields[0],
                                  is_word ? "one value" : "at least one value");
            if (!parse_value(fields[1], pass == 2, &data_address, path, number))
                return 0;
            if (pass == 2 || !identifier_start((unsigned char)fields[1][0])) {
                if (data_address >= MEM_SIZE || (uint32_t)(count - 2) > MEM_SIZE - data_address)
                    return diagnostic(path, number, "%s address range exceeds memory (0 to %d)",
                                      fields[0], MEM_SIZE - 1);
            }
            for (i = 2; i < count; i++) {
                if (!parse_value(fields[i], pass == 2, &value, path, number))
                    return 0;
                if (pass == 2)
                    store_word((int)data_address + i - 2, value);
            }
            continue;
        }

        opcode = opcode_number(fields[0]);
        if (opcode < 0)
            return diagnostic(path, number, "unknown opcode '%s'", fields[0]);
        if (count != 5 && count != 6)
            return diagnostic(path, number, "instruction requires 5 or 6 fields, found %d", count);
        for (i = 0; i < 3; i++) {
            registers[i] = reg_number(fields[i + 1]);
            if (registers[i] < 0)
                return diagnostic(path, number, "unknown register '%s'", fields[i + 1]);
        }
        if (!parse_value(fields[4], pass == 2, &immediate1, path, number) ||
            (count == 6 && !parse_value(fields[5], pass == 2, &immediate2, path, number)))
            return 0;
        words = registers[0] == 2 || registers[1] == 2 || registers[2] == 2 ? 2 : 1;
        if (address > MEM_SIZE - words)
            return diagnostic(path, number, "instruction exceeds memory capacity (%d words)", MEM_SIZE);
        if (pass == 2) {
            uint32_t word = ((uint32_t)opcode << 24) |
                            ((uint32_t)registers[0] << 20) |
                            ((uint32_t)registers[1] << 16) |
                            ((uint32_t)registers[2] << 12) |
                            (immediate1 & UINT32_C(0xFFF));
            store_word(address, word);
            if (words == 2)
                store_word(address + 1, immediate2);
        }
        address += words;
    }
    return status == 0;
}

int main(int argc, char *argv[])
{
    FILE *input, *output;
    int i, success;
    if (argc != 3) {
        fprintf(stderr, "usage: %s program.asm memin.txt\n", argv[0]);
        return 1;
    }
    input = fopen(argv[1], "r");
    if (!input) {
        diagnostic(argv[1], 0, "cannot open input");
        return 1;
    }
    success = assemble_pass(input, argv[1], 1);
    if (success && fseek(input, 0, SEEK_SET) != 0)
        success = diagnostic(argv[1], 0, "cannot rewind input");
    if (success)
        success = assemble_pass(input, argv[1], 2);
    if (fclose(input) != 0)
        success = diagnostic(argv[1], 0, "cannot close input");
    if (!success)
        return 1;

    /* Assembly must succeed completely before opening the destination. */
    output = fopen(argv[2], "w");
    if (!output) {
        diagnostic(argv[2], 0, "cannot open output");
        return 1;
    }
    for (i = 0; i <= max_addr; i++) {
        if (fprintf(output, "%08" PRIX32 "\n", mem[i]) < 0) {
            diagnostic(argv[2], 0, "cannot write output");
            fclose(output);
            return 1;
        }
    }
    if (fclose(output) != 0) {
        diagnostic(argv[2], 0, "cannot close output");
        return 1;
    }
    return 0;
}
