#ifndef FLOW_H
#define FLOW_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdalign.h>
#include <assert.h>

#define MAX_FLOWS 1024
#define QUARANTINE_RING_SIZE 256

/*
 * Section 1: FlowHandle (Generational Handle Pattern)
 *
 * Invariants:
 *   - Fast-path packets never carry raw pointers; they carry a 64-bit value-type
 *     handle {flow_id, generation} providing O(1) indexed lookup and instant validation.
 *   - `generation` increments monotonically on flow retirement. Stale handles
 *     holding superseded generations are silently dropped, preventing Use-After-Free (UAF).
 */
typedef struct {
    uint32_t flow_id;
    uint32_t generation;
} FlowHandle;

static inline bool flow_handle_equal(FlowHandle a, FlowHandle b) {
    return (a.flow_id == b.flow_id) && (a.generation == b.generation);
}

typedef enum {
    FLOW_STATE_FREE      = 0,
    FLOW_STATE_ACTIVE    = 1,
    FLOW_STATE_DRAINING  = 2,
    FLOW_STATE_QUIESCENT = 3
} FlowState;

/*
 * Simulated flow context — allocated in a contiguous flat array inside FlowTable.
 * Strictly eliminates heap allocations and pointer chasing on the fast path.
 */
typedef struct {
    uint32_t flow_id;
    uint64_t byte_count;
} FlowContext;

/*
 * Section 2: Hot-Cold Separated Flow Layout (ADR-0001, Data-Oriented Design)
 *
 * Hot Table (Dense Generation Array):
 *   Packed densely so 16 flow generations occupy a single 64B cache line.
 *   Fast-path readers perform acquire loads on `generation` without cache thrashing.
 *
 * Cold Table (FlowColdContext):
 *   Padded and aligned to 64 bytes to eliminate false sharing during slow-path
 *   state transitions.
 */
typedef struct {
    atomic_uint generation;
} FlowGenerationEntry;

static_assert(sizeof(FlowGenerationEntry) == 4,
              "FlowGenerationEntry must be 4 bytes");

typedef struct {
    alignas(64) atomic_uint state;
    uint8_t                 pad[64 - sizeof(atomic_uint)];
} __attribute__((aligned(64))) FlowColdContext;

static_assert(sizeof(FlowColdContext) == 64,
              "FlowColdContext must occupy exactly one cache line");

/* Single-threaded FIFO quarantine ring owned strictly by the Terminal/Egress reclaimer */
typedef struct {
    uint32_t flow_id;
    uint32_t generation;
    uint64_t target_epoch;
} QuarantineEntry;

typedef struct {
    QuarantineEntry entries[QUARANTINE_RING_SIZE];
    size_t head;
    size_t tail;
} QuarantineRing;

/* Forward declaration of QSBRModule to decouple flow table from QSBR internals */
struct QSBRModule;

/*
 * Section 3: FlowTable (Unified Data-Oriented Flow Table Instance)
 *
 * Encapsulates the Dense Generation Array, Cold Context Table, Flat Context Pool,
 * and Quarantine Ring into a single cacheline-aligned instance with zero static globals.
 */
typedef struct {
    /* Hot Area: Dense Generation Array (16 flows per 64B Cache Line) */
    alignas(64) FlowGenerationEntry generations[MAX_FLOWS];

    /* Cold Area: State flags cacheline-isolated to prevent false sharing */
    alignas(64) FlowColdContext     cold[MAX_FLOWS];

    /* Flat Context Pool: Pre-allocated contiguous memory, zero malloc */
    alignas(64) FlowContext         contexts[MAX_FLOWS];

    /* Dedicated Single-Threaded Quarantine Pool (Egress thread owned) */
    alignas(64) QuarantineRing      quarantine;
} __attribute__((aligned(64))) FlowTable;

/* ──────── Fast-Path Inlined APIs (Zero Function Call Overhead) ──────── */

static inline bool flow_table_validate(const FlowTable *ft, FlowHandle handle) {
    if (__builtin_expect(handle.flow_id >= MAX_FLOWS, 0)) {
        return false;
    }
    uint32_t cur = atomic_load_explicit(&ft->generations[handle.flow_id].generation,
                                       memory_order_acquire);
    return (cur == handle.generation);
}

static inline FlowContext *flow_table_get_context(FlowTable *ft, uint32_t flow_id) {
    assert(flow_id < MAX_FLOWS);
    return &ft->contexts[flow_id];
}

static inline const FlowContext *flow_table_get_context_const(const FlowTable *ft, uint32_t flow_id) {
    assert(flow_id < MAX_FLOWS);
    return &ft->contexts[flow_id];
}

static inline uint32_t flow_table_get_generation(const FlowTable *ft, uint32_t flow_id) {
    assert(flow_id < MAX_FLOWS);
    return atomic_load_explicit(&ft->generations[flow_id].generation,
                                memory_order_acquire);
}

static inline FlowState flow_table_get_state(const FlowTable *ft, uint32_t flow_id) {
    assert(flow_id < MAX_FLOWS);
    return (FlowState)atomic_load_explicit(&ft->cold[flow_id].state,
                                           memory_order_acquire);
}

/* ──────── Slow-Path Management APIs ──────── */

void flow_table_init(FlowTable *ft);

void flow_table_open(FlowTable *ft, uint32_t flow_id);

void flow_table_set_state(FlowTable *ft, uint32_t flow_id, FlowState state);

void flow_table_advance_generation(FlowTable *ft, uint32_t flow_id);

bool flow_table_retire_to_quarantine(
    FlowTable *ft,
    uint32_t flow_id,
    uint32_t generation,
    uint64_t target_epoch
);

uint64_t flow_table_poll_reclaim(FlowTable *ft, struct QSBRModule *qsbr);

bool flow_table_audit_clean(const FlowTable *ft, uint32_t num_flows);

#endif /* FLOW_H */
