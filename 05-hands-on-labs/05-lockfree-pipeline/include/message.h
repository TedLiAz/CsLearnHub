#ifndef MESSAGE_H
#define MESSAGE_H

#include "flow.h"
#include <string.h>

/*
 * Section: PipelineMsg (Unified 32-Byte Message Envelope)
 *
 * Invariants:
 *   - Exactly 32 bytes (half a cache line). Two messages fit precisely
 *     inside one 64B cache line, avoiding split-line memory tearing.
 *   - Discriminated union allows fast-path packets, slow-path drain
 *     sentinels, and pipeline termination events to travel in the same
 *     FIFO ring without heap indirection.
 */
typedef enum {
    MSG_TYPE_PACKET         = 0,
    MSG_TYPE_DRAIN_SENTINEL = 1,
    MSG_TYPE_PIPELINE_HALT  = 2
} MsgType;

typedef struct {
    alignas(32) uint8_t   type;
    uint8_t               sub_type;
    uint16_t              flags;
    FlowHandle            handle;
    uint32_t              flow_hash;

    union {
        /* Branch A: normal data packet (fast path) */
        struct {
            void    *pkt_payload;
            uint32_t data_len;
            uint16_t queue_id;
            uint16_t rsvd;
        };

        /* Branch B: drain sentinel (slow path) */
        struct {
            uint64_t     sentinel_seq;
            atomic_uint *barrier_ack;
        };
    };
} __attribute__((aligned(32))) PipelineMsg;

static_assert(sizeof(PipelineMsg) == 32,
              "PipelineMsg must be exactly 32 bytes (half cache line)");
static_assert(alignof(PipelineMsg) == 32,
              "PipelineMsg must be 32-byte aligned");

static inline void pipeline_msg_init_packet(
    PipelineMsg *msg,
    uint32_t flow_id,
    uint32_t generation,
    uint32_t flow_hash,
    uint32_t data_len
) {
    memset(msg, 0, sizeof(*msg));
    msg->type              = MSG_TYPE_PACKET;
    msg->handle.flow_id    = flow_id;
    msg->handle.generation = generation;
    msg->flow_hash         = flow_hash;
    msg->pkt_payload       = NULL;
    msg->data_len          = data_len;
    msg->queue_id          = 0;
}

static inline void pipeline_msg_init_drain_sentinel(
    PipelineMsg *msg,
    uint32_t flow_id,
    uint32_t generation
) {
    memset(msg, 0, sizeof(*msg));
    msg->type              = MSG_TYPE_DRAIN_SENTINEL;
    msg->handle.flow_id    = flow_id;
    msg->handle.generation = generation;
    msg->sentinel_seq      = 0;
    msg->barrier_ack       = NULL;
}

static inline void pipeline_msg_init_halt(PipelineMsg *msg) {
    memset(msg, 0, sizeof(*msg));
    msg->type = MSG_TYPE_PIPELINE_HALT;
}

static inline bool pipeline_msg_is_halt(const PipelineMsg *msg) {
    return (msg->type == MSG_TYPE_PIPELINE_HALT);
}

#endif /* MESSAGE_H */
