#include "flow.h"
#include <stdlib.h>
#include <string.h>

static FlowGenerationEntry g_flow_gen[MAX_FLOWS];
static FlowColdContext     g_flow_cold[MAX_FLOWS];
static FlowControlBlock    g_flow_table[MAX_FLOWS];

void flow_subsystem_init(void) {
    for (uint32_t i = 0; i < MAX_FLOWS; ++i) {
        atomic_init(&g_flow_gen[i].generation, 1);
        atomic_init(&g_flow_cold[i].state, FLOW_STATE_FREE);
        g_flow_cold[i].flow_context = NULL;

        atomic_init(&g_flow_table[i].generation, 1);
        atomic_init(&g_flow_table[i].state, FLOW_STATE_FREE);
        g_flow_table[i].flow_context = NULL;
    }
}

void flow_open(uint32_t flow_id) {
    assert(flow_id < MAX_FLOWS);

    FlowContext *ctx = (FlowContext *)malloc(sizeof(FlowContext));
    assert(ctx != NULL);
    ctx->flow_id = flow_id;
    ctx->byte_count = 0;

    g_flow_cold[flow_id].flow_context = ctx;
    g_flow_table[flow_id].flow_context = ctx;

    atomic_store_explicit(&g_flow_cold[flow_id].state, FLOW_STATE_ACTIVE, memory_order_release);
    atomic_store_explicit(&g_flow_table[flow_id].state, FLOW_STATE_ACTIVE, memory_order_release);
}

uint32_t flow_get_generation(uint32_t flow_id) {
    assert(flow_id < MAX_FLOWS);
    return atomic_load_explicit(&g_flow_gen[flow_id].generation, memory_order_acquire);
}

FlowState flow_get_state(uint32_t flow_id) {
    assert(flow_id < MAX_FLOWS);
    return (FlowState)atomic_load_explicit(&g_flow_cold[flow_id].state, memory_order_acquire);
}

void *flow_get_context(uint32_t flow_id) {
    assert(flow_id < MAX_FLOWS);
    return g_flow_cold[flow_id].flow_context;
}

FlowControlBlock *flow_get_fcb(uint32_t flow_id) {
    assert(flow_id < MAX_FLOWS);
    return &g_flow_table[flow_id];
}

void flow_advance_generation(uint32_t flow_id) {
    assert(flow_id < MAX_FLOWS);
    /*
     * Release store publishes that all prior packet processing in the pipeline
     * for this flow has completely finished before the generation increments.
     */
    atomic_fetch_add_explicit(&g_flow_gen[flow_id].generation, 1, memory_order_release);
    atomic_fetch_add_explicit(&g_flow_table[flow_id].generation, 1, memory_order_release);
}

void flow_set_state(uint32_t flow_id, FlowState state) {
    assert(flow_id < MAX_FLOWS);
    atomic_store_explicit(&g_flow_cold[flow_id].state, (uint32_t)state, memory_order_release);
    atomic_store_explicit(&g_flow_table[flow_id].state, (uint32_t)state, memory_order_release);
}

void flow_reclaim_context(uint32_t flow_id) {
    assert(flow_id < MAX_FLOWS);
    free(g_flow_cold[flow_id].flow_context);
    g_flow_cold[flow_id].flow_context = NULL;
    g_flow_table[flow_id].flow_context = NULL;
    atomic_store_explicit(&g_flow_cold[flow_id].state, FLOW_STATE_FREE, memory_order_release);
    atomic_store_explicit(&g_flow_table[flow_id].state, FLOW_STATE_FREE, memory_order_release);
}
