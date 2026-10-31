# Strict FIFO Burst Flush before DrainSentinel Injection

To eliminate the per-packet atomic synchronization bottleneck in inter-stage forwarding without violating the Drain Barrier Protocol, the Worker stage accumulates validated packets in a local buffer and flushes them to downstream rings via burst enqueue. Whenever an in-band `DrainSentinel` is encountered, the Worker synchronously flushes all preceding accumulated data packets before enqueueing the sentinel, strictly preserving FIFO retirement order and eliminating Use-After-Free hazards.

