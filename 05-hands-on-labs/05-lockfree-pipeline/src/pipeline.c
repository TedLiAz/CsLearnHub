#include "pipeline.h"
#include <immintrin.h>

void pipeline_init(PipelineContext *ctx, uint64_t total_packets, uint32_t num_flows) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->total_packets    = total_packets;
    ctx->num_active_flows = num_flows;

    ring_buffer_init(&ctx->ring1, ctx->ring1_buf, RING_CAPACITY);
    ring_buffer_init(&ctx->ring2, ctx->ring2_buf, RING_CAPACITY);
    ring_buffer_init(&ctx->ctrl_ring, ctx->ctrl_ring_buf, RING_CAPACITY);

    flow_table_init(&ctx->flow_table);
    for (uint32_t i = 0; i < num_flows; ++i) {
        flow_table_open(&ctx->flow_table, i);
    }

    qsbr_init(&ctx->qsbr, 1);

    atomic_init(&ctx->ingress_packets_sent, 0);
    atomic_init(&ctx->control_done, false);
    atomic_init(&ctx->ingress_done, false);
}

/*
 * Section: Bounded Adaptive Micro-Backoff (ADR-0006)
 *
 * Scales pause iterations from 1 up to MAX_BACKOFF_PAUSES (8) upon empty
 * dequeue or queue congestion, resetting immediately to 0 when packets arrive.
 * Strictly avoids OS syscalls (sched_yield / nanosleep) to preserve sub-microsecond
 * tail latency.
 */
#define MAX_BACKOFF_SHIFT 3  /* 1 << 3 = 8 pauses */

static inline void adaptive_backoff(uint32_t *shift) {
    uint32_t pauses = 1U << (*shift);
    for (uint32_t i = 0; i < pauses; ++i) {
        _mm_pause();
    }
    if (*shift < MAX_BACKOFF_SHIFT) {
        (*shift)++;
    }
}

static inline void adaptive_backoff_reset(uint32_t *shift) {
    *shift = 0;
}

/*
 * Section: Ingress Control Plane Handling (ADR-0004 & Control Ring)
 *
 * Invariants:
 *   - Control plane posts drain requests into `ctrl_ring` asynchronously.
 *   - Ingress is the sole consumer of `ctrl_ring` and sole writer to `ring1`.
 *   - Ingress transitions flow state to FLOW_STATE_DRAINING and injects
 *     the DrainSentinel in strict FIFO order with data packets.
 */
