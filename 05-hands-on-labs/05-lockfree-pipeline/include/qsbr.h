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
 *   - Reclaimer advances `global_epoch` to E_target = E_current + 1 when initiating
 *     quarantine for a retired resource slot.
 *   - Reader threads publish their observed global_epoch at quiescent points (burst boundaries).
 *   - A quarantined entry is reclaimed ONLY when every reader thread has published an epoch
 *     >= target_epoch, guaranteeing at least one full quiescent grace period has elapsed.
 */

#define MAX_PIPELINE_THREADS 4

typedef struct {
    alignas(64) atomic_uint_fast64_t epoch;
    uint8_t pad[64 - sizeof(atomic_uint_fast64_t)];
} QSBRThreadState;

typedef struct QSBRModule {
    atomic_uint_fast64_t global_epoch;
    uint32_t             num_threads;
    QSBRThreadState      threads[MAX_PIPELINE_THREADS];
} QSBRModule;

/* ──────── QSBR Pure Epoch APIs ──────── */

void qsbr_init(QSBRModule *q, uint32_t num_threads);

void qsbr_quiescent(QSBRModule *q, uint32_t thread_id);

uint64_t qsbr_current_epoch(const QSBRModule *q);

uint64_t qsbr_advance_epoch(QSBRModule *q);

bool qsbr_check_all_quiescent(const QSBRModule *q, uint64_t target_epoch);

void qsbr_thread_exit(QSBRModule *q, uint32_t thread_id);

#endif /* QSBR_H */
