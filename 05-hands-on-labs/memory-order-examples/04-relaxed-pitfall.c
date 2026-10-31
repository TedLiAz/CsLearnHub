/*
 * 04-relaxed-pitfall.c
 * relaxed 内存序的陷阱：可见性问题与 IRIW（Independent Reads of Independent Writes）
 *
 * 编译: gcc -std=c11 -pthread -O2 -o 04-pitfall 04-relaxed-pitfall.c
 * 运行: ./04-pitfall
 *
 * 演示内容:
 *   - 陷阱 1: relaxed flag 导致数据不可见（最常见错误）
 *   - 陷阱 2: IRIW 问题——两个读线程看到写操作的顺序不一致
 *   - 陷阱 3: seq_cst 与 release 混用导致全序假设失效
 *   - ThreadSanitizer 预期输出说明
 */

#define _POSIX_C_SOURCE 200809L
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

/* ═════════════════════════════════════════════
 * 陷阱 1: relaxed flag 导致数据不可见
 *
 * 场景：生产者写数据后通过 relaxed flag 通知消费者
 *       消费者在 ARM64 上可能读到旧数据
 *
 * 预期 ThreadSanitizer 输出：
 *   WARNING: ThreadSanitizer: data race
 *   Write of size 4 at ... by thread T1:
 *     #0 trap1_producer ...
 *   Previous read of size 4 at ... by thread T2:
 *     #0 trap1_consumer ...
 * ═════════════════════════════════════════════ */

static int        g_t1_data  = 0;
static atomic_int g_t1_flag  = ATOMIC_VAR_INIT(0);
static int        g_t1_result = -1;
static int        g_t1_error  = 0;

static void *trap1_producer(void *arg)
{
    g_t1_data = 100;                 /* 普通写 */
    atomic_store_explicit(&g_t1_flag, 1, memory_order_relaxed);  /* ❌ relaxed */
    return NULL;
}

static void *trap1_consumer(void *arg)
{
    while (atomic_load_explicit(&g_t1_flag, memory_order_relaxed) == 0)  /* ❌ relaxed */
        ;
    g_t1_result = g_t1_data;   /* 可能读到 0 而非 100！*/
    if (g_t1_result != 100) {
        g_t1_error = 1;
    }
    return NULL;
}

static void run_trap1(void)
{
    printf("── 陷阱 1: relaxed 传递数据 ──────────────────\n");
    printf("问题：relaxed store/load 不建立 synchronizes-with，\n");
    printf("      编译器/CPU 可以将 flag=1 重排到 data=100 之前。\n");
    printf("危害：ARM64 上消费者可能读到 data==0（旧值）。\n");
    printf("      x86-64 上编译器重排仍可能导致问题。\n\n");

    g_t1_data = 0; g_t1_error = 0;
    atomic_store(&g_t1_flag, 0);

    pthread_t p, c;
    pthread_create(&p, NULL, trap1_producer, NULL);
    pthread_create(&c, NULL, trap1_consumer, NULL);
    pthread_join(p, NULL);
    pthread_join(c, NULL);

    printf("结果: data = %d（期望 100）%s\n",
           g_t1_result,
           g_t1_error ? "  ← 出错了！（ARM64 概率更高）" : "  （x86-64 TSO 此次未触发，但 TSan 仍会报告竞争）");

    printf("\n正确修复: \n");
    printf("  producer: memory_order_release\n");
    printf("  consumer: memory_order_acquire\n\n");
}

/* ═════════════════════════════════════════════
 * 陷阱 2: IRIW (Independent Reads of Independent Writes)
 *
 * 经典的四线程问题，展示弱内存模型下的可见性悖论：
 *
 * 线程 W1: x = 1 (release)
 * 线程 W2: y = 1 (release)
 * 线程 R1: r1 = x (acquire); r2 = y (acquire)
 * 线程 R2: r3 = y (acquire); r4 = x (acquire)
 *
 * 在弱内存模型（ARM64）上，可能出现：
 *   R1 看到 x=1, y=0
 *   R2 看到 y=1, x=0
 * 这在直觉上是不可能的——但实际上是合法的！
 *
 * 用 seq_cst 则能保证全序：R1 和 R2 看到相同的写入顺序。
 * ═════════════════════════════════════════════ */

static atomic_int g_x = ATOMIC_VAR_INIT(0);
static atomic_int g_y = ATOMIC_VAR_INIT(0);

typedef struct { int r1, r2, r3, r4; } IRIWResult;

static volatile int g_iriw_r1, g_iriw_r2, g_iriw_r3, g_iriw_r4;
static pthread_barrier_t g_barrier;

static void *iriw_w1(void *arg) {
    pthread_barrier_wait(&g_barrier);
    atomic_store_explicit(&g_x, 1, memory_order_release);
    return NULL;
}
static void *iriw_w2(void *arg) {
    pthread_barrier_wait(&g_barrier);
    atomic_store_explicit(&g_y, 1, memory_order_release);
    return NULL;
}
static void *iriw_r1(void *arg) {
    pthread_barrier_wait(&g_barrier);
    g_iriw_r1 = atomic_load_explicit(&g_x, memory_order_acquire);
    g_iriw_r2 = atomic_load_explicit(&g_y, memory_order_acquire);
    return NULL;
}
static void *iriw_r2(void *arg) {
    pthread_barrier_wait(&g_barrier);
    g_iriw_r3 = atomic_load_explicit(&g_y, memory_order_acquire);
    g_iriw_r4 = atomic_load_explicit(&g_x, memory_order_acquire);
    return NULL;
}

