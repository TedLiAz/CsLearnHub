/*
 * 02-spinlock.c
 * 自旋锁实现：seq_cst vs relaxed/release/acquire 对比
 *
 * 编译: gcc -std=c11 -pthread -O2 -o 02-spinlock 02-spinlock.c
 * 运行: ./02-spinlock
 *
 * 演示内容:
 *   - 基于 seq_cst 的朴素自旋锁（最安全但性能最差）
 *   - 基于 acquire/release 的优化自旋锁（性能更好，语义正确）
 *   - 使用 relaxed 实现的错误自旋锁（逻辑正确性存疑）
 */

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

#define NUM_THREADS  8
#define INCREMENTS   1000000

/* ─────────────────────────────────────────────
 * 版本 1：seq_cst 自旋锁（最简单，但性能最差）
 *
 * x86-64 上 seq_cst store 需要 MFENCE 或 XCHG，
 * 每次解锁都有全系统内存屏障，代价极高。
 * ───────────────────────────────────────────── */
typedef struct {
    atomic_flag locked; /* ATOMIC_FLAG_INIT 初始为 false（未锁） */
} SpinLockSeqCst;

static void seqcst_lock(SpinLockSeqCst *sl)
{
    /*
     * atomic_flag_test_and_set 默认 seq_cst：
     * - 读旧值 + 设为 true 是原子操作
     * - 若旧值为 true（已被锁），自旋等待
     */
    while (atomic_flag_test_and_set(&sl->locked))
        ; /* 自旋 */
}

static void seqcst_unlock(SpinLockSeqCst *sl)
{
    /* 默认 seq_cst：会生成 MFENCE（x86-64），非常昂贵 */
    atomic_flag_clear(&sl->locked);
}

/* ─────────────────────────────────────────────
 * 版本 2：acquire/release 优化自旋锁（推荐）
 *
 * 语义分析：
 * - lock   = acquire：确保临界区内的操作不会跑到 lock 之前
 * - unlock = release：确保临界区内的写入对下一个持锁者可见
 * - 这与 mutex 的语义完全一致
 *
 * x86-64 上：
 * - test_and_set(acquire) 只需普通 XCHG（相比 seq_cst 无需额外 MFENCE）
 * - clear(release) 只需普通 MOV（无需 MFENCE）
 * ───────────────────────────────────────────── */
typedef struct {
    atomic_flag locked;
} SpinLockAcqRel;

static void acqrel_lock(SpinLockAcqRel *sl)
{
    /*
     * test_and_set with acquire:
     * 临界区内的读写不能被重排到此操作之前
     */
    while (atomic_flag_test_and_set_explicit(&sl->locked, memory_order_acquire))
        ;
}

static void acqrel_unlock(SpinLockAcqRel *sl)
{
    /*
     * clear with release:
     * 临界区内的写入对下一个 lock(acquire) 的线程可见
     * x86-64: 只需 MOV，无 MFENCE
     */
    atomic_flag_clear_explicit(&sl->locked, memory_order_release);
}

/* ─────────────────────────────────────────────
 * 版本 3：❌ 错误的 relaxed 自旋锁
 *
 * 危害：临界区内的写入不能保证对其他线程可见
 * 在 ARM64 上，一个线程解锁后，另一个线程加锁，
 * 仍可能看到临界区内写入的"旧值"。
 * ───────────────────────────────────────────── */
typedef struct {
    atomic_flag locked;
} SpinLockBad;

static void bad_lock(SpinLockBad *sl)
{
    /*
     * ❌ relaxed：不阻止临界区内的操作跑到加锁操作之前
     * 语义上是错误的，即使加锁成功也不保证内存可见性
     */
    while (atomic_flag_test_and_set_explicit(&sl->locked, memory_order_relaxed))
        ;
}

static void bad_unlock(SpinLockBad *sl)
{
    /*
     * ❌ relaxed：不保证临界区内的写入对其他线程可见
     */
    atomic_flag_clear_explicit(&sl->locked, memory_order_relaxed);
}

/* ─────────────────────────────────────────────
 * 测试框架：多线程对共享计数器做原子自增
 * ───────────────────────────────────────────── */

typedef struct {
    void  *lock_ptr;
    void (*lock_fn)(void *);
    void (*unlock_fn)(void *);
    long  *counter;  /* 临界区内保护的普通变量 */
} ThreadArg;

static void *worker(void *varg)
{
    ThreadArg *arg = (ThreadArg *)varg;
    for (int i = 0; i < INCREMENTS; ++i) {
        arg->lock_fn(arg->lock_ptr);
        (*arg->counter)++;   /* 临界区：非原子自增 */
        arg->unlock_fn(arg->lock_ptr);
    }
    return NULL;
}

static double run_test(
    const char *name,
    void *lock,
    void (*lock_fn)(void *),
    void (*unlock_fn)(void *))
{
    pthread_t threads[NUM_THREADS];
    long counter = 0;
    ThreadArg args[NUM_THREADS];

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < NUM_THREADS; ++i) {
        args[i].lock_ptr  = lock;
        args[i].lock_fn   = lock_fn;
        args[i].unlock_fn = unlock_fn;
        args[i].counter   = &counter;
        pthread_create(&threads[i], NULL, worker, &args[i]);
    }
    for (int i = 0; i < NUM_THREADS; ++i)
        pthread_join(threads[i], NULL);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = (t1.tv_sec - t0.tv_sec) * 1000.0
              + (t1.tv_nsec - t0.tv_nsec) / 1e6;

    long expected = (long)NUM_THREADS * INCREMENTS;
    const char *status = (counter == expected) ? "OK  " : "FAIL";
    printf("[%s] %-30s  counter=%ld (期望 %ld)  耗时 %.1f ms\n",
           status, name, counter, expected, ms);
    return ms;
}

int main(void)
{
    printf("自旋锁对比测试 (%d 线程 × %d 次自增)\n\n",
           NUM_THREADS, INCREMENTS);

    SpinLockSeqCst sl1 = { ATOMIC_FLAG_INIT };
    SpinLockAcqRel sl2 = { ATOMIC_FLAG_INIT };
    SpinLockBad    sl3 = { ATOMIC_FLAG_INIT };

    double t_seqcst = run_test("seq_cst",
        &sl1,
        (void (*)(void *))seqcst_lock,
        (void (*)(void *))seqcst_unlock);

    double t_acqrel = run_test("acquire/release (推荐)",
        &sl2,
        (void (*)(void *))acqrel_lock,
        (void (*)(void *))acqrel_unlock);

    run_test("relaxed (错误！结果可能不等于期望)",
        &sl3,
        (void (*)(void *))bad_lock,
        (void (*)(void *))bad_unlock);

    printf("\n性能对比：seq_cst / acquire-release = %.2fx\n",
           t_seqcst / t_acqrel);
    printf("\n说明:\n");
    printf("  - acquire/release 版本在语义上与 seq_cst 等价（对自旋锁场景）\n");
    printf("  - 但在 x86-64 上，release unlock 只需 MOV 而非 MFENCE，更高效\n");
    printf("  - relaxed 版本在 ARM64 上结果错误（counter != 期望值）\n");
    return 0;
}
