#include "flow.h"
#include "qsbr.h"
#include <string.h>

void flow_table_init(FlowTable *ft) {
    assert(ft != NULL);
    memset(ft, 0, sizeof(*ft));

    for (uint32_t i = 0; i < MAX_FLOWS; ++i) {
        atomic_init(&ft->generations[i].generation, 1);
        atomic_init(&ft->cold[i].state, FLOW_STATE_FREE);
        ft->contexts[i].flow_id = i;
        ft->contexts[i].byte_count = 0;
    }

    ft->quarantine.head = 0;
    ft->quarantine.tail = 0;
}

void flow_table_open(FlowTable *ft, uint32_t flow_id) {
    assert(ft != NULL);
    assert(flow_id < MAX_FLOWS);

    /* Data-Oriented: Contiguous flat array assignment, zero malloc overhead */
    ft->contexts[flow_id].flow_id = flow_id;
    ft->contexts[flow_id].byte_count = 0;

    atomic_store_explicit(&ft->cold[flow_id].state, FLOW_STATE_ACTIVE, memory_order_release);
}

void flow_table_set_state(FlowTable *ft, uint32_t flow_id, FlowState state) {
    assert(ft != NULL);
    assert(flow_id < MAX_FLOWS);
    atomic_store_explicit(&ft->cold[flow_id].state, (uint32_t)state, memory_order_release);
}

void flow_table_advance_generation(FlowTable *ft, uint32_t flow_id) {
    assert(ft != NULL);
    assert(flow_id < MAX_FLOWS);
    /*
     * Release store: publishes that all prior packet processing in the pipeline
     * for this flow generation has completed before the generation increments.
     * Single atomic RMW avoids cache line bouncing on multiple redundant tables.
     */
    atomic_fetch_add_explicit(&ft->generations[flow_id].generation, 1, memory_order_release);
}

bool flow_table_retire_to_quarantine(
    FlowTable *ft,
    uint32_t flow_id,
    uint32_t generation,
    uint64_t target_epoch
) {
    assert(ft != NULL);
    assert(flow_id < MAX_FLOWS);

    /*
     * Enforce strict retirement sequence:
     * 1. Monotonically increment generation, instantly invalidating stale handles.
     * 2. Transition state to QUIESCENT.
     * 3. Record target epoch barrier into quarantine pool.
     */
    flow_table_advance_generation(ft, flow_id);
    flow_table_set_state(ft, flow_id, FLOW_STATE_QUIESCENT);

    if (ft->quarantine.head - ft->quarantine.tail >= QUARANTINE_RING_SIZE) {
        return false;
    }

    QuarantineEntry *entry = &ft->quarantine.entries[ft->quarantine.head % QUARANTINE_RING_SIZE];
    entry->flow_id      = flow_id;
    entry->generation   = generation;
    entry->target_epoch = target_epoch;
    ft->quarantine.head++;

    return true;
}

uint64_t flow_table_poll_reclaim(FlowTable *ft, struct QSBRModule *qsbr) {
    assert(ft != NULL);
    assert(qsbr != NULL);

    uint64_t reclaimed = 0;

    /*
     * FIFO epoch ordering: entries are enqueued in non-decreasing target epoch snapshots.
     * If head-of-queue has not cleared its target epoch barrier, none of subsequent entries
     * can have cleared it either, enabling O(1) early exit.
     */
    while (ft->quarantine.head != ft->quarantine.tail) {
        QuarantineEntry *entry = &ft->quarantine.entries[ft->quarantine.tail % QUARANTINE_RING_SIZE];
        if (qsbr_check_all_quiescent(qsbr, entry->target_epoch)) {
            uint32_t flow_id = entry->flow_id;
            ft->quarantine.tail++;

            /* Data-Oriented: Zero heap free(), simply clear flat context and reset state to FREE */
            ft->contexts[flow_id].byte_count = 0;
            atomic_store_explicit(&ft->cold[flow_id].state, FLOW_STATE_FREE, memory_order_release);
            reclaimed++;
        } else {
            break;
        }
    }

    return reclaimed;
}

bool flow_table_audit_clean(const FlowTable *ft, uint32_t num_flows) {
    assert(ft != NULL);
    assert(num_flows <= MAX_FLOWS);

    if (ft->quarantine.head != ft->quarantine.tail) {
        return false;
    }

    for (uint32_t i = 0; i < num_flows; ++i) {
        if (flow_table_get_state(ft, i) != FLOW_STATE_FREE) {
            return false;
        }
    }

    return true;
}
