CC ?= cc
PYTHON ?= python3
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
LDFLAGS ?=
SANITIZER_FLAGS = -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror -fno-omit-frame-pointer -fsanitize=address,undefined

.PHONY: all test test-sanitize clean

all: build/assembler build/simulator

build/assembler: assembler/assembler/assembler.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

build/simulator: simulator/simulator/simulator.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

test: all
	$(PYTHON) tests/run_tests.py --assembler build/assembler --simulator build/simulator

build/sanitize/assembler: assembler/assembler/assembler.c
	mkdir -p $(@D)
	$(CC) $(SANITIZER_FLAGS) $< $(LDFLAGS) -o $@

build/sanitize/simulator: simulator/simulator/simulator.c
	mkdir -p $(@D)
	$(CC) $(SANITIZER_FLAGS) $< $(LDFLAGS) -o $@

test-sanitize: build/sanitize/assembler build/sanitize/simulator
	$(PYTHON) tests/run_tests.py --assembler build/sanitize/assembler --simulator build/sanitize/simulator

clean:
	rm -rf build
