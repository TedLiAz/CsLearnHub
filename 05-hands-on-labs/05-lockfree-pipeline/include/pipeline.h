#ifndef PIPELINE_H
#define PIPELINE_H

#include "flow.h"
#include "message.h"
#include "ring_buffer.h"
#include "qsbr.h"
#include <pthread.h>

#define DEFAULT_TOTAL_PACKETS    2000000
#define DEFAULT_ACTIVE_FLOWS     16
#define DEFAULT_ACTIVE_CHANNELS  DEFAULT_ACTIVE_FLOWS
#define RING_CAPACITY            8192
#define BURST_SIZE               32
#define QSBR_RECLAIM_INTERVAL    1024

#define QSBR_WORKER_THREAD       0

/* ──────── Per-Stage Statistics (Owned privately by each thread; no atomic overhead) ──────── */

typedef struct {
    uint64_t packets_sent;
    uint64_t sentinels_sent;
    uint64_t packets_tail_dropped;
} IngressStats;

typedef struct {
    uint64_t packets_processed;
    uint64_t packets_dropped;
    uint64_t sentinels_forwarded;
} WorkerStats;

typedef struct {
    uint64_t packets_egressed;
    uint64_t sentinels_received;
    uint64_t flows_reclaimed;
} EgressStats;

typedef struct {
    uint64_t drains_initiated;
} ControlStats;

/* ──────── Unified Pipeline Shared Context ──────── */

typedef struct {
    RingBuffer              ring1;
    RingBuffer              ring2;
    RingBuffer              ctrl_ring;
    alignas(64) PipelineMsg ring1_buf[RING_CAPACITY];
    alignas(64) PipelineMsg ring2_buf[RING_CAPACITY];
    alignas(64) PipelineMsg ctrl_ring_buf[RING_CAPACITY];
    QSBRModule              qsbr;

    uint64_t                total_packets;
    uint32_t                num_active_flows;

    atomic_uint_fast64_t    ingress_packets_sent;
    atomic_bool             control_done;
    atomic_bool             ingress_done;

    IngressStats            ingress_stats;
    WorkerStats             worker_stats;
    EgressStats             egress_stats;
    ControlStats            control_stats;
} PipelineContext;

/* ──────── Pipeline Stage Prototypes ──────── */

void pipeline_init(PipelineContext *ctx, uint64_t total_packets, uint32_t num_flows);

void *ingress_thread(void *arg);

void *worker_thread(void *arg);

void *egress_thread(void *arg);

void *control_thread(void *arg);

#endif /* PIPELINE_H */
