#ifndef QSBR_H
#define QSBR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdalign.h>
#include <assert.h>

/*
 * Section: Lightweight Quiescent-State-Based Reclamation (QSBR)
 *
 * Invariants:
 *   - Terminal advances `global_epoch` to E_target = E_current + 1 when initiating
 *     quarantine for a retired flow slot.
 *   - Reader threads publish their observed global_epoch at quiescent points (burst boundaries).
 *   - A quarantined entry is freed ONLY when every reader thread has published an epoch
 *     >= target_epoch, guaranteeing at least one full quiescent grace period has elapsed.
 */

#define MAX_PIPELINE_THREADS 4
#define QUARANTINE_RING_SIZE 256

typedef struct {
    alignas(64) atomic_uint_fast64_t epoch;
    uint8_t pad[64 - sizeof(atomic_uint_fast64_t)];
} QSBRThreadState;

typedef struct {
    atomic_uint_fast64_t global_epoch;
    uint32_t             num_threads;
    QSBRThreadState      threads[MAX_PIPELINE_THREADS];
} QSBRModule;

typedef struct {
    uint32_t flow_id;
    uint32_t generation;
    uint64_t target_epoch;
} QuarantineEntry;

/* Single-threaded FIFO quarantine ring owned strictly by the Terminal/Egress reclaimer */
typedef struct {
    QuarantineEntry entries[QUARANTINE_RING_SIZE];
    size_t head;
    size_t tail;
} QuarantineRing;

/* ──────── QSBR APIs ──────── */

void qsbr_init(QSBRModule *q, uint32_t num_threads);

void qsbr_quiescent(QSBRModule *q, uint32_t thread_id);

uint64_t qsbr_current_epoch(QSBRModule *q);

uint64_t qsbr_advance_epoch(QSBRModule *q);

bool qsbr_check_all_quiescent(QSBRModule *q, uint64_t target_epoch);

void qsbr_thread_exit(QSBRModule *q, uint32_t thread_id);

/* ──────── Quarantine Ring APIs ──────── */

void quarantine_ring_init(QuarantineRing *r);

bool quarantine_ring_enqueue(QuarantineRing *r, const QuarantineEntry *e);

bool quarantine_ring_peek(QuarantineRing *r, QuarantineEntry *e);

bool quarantine_ring_dequeue(QuarantineRing *r, QuarantineEntry *e);

uint64_t try_reclaim_quarantined_flows(QuarantineRing *ring, QSBRModule *qsbr);

#endif /* QSBR_H */
