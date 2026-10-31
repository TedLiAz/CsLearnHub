# Makefile for memory-order-examples
# 用法: make          编译所有示例
#       make run      编译并运行所有示例
#       make tsan     用 ThreadSanitizer 编译（检测 data race）
#       make clean    删除编译产物

CC      = gcc
CFLAGS  = -std=c11 -pthread -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
TFLAGS  = -std=c11 -pthread -Wall -Wextra -O1 -fsanitize=thread -g

SRCS    = 01-producer-consumer.c \
          02-spinlock.c          \
          03-double-checked-init.c \
          04-relaxed-pitfall.c

BINS    = $(SRCS:.c=)
TBINS   = $(addprefix tsan-,$(BINS))

.PHONY: all run tsan clean

all: $(BINS)

# 编译规则：每个 .c 生成同名可执行文件
%: %.c
	$(CC) $(CFLAGS) -o $@ $<
	@echo "[编译完成] $@"

# 运行所有示例
run: all
	@echo ""
	@echo "══════════════════════════════════════"
	@echo "01 producer-consumer (release/acquire)"
	@echo "══════════════════════════════════════"
	./01-producer-consumer

	@echo ""
	@echo "══════════════════════════════════════"
	@echo "02 spinlock 对比"
	@echo "══════════════════════════════════════"
	./02-spinlock

	@echo ""
	@echo "══════════════════════════════════════"
	@echo "03 双重检查初始化"
	@echo "══════════════════════════════════════"
	./03-double-checked-init

	@echo ""
	@echo "══════════════════════════════════════"
	@echo "04 relaxed 陷阱演示"
	@echo "══════════════════════════════════════"
	./04-relaxed-pitfall

# ThreadSanitizer 版本
tsan: $(SRCS)
	@echo "[TSan] 编译 01-producer-consumer ..."
	$(CC) $(TFLAGS) -o tsan-01 01-producer-consumer.c
	@echo "[TSan] 编译 04-relaxed-pitfall ..."
	$(CC) $(TFLAGS) -o tsan-04 04-relaxed-pitfall.c
	@echo ""
	@echo "运行 TSan 检测（注意观察 WARNING: data race 输出）:"
	@echo ""
	-./tsan-01 2>&1 | head -40
	@echo ""
	-./tsan-04 2>&1 | head -40

clean:
	rm -f $(BINS) tsan-01 tsan-04
	@echo "[clean] 清理完成"
