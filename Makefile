# Makefile — type "make" to build, "./minios" to run.
# "make examples" also builds the standalone handbook demos.
#
#   src/*.c  --compile-->  build/*.o  --link-->  minios
#

CC      = gcc
CFLAGS  = -std=c11 -O2 -g -Wall -Wextra -D_GNU_SOURCE -Iinclude
LDLIBS  = -lm

SRCS    = $(wildcard src/*.c)
OBJS    = $(patsubst src/%.c,build/%.o,$(SRCS))
TARGET  = minios

# one 'make' builds everything the docs and the demo refer to
all: $(TARGET) examples experiment-bins

# link step: all the .o files become one program
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDLIBS)
	@echo "built ./$(TARGET) — now run:  ./$(TARGET)"

# compile step: one .c file -> one .o file
#   $@ = the .o   $< = the .c
build/%.o: src/%.c include/minios.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

run: $(TARGET)
	./$(TARGET)

# a quick non-interactive check, good for the demo
demo: $(TARGET)
	./$(TARGET) demo.txt

clean:
	rm -rf build $(TARGET)

.PHONY: all run demo clean

# ---- handbook demo programs (Chapters 3 and 4), each is standalone ----
EX_SRCS = $(wildcard examples/*.c)
EX_BINS = $(patsubst examples/%.c,build/ex_%,$(EX_SRCS))

examples: $(EX_BINS)

build/ex_%: examples/%.c | build
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

.PHONY: examples

# ---- experiments (Chapter 4 analysis) ----
# exp4 links against the project's own object files.
EXP_STANDALONE = build/exp1_fork_vs_exec build/exp2_cow build/exp3_zombie_orphan

experiment-bins: $(EXP_STANDALONE) build/exp4_procscan

experiments: $(TARGET) examples experiment-bins
	@echo "now run:  ./experiments/run_experiments.sh"

build/exp%: experiments/exp%.c | build
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

build/exp4_procscan: experiments/exp4_procscan.c build/process.o build/util.o | build
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

.PHONY: experiments experiment-bins
