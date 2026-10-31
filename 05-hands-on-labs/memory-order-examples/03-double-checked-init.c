/*
 * 03-double-checked-init.c
 * 双重检查初始化（Double-Checked Locking, DCL）
 *
 * 编译: gcc -std=c11 -pthread -O2 -o 03-dci 03-double-checked-init.c
 * 运行: ./03-dci
 *
 * 演示内容:
 *   - 错误版本：instance 用普通指针，存在 data race
 *   - 正确版本：instance 用 atomic + acquire/release，安全且高效
 *   - 为什么 seq_cst 版本虽然正确但不必要地昂贵
 */

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>

/* ─────────────────────────────────────────────
 * 共享数据结构（模拟"昂贵"的初始化）
 * ───────────────────────────────────────────── */
#define MAGIC_MARKER  0xDEADBEEF

typedef struct {
    int   value;
    char  name[32];
    int   marker;   /* 用于检测"半初始化"：若不等于 MAGIC_MARKER 则说明初始化未完成 */
} Singleton;

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

/* ─────────────────────────────────────────────
 * 版本 1：❌ 错误的 DCL（非原子指针）
 *
 * 问题：instance 不是原子变量
 * - 其他线程对 instance 的读取是 data race（C11 UB）
 * - CPU 可能将 `instance = tmp` 重排到 tmp 字段初始化之前
 *   => 其他线程看到 instance != NULL 但内容是垃圾
 * ───────────────────────────────────────────── */
static Singleton *g_bad_instance = NULL;  /* 普通指针，存在竞争 */

static Singleton *bad_get_instance(void)
{
    /*
     * ❌ 这一行是 data race！
     * 其他线程可能同时在 mutex 内写 g_bad_instance，
     * 这里是无锁读取 => 未定义行为
     */
    if (g_bad_instance == NULL) {
        pthread_mutex_lock(&g_mu);
        if (g_bad_instance == NULL) {
            Singleton *tmp = malloc(sizeof(Singleton));
            tmp->value  = 42;
            strncpy(tmp->name, "singleton", sizeof(tmp->name));
            tmp->marker = MAGIC_MARKER;

            /*
             * ❌ 关键问题：CPU 可能将此赋值重排到上面的字段初始化之前！
             * 其他线程在第一个 if 处读到 g_bad_instance != NULL，
             * 然后访问 tmp->marker，但 marker 还没被写入！
             *
             * ARM64 上发生概率很高，x86-64 上概率低但编译器重排仍可能发生。
             */
            g_bad_instance = tmp;  /* 非原子写，可被重排 */
        }
        pthread_mutex_unlock(&g_mu);
    }
    return g_bad_instance;  /* 非原子读，可能看到半初始化对象 */
}

/* ─────────────────────────────────────────────
 * 版本 2：✅ 正确的 DCL（atomic + acquire/release）
 *
 * 语义分析：
 * - 锁外的 acquire load：如果读到非 NULL，保证看到完整初始化的对象
 *   （因为写入时用了 release store，建立 synchronizes-with）
 * - 锁内的 relaxed load：已在 mutex 保护下，mutex 自带内存屏障
 * - 初始化完成后的 release store：将完整对象"发布"给其他线程
 * ───────────────────────────────────────────── */
static atomic_uintptr_t g_good_instance = ATOMIC_VAR_INIT(0);

static Singleton *good_get_instance(void)
{
    /*
     * 快路径（hot path）：acquire load
     * 若非 NULL，可安全使用（happens-before 保证初始化已完成）
     * x86-64: 只需 MOV（无额外硬件指令）
     * ARM64:  LDAR 指令
     */
    Singleton *p = (Singleton *)atomic_load_explicit(
        &g_good_instance, memory_order_acquire);

    if (p == NULL) {
        /* 慢路径：需要初始化 */
        pthread_mutex_lock(&g_mu);

        /*
         * 双重检查（锁内）：
         * 锁内用 relaxed 即可，mutex unlock/lock 自带完整内存屏障
         */
        p = (Singleton *)atomic_load_explicit(
            &g_good_instance, memory_order_relaxed);

        if (p == NULL) {
            Singleton *tmp = malloc(sizeof(Singleton));

            /* 先完成所有初始化 */
            tmp->value  = 42;
            strncpy(tmp->name, "singleton", sizeof(tmp->name));
            tmp->marker = MAGIC_MARKER;  /* 最后写 marker */

            /*
             * release store："发布"完整初始化的对象
             * - 确保上面所有字段写入对"acquire load 读到非 NULL"的线程可见
             * - 从此刻起，其他线程的 acquire load 可安全读取
             */
            atomic_store_explicit(
                &g_good_instance, (uintptr_t)tmp, memory_order_release);
            p = tmp;
        }
        pthread_mutex_unlock(&g_mu);
    }
    return p;
}