static inline void process_pending_drains(PipelineContext *ctx) {
    PipelineMsg req;
    while (ring_buffer_dequeue(&ctx->ctrl_ring, &req)) {
        uint32_t flow_id = req.handle.flow_id;

        flow_table_set_state(&ctx->flow_table, flow_id, FLOW_STATE_DRAINING);
        uint32_t cur_gen = flow_table_get_generation(&ctx->flow_table, flow_id);

        PipelineMsg sentinel;
        pipeline_msg_init_drain_sentinel(&sentinel, flow_id, cur_gen);

        while (!ring_buffer_enqueue(&ctx->ring1, &sentinel)) {
            _mm_pause();
        }

        ctx->ingress_stats.sentinels_sent++;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Stage 1: Ingress Stage (Packet Ingestion & Drain Sentinel Injection)
 * ═══════════════════════════════════════════════════════════════════════════ */

void *ingress_thread(void *arg) {
    PipelineContext *ctx = (PipelineContext *)arg;
    uint64_t pkt_gen_seq = 0;

    while (ctx->ingress_stats.packets_sent < ctx->total_packets &&
           !atomic_load_explicit(&ctx->ingress_done, memory_order_relaxed)) {
        process_pending_drains(ctx);

        PipelineMsg batch[BURST_SIZE];
        size_t batch_len = 0;
        bool any_active = false;

        for (size_t i = 0; i < BURST_SIZE &&
             (ctx->ingress_stats.packets_sent + batch_len) < ctx->total_packets; ++i) {
            uint32_t flow_id = (pkt_gen_seq++) % ctx->num_active_flows;

            FlowState st = flow_table_get_state(&ctx->flow_table, flow_id);
            if (st != FLOW_STATE_ACTIVE) {
                continue;
            }

            any_active = true;
            uint32_t gen = flow_table_get_generation(&ctx->flow_table, flow_id);

            pipeline_msg_init_packet(&batch[batch_len], flow_id, gen, flow_id, 64);
            batch_len++;
        }

        if (batch_len > 0) {
            size_t sent = 0;
            while (sent < batch_len) {
                size_t n = ring_buffer_enqueue_burst(&ctx->ring1,
                                                     batch + sent,
                                                     batch_len - sent);
                sent += n;
                if (n == 0) _mm_pause();
            }
            ctx->ingress_stats.packets_sent += batch_len;
            atomic_store_explicit(&ctx->ingress_packets_sent,
                                  ctx->ingress_stats.packets_sent,
                                  memory_order_relaxed);
        } else if (!any_active) {
            /* All active flows have entered DRAINING/QUIESCENT/FREE state */
            break;
        }
    }

    /* Signal to control thread that packet generation has concluded */
    atomic_store_explicit(&ctx->ingress_done, true, memory_order_release);

    /* Wait for control thread to finalize all scheduled flow drains */
    while (!atomic_load_explicit(&ctx->control_done, memory_order_acquire)) {
        process_pending_drains(ctx);
        _mm_pause();
    }
    process_pending_drains(ctx);

    /* Inject pipeline termination halt sentinel (tells downstream stages to exit) */
    PipelineMsg halt;
    pipeline_msg_init_halt(&halt);

    while (!ring_buffer_enqueue(&ctx->ring1, &halt)) {
        _mm_pause();
    }

    return NULL;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Stage 2: Worker Stage (Generational Validation & Burst Forwarding)
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * Microarchitectural Design Rationale (Pass-by-Value Invariant):
 *
 * Notice that `len` is passed by value (not as `size_t *len`).
 * Passing a pointer `&out_len` forces the compiler to treat `out_len` as aliased (address-taken),
 * spilling it from a hardware CPU register to the stack frame (`0x38(%rsp)`). This caused
 * a severe regression in earlier iterations where every single packet executed a memory load
 * and memory store to the stack.
 *
 * By passing `len` by value and resetting `out_len = 0` inline at call sites, `out_len` is
 * guaranteed to remain permanently resident inside a CPU general-purpose register (%rsi/%rdx).
 */
static inline void burst_flush_to_ring(RingBuffer *ring, PipelineMsg *msgs, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        size_t n = ring_buffer_enqueue_burst(ring, msgs + sent, len - sent);
        sent += n;
        if (n == 0) _mm_pause();
    }
}

void *worker_thread(void *arg) {
    PipelineContext *ctx = (PipelineContext *)arg;
    PipelineMsg batch[BURST_SIZE];
    PipelineMsg out_batch[BURST_SIZE];
    size_t out_len = 0;

    /*
     * Thread-Local Hardware Register Accumulators:
     *
     * Incrementing `ctx->worker_stats.packets_processed++` on every packet in the fast path
     * generates a memory Read-Modify-Write instruction (`incq (mem)`) on every packet.
     * Over 10M packets, this saturates the CPU L1 data cache load/store execution ports (Port 2/3/4/7).
     *
     * By accumulating in local variables `local_processed` and `local_dropped`, the increments
     * execute as pure 1-cycle ALU register operations (`inc %reg`). They are flushed to the shared
     * Context only upon burst completion or thread exit, eliminating millions of memory bus cycles.
     */
    uint64_t local_processed = 0;
    uint64_t local_dropped = 0;
    uint32_t backoff = 0;

    for (;;) {
        size_t count = ring_buffer_dequeue_burst(&ctx->ring1, batch, BURST_SIZE);
        if (count == 0) {
            adaptive_backoff(&backoff);
            continue;
        }
        adaptive_backoff_reset(&backoff);

        for (size_t i = 0; i < count; ++i) {
            PipelineMsg *msg = &batch[i];

            if (__builtin_expect(msg->type == MSG_TYPE_PACKET, 1)) {
                /*
                 * FAST PATH: Inlined O(1) Dense Generation Array Validation.
                 *
                 * Reads generation directly from the contiguous cacheline-aligned array.
                 * Validates flow lifecycle in 1 single-cycle instruction without locks,
                 * reference counting, or hazard pointers.
                 */
                if (__builtin_expect(flow_table_validate(&ctx->flow_table, msg->handle), 1)) {
                    FlowContext *fctx = flow_table_get_context(&ctx->flow_table, msg->handle.flow_id);
                    (void)fctx;

                    out_batch[out_len++] = *msg;
                    local_processed++;

                    /* Flush full burst into Ring 2 */
                    if (out_len == BURST_SIZE) {
                        burst_flush_to_ring(&ctx->ring2, out_batch, BURST_SIZE);
                        out_len = 0;
                    }
                } else {
                    /* Stale generation: flow retired, silently drop to prevent UAF */
                    local_dropped++;
                }
            } else if (__builtin_expect(msg->type == MSG_TYPE_DRAIN_SENTINEL, 0)) {
                /*
                 * DRAIN BARRIER PROTOCOL (ADR-0007):
                 * Synchronously flush all preceding accumulated data packets
                 * before enqueueing the sentinel to preserve strict FIFO retirement.
                 * Guarantees downstream terminal stage sees all active generation
                 * packets before advancing the retirement epoch.
                 */
                if (out_len > 0) {
                    burst_flush_to_ring(&ctx->ring2, out_batch, out_len);
                    out_len = 0;
                }

                while (!ring_buffer_enqueue(&ctx->ring2, msg)) {
                    _mm_pause();
                }
                ctx->worker_stats.sentinels_forwarded++;
            } else if (__builtin_expect(msg->type == MSG_TYPE_PIPELINE_HALT, 0)) {
                /* Flush remaining valid batch before forwarding halt */
                if (out_len > 0) {
                    burst_flush_to_ring(&ctx->ring2, out_batch, out_len);
                    out_len = 0;
                }

                while (!ring_buffer_enqueue(&ctx->ring2, msg)) {
                    _mm_pause();
                }

                /* Synchronize thread-local register counters to shared context */
                ctx->worker_stats.packets_processed += local_processed;
                ctx->worker_stats.packets_dropped   += local_dropped;

                /* Announce permanent quiescence before exit so reclaimer never blocks */
                qsbr_thread_exit(&ctx->qsbr, QSBR_WORKER_THREAD);
                return NULL;
            }
        }

        /* Flush partial batch before quiescent epoch announcement to maintain low latency */
        if (out_len > 0) {
            burst_flush_to_ring(&ctx->ring2, out_batch, out_len);
            out_len = 0;
        }

        /* Synchronize stats at burst boundary */
        ctx->worker_stats.packets_processed += local_processed;
        ctx->worker_stats.packets_dropped   += local_dropped;
        local_processed = 0;
        local_dropped   = 0;

        /* Cooperative QSBR quiescent state epoch announcement */
        qsbr_quiescent(&ctx->qsbr, QSBR_WORKER_THREAD);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Stage 3: Terminal / Egress Stage (Egress, Generation Bump & QSBR Quarantine)
 * ═══════════════════════════════════════════════════════════════════════════ */

void *egress_thread(void *arg) {
    PipelineContext *ctx = (PipelineContext *)arg;
    PipelineMsg batch[BURST_SIZE];
    uint64_t pkt_counter = 0;

    /* Thread-local hardware register accumulator eliminates memory RMW on egress */
    uint64_t local_egressed = 0;
    uint32_t backoff = 0;

    for (;;) {
        size_t count = ring_buffer_dequeue_burst(&ctx->ring2, batch, BURST_SIZE);
        if (count == 0) {
            adaptive_backoff(&backoff);
            continue;
        }
        adaptive_backoff_reset(&backoff);

        for (size_t i = 0; i < count; ++i) {
            PipelineMsg *msg = &batch[i];

            if (__builtin_expect(msg->type == MSG_TYPE_PACKET, 1)) {
                local_egressed++;
                pkt_counter++;
            } else if (__builtin_expect(msg->type == MSG_TYPE_DRAIN_SENTINEL, 0)) {
                ctx->egress_stats.packets_egressed += local_egressed;
                local_egressed = 0;
                ctx->egress_stats.sentinels_received++;

                /*
                 * Drain sentinel arrived at terminal stage:
                 * Advance global epoch and retire to quarantine pool in FlowTable.
                 */
                uint64_t target_epoch = qsbr_advance_epoch(&ctx->qsbr);
                flow_table_retire_to_quarantine(&ctx->flow_table,
                                                msg->handle.flow_id,
                                                msg->handle.generation,
                                                target_epoch);
            } else if (__builtin_expect(msg->type == MSG_TYPE_PIPELINE_HALT, 0)) {
                ctx->egress_stats.packets_egressed += local_egressed;
                local_egressed = 0;

                /* Termination halt: advance epoch and drain all quarantined flows */
                qsbr_advance_epoch(&ctx->qsbr);
                while (ctx->flow_table.quarantine.head != ctx->flow_table.quarantine.tail) {
                    ctx->egress_stats.flows_reclaimed +=
                        flow_table_poll_reclaim(&ctx->flow_table, &ctx->qsbr);
                    if (ctx->flow_table.quarantine.head == ctx->flow_table.quarantine.tail) {
                        break;
                    }
                    _mm_pause();
                }
                return NULL;
            }
        }

        /* Periodic quiescent-state quarantine reclamation */
        if (pkt_counter >= QSBR_RECLAIM_INTERVAL) {
            ctx->egress_stats.packets_egressed += local_egressed;
            local_egressed = 0;
            ctx->egress_stats.flows_reclaimed +=
                flow_table_poll_reclaim(&ctx->flow_table, &ctx->qsbr);
            pkt_counter = 0;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Control Plane: Control Thread (Asynchronous Dynamic Flow Drain Initiation)
 * ═══════════════════════════════════════════════════════════════════════════ */

void *control_thread(void *arg) {
    PipelineContext *ctx = (PipelineContext *)arg;

    uint64_t drain_interval = ctx->total_packets / (ctx->num_active_flows + 1);
    if (drain_interval == 0) drain_interval = 1;

    for (uint32_t flow = 0; flow < ctx->num_active_flows; ++flow) {
        uint64_t threshold = (uint64_t)(flow + 1) * drain_interval;
        while (atomic_load_explicit(&ctx->ingress_packets_sent, memory_order_relaxed) < threshold &&
               !atomic_load_explicit(&ctx->ingress_done, memory_order_acquire)) {
            _mm_pause();
        }

        /* Post drain request to Ingress through the lock-free Control Ring */
        PipelineMsg drain_req;
        pipeline_msg_init_drain_sentinel(&drain_req, flow, 0);

        while (!ring_buffer_enqueue(&ctx->ctrl_ring, &drain_req)) {
            _mm_pause();
        }

        ctx->control_stats.drains_initiated++;
    }

    atomic_store_explicit(&ctx->control_done, true, memory_order_release);
    return NULL;
}
