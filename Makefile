CC      = gcc
CFLAGS  = -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -D_GNU_SOURCE -pthread -Iinclude
LDLIBS  = -lm -pthread

SRCS = $(wildcard src/*.c)
OBJS = $(patsubst src/%.c,build/%.o,$(SRCS))

TEST_OBJS = $(filter-out build/main.o,$(OBJS))

EXAMPLES = $(patsubst examples/%.c,bin/%,$(wildcard examples/*.c))

all: bin/minios bin/minios_test $(EXAMPLES)

bin/minios: $(OBJS) | bin
	$(CC) $(OBJS) -o $@ $(LDLIBS)

bin/minios_test: tests/minios_test.c $(TEST_OBJS) | bin
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

bin/%: examples/%.c | bin
	$(CC) -std=c11 -O0 -Wall -Wextra -D_GNU_SOURCE -pthread $< -o $@ -lm

build/%.o: src/%.c include/minios.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build bin:
	mkdir -p $@

test: all
	./tests/run_all.sh

tsan:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CFLAGS) -fsanitize=thread -O1" LDLIBS="$(LDLIBS) -fsanitize=thread" bin/minios_test
	TSAN_OPTIONS="suppressions=$(CURDIR)/tests/tsan.supp" ./bin/minios_test --stress
	$(MAKE) clean

clean:
	rm -rf build bin

.PHONY: all test tsan clean