static void run_trap2(void)
{
    printf("── 陷阱 2: IRIW（独立写的独立读）─────────────\n");
    printf("问题：在 ARM64 弱内存模型下，两个读线程可能看到\n");
    printf("      写操作的不同顺序，即使使用 acquire/release。\n");
    printf("      这需要 seq_cst 才能保证全序一致性。\n\n");

    int conflict_count = 0;
    const int TRIALS = 100000;

    for (int t = 0; t < TRIALS; ++t) {
        atomic_store(&g_x, 0);
        atomic_store(&g_y, 0);
        pthread_barrier_init(&g_barrier, NULL, 4);

        pthread_t w1, w2, r1, r2;
        pthread_create(&w1, NULL, iriw_w1, NULL);
        pthread_create(&w2, NULL, iriw_w2, NULL);
        pthread_create(&r1, NULL, iriw_r1, NULL);
        pthread_create(&r2, NULL, iriw_r2, NULL);
        pthread_join(w1, NULL);
        pthread_join(w2, NULL);
        pthread_join(r1, NULL);
        pthread_join(r2, NULL);

        pthread_barrier_destroy(&g_barrier);

        /*
         * 检测"悖论"结果：
         * R1 看到 x=1, y=0 且 R2 看到 y=1, x=0
         * 即两个读线程看到完全相反的写入顺序
         */
        if (g_iriw_r1 == 1 && g_iriw_r2 == 0 &&
            g_iriw_r3 == 1 && g_iriw_r4 == 0) {
            conflict_count++;
        }
    }

    printf("测试 %d 次，发现 %d 次 IRIW 悖论结果\n", TRIALS, conflict_count);
    printf("（在 ARM64 上此数值 > 0，在 x86-64 上通常为 0，因为 TSO 保证了全序）\n\n");

    printf("修复方法：将所有 store 改为 seq_cst，或将所有 load 改为 seq_cst\n");
    printf("  atomic_store_explicit(&x, 1, memory_order_seq_cst);\n");
    printf("  atomic_store_explicit(&y, 1, memory_order_seq_cst);\n\n");
}

/* ═════════════════════════════════════════════
 * 陷阱 3: seq_cst 与 release 混用
 *
 * 即使混用中"更强的"那个用了 seq_cst，全序保证也会失效。
 * 原因：seq_cst 全序只在所有操作都是 seq_cst 时才成立。
 * ═════════════════════════════════════════════ */

static void run_trap3(void)
{
    printf("── 陷阱 3: seq_cst 与 release/acquire 混用 ──\n");
    printf("症状：你以为用了 seq_cst 就有全序，但如果某些操作用了\n");
    printf("      release/acquire，全序保证会被打破。\n\n");
    printf("反例：\n");
    printf("  // 线程 A\n");
    printf("  atomic_store_explicit(&x, 1, memory_order_release); // 不是 seq_cst!\n");
    printf("  int ry = atomic_load(&y);  // seq_cst load\n\n");
    printf("  // 线程 B\n");
    printf("  atomic_store_explicit(&y, 1, memory_order_release); // 不是 seq_cst!\n");
    printf("  int rx = atomic_load(&x);  // seq_cst load\n\n");
    printf("结果：可能出现 rx==0 && ry==0（即使 load 是 seq_cst）\n");
    printf("      因为 release store 不在 seq_cst 全序中\n\n");
    printf("修复：要么全用 seq_cst，要么接受 acquire/release 的弱序语义。\n");
    printf("      不要混用，这是最隐蔽的 bug 来源之一。\n\n");
}

/* ─────────────────────────────────────────────
 * ThreadSanitizer 使用说明
 * ───────────────────────────────────────────── */
static void print_tsan_guide(void)
{
    printf("══════════════════════════════════════════════\n");
    printf("ThreadSanitizer (TSan) 使用指南\n");
    printf("══════════════════════════════════════════════\n\n");
    printf("编译命令（检测陷阱 1 的 data race）：\n");
    printf("  gcc -std=c11 -pthread -fsanitize=thread -O1 \\\n");
    printf("      -o 04-tsan 04-relaxed-pitfall.c && ./04-tsan\n\n");
    printf("预期 TSan 输出（陷阱 1）：\n");
    printf("  WARNING: ThreadSanitizer: data race\n");
    printf("  Write of size 4 at 0x... by thread T1:\n");
    printf("    #0 trap1_producer 04-relaxed-pitfall.c:XX\n");
    printf("  Previous read of size 4 at 0x... by thread T2:\n");
    printf("    #0 trap1_consumer 04-relaxed-pitfall.c:XX\n\n");
    printf("注意：TSan 在 x86-64 上也能检测到 relaxed 引起的 data race，\n");
    printf("      即使在 x86-64 上运行时未发生实际错误（因为 TSO 保护）。\n");
    printf("      这正是 TSan 的价值：在 x86 上提前发现 ARM64 上会出现的 bug。\n\n");
}

int main(void)
{
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║    relaxed 内存序陷阱演示                    ║\n");
    printf("╚══════════════════════════════════════════════╝\n\n");

    run_trap1();
    run_trap2();
    run_trap3();
    print_tsan_guide();

    return 0;
}
