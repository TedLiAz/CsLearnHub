/*
 * 01-producer-consumer.c
 * 经典 acquire/release 配对示例
 *
 * 编译: gcc -std=c11 -pthread -O2 -o 01-producer-consumer 01-producer-consumer.c
 * 运行: ./01-producer-consumer
 *
 * 演示内容:
 *   - 正确版本: release/acquire 配对，保证数据可见性
 *   - 错误版本: relaxed/relaxed，存在数据竞争（用 TSan 检测）
 */

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <unistd.h>

/* ─────────────────────────────────────────────
 * 共享状态
 * ───────────────────────────────────────────── */
#define ITERATIONS 1000000

static int          g_data   = 0;           /* 普通变量（受 flag 保护） */
static atomic_int   g_flag   = ATOMIC_VAR_INIT(0);
static atomic_uint  g_errors = ATOMIC_VAR_INIT(0);  /* 统计错误次数 */

/* ─────────────────────────────────────────────
 * 正确版本：release / acquire 配对
 * ───────────────────────────────────────────── */
static void *correct_producer(void *arg)
{
    for (int i = 1; i <= ITERATIONS; ++i) {
        /* 等待消费者重置 flag 为 0 */
        while (atomic_load_explicit(&g_flag, memory_order_acquire) != 0)
            ;

        g_data = i;   /* 普通写，受 release store 保护 */

        /*
         * release store:
         *   - 确保 g_data = i 对做 acquire load 的消费者可见
         *   - 阻止编译器/CPU 将此 store 重排到 g_data = i 之前
         */
        atomic_store_explicit(&g_flag, 1, memory_order_release);
    }
    return NULL;
}

static void *correct_consumer(void *arg)
{
    for (int i = 1; i <= ITERATIONS; ++i) {
        /* 等待 flag == 1 */
        while (atomic_load_explicit(&g_flag, memory_order_acquire) != 1)
            ;

        /*
         * acquire load 与 producer 的 release store synchronizes-with，
         * 保证此时读到的 g_data == i（happens-before 保证）
         */
        if (g_data != i) {
            atomic_fetch_add_explicit(&g_errors, 1, memory_order_relaxed);
        }

        atomic_store_explicit(&g_flag, 0, memory_order_release);
    }
    return NULL;
}

/* ─────────────────────────────────────────────
 * 错误版本：relaxed / relaxed
 *
 * 在 x86-64 上可能不报错（TSO 保护了 StoreStore），
 * 但在 ARM64 上高概率出错。
 * 使用 ThreadSanitizer (-fsanitize=thread) 可在 x86 上检测到 data race。
 * ───────────────────────────────────────────── */
static int          g_bad_data = 0;
static atomic_int   g_bad_flag = ATOMIC_VAR_INIT(0);

static void *bad_producer(void *arg)
{
    for (int i = 1; i <= ITERATIONS; ++i) {
        while (atomic_load_explicit(&g_bad_flag, memory_order_relaxed) != 0)
            ;

        g_bad_data = i;

        /*
         * ❌ 错误：memory_order_relaxed 不提供任何顺序约束
         * 编译器 / CPU 可以将此 store 重排到 g_bad_data = i 之前！
         */
        atomic_store_explicit(&g_bad_flag, 1, memory_order_relaxed);
    }
    return NULL;
}

static void *bad_consumer(void *arg)
{
    int errors = 0;
    for (int i = 1; i <= ITERATIONS; ++i) {
        /*
         * ❌ 错误：relaxed load 不与任何 release 建立 synchronizes-with
         * 即使 flag==1，也不保证能看到最新的 g_bad_data 值
         */
        while (atomic_load_explicit(&g_bad_flag, memory_order_relaxed) != 1)
            ;

        /* 在 ARM64 上，此处可能读到旧值（data race，未定义行为） */
        if (g_bad_data != i) {
            errors++;
        }

        atomic_store_explicit(&g_bad_flag, 0, memory_order_relaxed);
    }
    if (errors > 0) {
        printf("[BAD]  检测到 %d 次数据不一致（在 ARM64 上可见）\n", errors);
    }
    return NULL;
}

/* ─────────────────────────────────────────────
 * main
 * ───────────────────────────────────────────── */
int main(void)
{
    pthread_t p, c;

    printf("=== 正确版本 (release/acquire) ===\n");
    g_flag = ATOMIC_VAR_INIT(0);
    g_data = 0;
    atomic_store(&g_errors, 0);

    pthread_create(&p, NULL, correct_producer, NULL);
    pthread_create(&c, NULL, correct_consumer, NULL);
    pthread_join(p, NULL);
    pthread_join(c, NULL);

    unsigned errs = atomic_load(&g_errors);
    if (errs == 0) {
        printf("[OK]   %d 轮迭代全部正确，无数据不一致\n\n", ITERATIONS);
    } else {
        printf("[FAIL] 发现 %u 次错误（不应该发生！）\n\n", errs);
    }

    printf("=== 错误版本 (relaxed/relaxed) ===\n");
    printf("注意: 在 x86-64 上可能不报错，但存在 data race（用 TSan 检测）\n");
    printf("      在 ARM64 上高概率出现数据不一致\n");
    g_bad_flag = ATOMIC_VAR_INIT(0);
    g_bad_data = 0;

    pthread_create(&p, NULL, bad_producer, NULL);
    pthread_create(&c, NULL, bad_consumer, NULL);
    pthread_join(p, NULL);
    pthread_join(c, NULL);

    printf("[完成] 错误版本测试结束\n");
    printf("\n提示: 使用以下命令检测 data race:\n");
    printf("  gcc -std=c11 -pthread -fsanitize=thread -O1 \\\n");
    printf("      -o 01-tsan 01-producer-consumer.c && ./01-tsan\n");
    return 0;
}
