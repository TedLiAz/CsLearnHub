#ifndef FLOW_H
#define FLOW_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdalign.h>
#include <assert.h>

#define MAX_FLOWS 1024

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
 * Simulated flow context — allocated on flow open, freed on QSBR quarantine reclaim.
 * In production this corresponds to a TLS session, flow table entry, or TCP socket state.
 */
typedef struct {
    uint32_t flow_id;
    uint64_t byte_count;
} FlowContext;

/*
 * Section 2: Hot-Cold Separated Flow Tables (ADR-0001)
 *
 * Hot Table (Dense Generation Array):
 *   Packed densely so 16 flow generations occupy a single 64B cache line.
 *   Fast-path readers perform acquire loads on `generation` without cache thrashing.
 *
 * Cold Table (FlowColdContext):
 *   Padded and aligned to 64 bytes to eliminate false sharing during slow-path
 *   state transitions and context pointer reclamation.
 */
typedef struct {
    atomic_uint generation;
} FlowGenerationEntry;

static_assert(sizeof(FlowGenerationEntry) == 4,
              "FlowGenerationEntry must be 4 bytes");

typedef struct {
    alignas(64) atomic_uint state;
    uint32_t                rsvd;
    void                   *flow_context;
    uint8_t                 pad[48];
} __attribute__((aligned(64))) FlowColdContext;

static_assert(sizeof(FlowColdContext) == 64,
              "FlowColdContext must occupy exactly one cache line");

/* Unified logical view of a flow slot */
typedef struct {
    alignas(64) atomic_uint generation;
    atomic_uint             state;
    void                   *flow_context;
    uint8_t                 pad[64 - sizeof(atomic_uint) * 2 - sizeof(void *)];
} __attribute__((aligned(64))) FlowControlBlock;

static_assert(sizeof(FlowControlBlock) == 64,
              "FlowControlBlock must occupy exactly one cache line");

/* ──────── Flow Management APIs ──────── */

void flow_subsystem_init(void);

void flow_open(uint32_t flow_id);

uint32_t flow_get_generation(uint32_t flow_id);

FlowState flow_get_state(uint32_t flow_id);

void *flow_get_context(uint32_t flow_id);

FlowControlBlock *flow_get_fcb(uint32_t flow_id);

void flow_advance_generation(uint32_t flow_id);

void flow_set_state(uint32_t flow_id, FlowState state);

void flow_reclaim_context(uint32_t flow_id);

#endif /* FLOW_H */
