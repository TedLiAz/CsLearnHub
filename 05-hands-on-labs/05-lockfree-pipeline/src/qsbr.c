#include "qsbr.h"
#include "flow.h"

void qsbr_init(QSBRModule *q, uint32_t num_threads) {
    assert(num_threads <= MAX_PIPELINE_THREADS);
    atomic_init(&q->global_epoch, 1);
    q->num_threads = num_threads;
    for (uint32_t i = 0; i < num_threads; ++i)
        atomic_init(&q->threads[i].epoch, 0);
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

uint64_t qsbr_current_epoch(QSBRModule *q) {
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

bool qsbr_check_all_quiescent(QSBRModule *q, uint64_t target_epoch) {
    for (uint32_t i = 0; i < q->num_threads; ++i) {
        uint64_t te = atomic_load_explicit(&q->threads[i].epoch, memory_order_acquire);
        if (te < target_epoch)
            return false;
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

/* ──────── Quarantine Ring Implementation ──────── */

void quarantine_ring_init(QuarantineRing *r) {
    r->head = 0;
    r->tail = 0;
}

bool quarantine_ring_enqueue(QuarantineRing *r, const QuarantineEntry *e) {
    if (r->head - r->tail >= QUARANTINE_RING_SIZE) return false;
    r->entries[r->head % QUARANTINE_RING_SIZE] = *e;
    r->head++;
    return true;
}

bool quarantine_ring_peek(QuarantineRing *r, QuarantineEntry *e) {
    if (r->head == r->tail) return false;
    *e = r->entries[r->tail % QUARANTINE_RING_SIZE];
    return true;
}

bool quarantine_ring_dequeue(QuarantineRing *r, QuarantineEntry *e) {
    if (r->head == r->tail) return false;
    *e = r->entries[r->tail % QUARANTINE_RING_SIZE];
    r->tail++;
    return true;
}

uint64_t try_reclaim_quarantined_flows(QuarantineRing *ring, QSBRModule *qsbr) {
    uint64_t reclaimed = 0;
    QuarantineEntry entry;

    /*
     * FIFO epoch ordering: entries are enqueued in non-decreasing target epoch snapshots.
     * If the head-of-queue entry has not cleared its target epoch barrier, none of
     * the subsequent entries can have cleared it either, allowing early exit.
     */
    while (quarantine_ring_peek(ring, &entry)) {
        if (qsbr_check_all_quiescent(qsbr, entry.target_epoch)) {
            quarantine_ring_dequeue(ring, &entry);
            flow_reclaim_context(entry.flow_id);
            reclaimed++;
        } else {
            break;
        }
    }
    return reclaimed;
}
