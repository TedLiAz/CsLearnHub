#include "qsbr.h"

void qsbr_init(QSBRModule *q, uint32_t num_threads) {
    assert(num_threads <= MAX_PIPELINE_THREADS);
    atomic_init(&q->global_epoch, 1);
    q->num_threads = num_threads;
    for (uint32_t i = 0; i < num_threads; ++i) {
        atomic_init(&q->threads[i].epoch, 0);
    }
}

void qsbr_quiescent(QSBRModule *q, uint32_t thread_id) {
    assert(thread_id < q->num_threads);
    uint64_t ge = atomic_load_explicit(&q->global_epoch, memory_order_relaxed);
    /*
     * Release store: guarantees that all prior memory operations in this reader thread
     * have completed and are globally visible before this quiescent epoch is published.
     */
    atomic_store_explicit(&q->threads[thread_id].epoch, ge, memory_order_release);
}

uint64_t qsbr_current_epoch(const QSBRModule *q) {
    return atomic_load_explicit(&q->global_epoch, memory_order_relaxed);
}

uint64_t qsbr_advance_epoch(QSBRModule *q) {
    /*
     * Advance global epoch and return the target epoch.
     * Any reader must pass a quiescent point at or after this target epoch
     * before quarantine reclamation can proceed.
     */
    return atomic_fetch_add_explicit(&q->global_epoch, 1, memory_order_release) + 1;
}

bool qsbr_check_all_quiescent(const QSBRModule *q, uint64_t target_epoch) {
    for (uint32_t i = 0; i < q->num_threads; ++i) {
        uint64_t te = atomic_load_explicit(&q->threads[i].epoch, memory_order_acquire);
        if (te < target_epoch) {
            return false;
        }
    }
    return true;
}

void qsbr_thread_exit(QSBRModule *q, uint32_t thread_id) {
    assert(thread_id < q->num_threads);
    /*
     * Marking the thread epoch as UINT64_MAX announces permanent quiescence
     * upon thread exit, ensuring the reclaimer never starves or blocks on dead threads.
     */
    atomic_store_explicit(&q->threads[thread_id].epoch, UINT64_MAX, memory_order_release);
}
