# Makefile for Modular Lock-Free Pipeline Lifecycle Safety Demo & Benchmark
#
# Modular Layout:
#   include/  - Public headers (flow.h, message.h, ring_buffer.h, qsbr.h, pipeline.h)
#   src/      - Implementations (main.c, flow.c, ring_buffer.c, qsbr.c, pipeline.c)
#
# Targets:
#   all     : Compile optimized release binary (-O3 -march=native)
#   run     : Execute standard run (2,000,000 packets, 16 flows)
#   tsan    : Compile with ThreadSanitizer & verify zero data races
#   bench   : Run external Python 3 + Linux perf benchmark suite
#   clean   : Remove compiled binaries

CC ?= gcc
CFLAGS ?= -std=c11 -pthread -O3 -march=native -Wall -Wextra -Werror -Iinclude
TSAN_FLAGS ?= -std=c11 -pthread -O1 -g -fsanitize=thread -Wall -Wextra -Werror -Iinclude

TARGET = lockfree_pipeline
TSAN_TARGET = lockfree_pipeline_tsan

SRCS = src/main.c src/flow.c src/ring_buffer.c src/qsbr.c src/pipeline.c
HEADERS = include/flow.h include/message.h include/ring_buffer.h include/qsbr.h include/pipeline.h

.PHONY: all run tsan bench clean help

all: $(TARGET)

$(TARGET): $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

$(TSAN_TARGET): $(SRCS) $(HEADERS)
	$(CC) $(TSAN_FLAGS) -o $@ $(SRCS)

run: $(TARGET)
	./$(TARGET) 2000000 16

tsan: $(TSAN_TARGET)
	@echo "══════════════════════════════════════════════════════════════"
	@echo " Running under ThreadSanitizer (TSan Data Race Detection)      "
	@echo "══════════════════════════════════════════════════════════════"
	./$(TSAN_TARGET) 200000 8
	@printf "\n[✓] ThreadSanitizer passed: 0 data races detected.\n\n"

bench: $(TARGET)
	python3 benchmark.py --packets 2000000 --flows 16 --runs 5 --warmup 1

clean:
	rm -f $(TARGET) $(TSAN_TARGET)
