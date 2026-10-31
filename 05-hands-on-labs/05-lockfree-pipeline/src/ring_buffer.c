#include "ring_buffer.h"

void ring_buffer_init(RingBuffer *ring, PipelineMsg *buffer, size_t capacity) {
    /* Power-of-two capacity is strictly required for bitwise AND wrapping */
    assert((capacity & (capacity - 1)) == 0 && "capacity must be a power of 2");
    assert(buffer != NULL);

    atomic_init(&ring->prod.head, 0);
    ring->prod.cached_tail = 0;

    atomic_init(&ring->cons.tail, 0);
    ring->cons.cached_head = 0;

    ring->meta.capacity = capacity;
    ring->meta.mask     = capacity - 1;
    ring->meta.entries  = buffer;
}