/* ─────────────────────────────────────────────
 * 版本 3：✅ seq_cst 版本（正确但过于昂贵）
 *
 * 在初始化场景中，慢路径本来就不频繁，
 * 使用 seq_cst 虽然正确但造成不必要的热路径开销。
 * ───────────────────────────────────────────── */
static atomic_uintptr_t g_seqcst_instance = ATOMIC_VAR_INIT(0);

static Singleton *seqcst_get_instance(void)
{
    /* seq_cst load：x86-64 上比 acquire 多一道编译器约束（功能相同但语义更强） */
    Singleton *p = (Singleton *)atomic_load(&g_seqcst_instance);

    if (p == NULL) {
        pthread_mutex_lock(&g_mu);
        p = (Singleton *)atomic_load(&g_seqcst_instance);

        if (p == NULL) {
            Singleton *tmp = malloc(sizeof(Singleton));
            tmp->value  = 42;
            strncpy(tmp->name, "singleton", sizeof(tmp->name));
            tmp->marker = MAGIC_MARKER;

            /* seq_cst store：x86-64 上 XCHG 或 MOV+MFENCE，不必要地昂贵 */
            atomic_store(&g_seqcst_instance, (uintptr_t)tmp);
            p = tmp;
        }
        pthread_mutex_unlock(&g_mu);
    }
    return p;
}

/* ─────────────────────────────────────────────
 * 测试：多线程并发调用，验证只初始化一次且内容正确
 * ───────────────────────────────────────────── */
#define TEST_THREADS 16
#define TEST_CALLS   100000

typedef struct {
    Singleton *(*get_fn)(void);
    int         errors;
    const char *name;
} TestArg;

static void *test_worker(void *varg)
{
    TestArg *arg = (TestArg *)varg;
    int errors = 0;
    for (int i = 0; i < TEST_CALLS; ++i) {
        Singleton *s = arg->get_fn();
        if (s == NULL || s->value != 42 || s->marker != (int)MAGIC_MARKER) {
            errors++;
        }
    }
    arg->errors = errors;
    return NULL;
}

static void run_test(const char *name, Singleton *(*get_fn)(void))
{
    pthread_t threads[TEST_THREADS];
    TestArg   args[TEST_THREADS];

    for (int i = 0; i < TEST_THREADS; ++i) {
        args[i].get_fn = get_fn;
        args[i].errors = 0;
        args[i].name   = name;
        pthread_create(&threads[i], NULL, test_worker, &args[i]);
    }

    int total_errors = 0;
    for (int i = 0; i < TEST_THREADS; ++i) {
        pthread_join(threads[i], NULL);
        total_errors += args[i].errors;
    }

    printf("[%s] %-30s  错误次数: %d / %d\n",
           total_errors == 0 ? "OK  " : "FAIL",
           name,
           total_errors,
           TEST_THREADS * TEST_CALLS);
}

int main(void)
{
    printf("双重检查初始化（DCL）测试 (%d 线程)\n\n", TEST_THREADS);

    printf("注意: 错误版本 (bad_get_instance) 存在 data race，\n");
    printf("      在 x86-64 上可能碰巧不出错，但属于未定义行为。\n");
    printf("      使用 -fsanitize=thread 编译可检测到竞争。\n\n");

    /* 注意：三个版本各用独立的全局实例，避免相互干扰 */
    run_test("acquire/release (推荐)", good_get_instance);
    run_test("seq_cst (正确但较慢)",   seqcst_get_instance);

    printf("\n内存序选择说明:\n");
    printf("  - acquire load (热路径) + release store (初始化完成时) = 最优\n");
    printf("  - seq_cst 在此场景过于昂贵（初始化只发生一次，但热路径每次都执行）\n");
    printf("  - 两个版本在正确性上等价，acquire/release 在热路径性能更好\n");
    return 0;
}
